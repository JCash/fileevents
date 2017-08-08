/*
 * http://qualapps.blogspot.se/2010/05/understanding-readdirectorychangesw_19.html
 */

#if defined(_MSC_VER)

#include "fileevents.h"
#include "fileevents_internal.h"
#include <assert.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <type_traits>
#include <map>
#include <string>
#include <vector>
#include <iostream>  // cout
#include <windows.h>
#include <comdef.h>  // _com_error
#include <Shlwapi.h> // PathFindFileNameW
#include <stdint.h>

#include <sys/stat.h>
#include <sys/types.h>

static const uint32_t s_MaxNumInfos = 100;

struct SFileEventSystem;

struct SWatchInfo
{
		OVERLAPPED 	m_Overlapped;
		HANDLE 		m_Directory;

		FILE_NOTIFY_INFORMATION* m_Buffer;
		void* 					 m_AllocatedBuffer;
		FILE_NOTIFY_INFORMATION* m_DoubleBuffer;
		size_t					 m_BufferSize;

		// The filter mask that was passed in when the request was added
		uint64_t    m_Mask;

		// The path to watch
		std::string m_DirPath;
		std::string m_Path;

		// If it's not a directory, we need to filter the events
		bool m_IsDir;

		struct SFileEventSystem* m_FES;

		~SWatchInfo()
		{
			free(m_AllocatedBuffer);
			free(m_DoubleBuffer);
		}
};

struct SPlatformData
{
	// map from id to request
	std::map< HFESWatchID, SWatchInfo* > m_Watchers;
};


static void CALLBACK readdirectory_callback(  DWORD dwErrorCode,
												DWORD dwNumberOfBytesTransfered,
												LPOVERLAPPED lpOverlapped);

static bool create_directory(SWatchInfo* info)
{
	info->m_Directory = ::CreateFile(info->m_DirPath.c_str(),
									FILE_LIST_DIRECTORY,
									FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
									NULL,
									OPEN_EXISTING,
									FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
									NULL);

	return info->m_Directory != INVALID_HANDLE_VALUE;
}

static bool is_dir(const char* path)
{
	struct stat s;
	if( stat(path, &s) == 0 )
	{
		return s.st_mode & S_IFDIR;
	}
	return false;
}

static std::string get_dir_name(const std::string& path)
{
	const size_t found = path.find_last_of("/\\");
	std::string out = path.substr(0, found);
	// Trim trailing separators
	size_t len = out.size() - 1;
	while( out[len] == '\\' || out[len] == '/' )
	{
		out = out.substr(0, len);
		--len;
	}
	return out;
}

static std::string get_file_name(const std::string& path)
{
	const size_t found = path.find_last_of("/\\");
	return path.substr(found == std::string::npos ? 0 : found);
}

static bool start_request(SWatchInfo* info)
{
	bool result = ::ReadDirectoryChangesW(  info->m_Directory,
											info->m_Buffer,
											info->m_BufferSize,
											true,

											FILE_NOTIFY_CHANGE_SIZE |
											FILE_NOTIFY_CHANGE_DIR_NAME |
											FILE_NOTIFY_CHANGE_FILE_NAME |

											FILE_NOTIFY_CHANGE_ATTRIBUTES |
											FILE_NOTIFY_CHANGE_LAST_WRITE |
											FILE_NOTIFY_CHANGE_LAST_ACCESS |
											FILE_NOTIFY_CHANGE_CREATION |
											FILE_NOTIFY_CHANGE_SECURITY,

											0,
											&info->m_Overlapped,
											readdirectory_callback);

	if( !result )
	{
		DWORD err = ::GetLastError();
		_com_error error(err);
		LPCTSTR errorText = error.ErrorMessage();
		fprintf(stderr, "ReadDirectoryChangesW failed with '%s' %d\n", errorText, err);
	}

	return result;
}

static void stop_request(SWatchInfo* info)
{
	::CancelIo(info->m_Directory);
}

static void _print_flags(DWORD flags)
{
	uint64_t out = 0;
	if( flags == FILE_ACTION_ADDED ) 					printf("Added, ");
	if( flags == FILE_ACTION_REMOVED ) 					printf("Removed, ");
	if( flags == FILE_ACTION_RENAMED_OLD_NAME ) 		printf("Renamed old, ");
	if( flags == FILE_ACTION_RENAMED_NEW_NAME ) 		printf("Renamed new, ");
	if( flags == FILE_ACTION_MODIFIED ) 				printf("Modified, ");

	printf("\n");
}

static EFileEvents convert_flags(uint64_t action)
{
	uint64_t out = 0;
	if( action == FILE_ACTION_ADDED ) 					out |= FE_CREATED;
	if( action == FILE_ACTION_REMOVED ) 				out |= FE_REMOVED;
	if( action == FILE_ACTION_RENAMED_OLD_NAME ) 		out |= FE_RENAMED | FE_REMOVED;
	if( action == FILE_ACTION_RENAMED_NEW_NAME ) 		out |= FE_RENAMED | FE_CREATED;
	if( action == FILE_ACTION_MODIFIED ) 				out |= FE_MODIFIED;

	/*
	if( flags & kFSEventStreamEventFlagItemInodeMetaMod ) 	out |= FE_ATTRIBUTE;
	if( flags & kFSEventStreamEventFlagItemFinderInfoMod ) 	out |= FE_ATTRIBUTE;
	if( flags & kFSEventStreamEventFlagItemChangeOwner ) 	out |= FE_ATTRIBUTE;
	if( flags & kFSEventStreamEventFlagItemXattrMod ) 		out |= FE_ATTRIBUTE;

	if( flags & kFSEventStreamEventFlagItemIsFile ) 		out |= FE_IS_FILE;
	if( flags & kFSEventStreamEventFlagItemIsDir ) 			out |= FE_IS_DIR;
	if( flags & kFSEventStreamEventFlagItemIsSymlink ) 		out |= FE_IS_SYMLINK;
	*/

	return (EFileEvents)out;
}

static EFileEvents get_filetype_flags(const char* path)
{
	uint64_t out = 0;
	//struct stat s;
	//if( stat(path, &s) == 0 )
	{
		//if( s.st_mode & S_IFREG )	out |= FE_IS_FILE;
		//if( s.st_mode & S_IFDIR )	out |= FE_IS_DIR;

		DWORD attributes = ::GetFileAttributes(path);
		if( attributes != INVALID_FILE_ATTRIBUTES)
		{
			if( attributes & FILE_ATTRIBUTE_REPARSE_POINT )
			{
				out |= FE_IS_SYMLINK;
				if( attributes & FILE_ATTRIBUTE_DIRECTORY )
					out |= FE_IS_DIR;
				else
					out |= FE_IS_FILE;
			}
		}

		if( out == 0 )
		{
			struct stat s;
			if( stat(path, &s) == 0 )
			{
				if( s.st_mode & S_IFREG )	out |= FE_IS_FILE;
				if( s.st_mode & S_IFDIR )	out |= FE_IS_DIR;
			}
		}
	}
	return (EFileEvents)out;
}

static void process_request(SWatchInfo* info)
{
	const FILE_NOTIFY_INFORMATION* entry = info->m_Buffer;
	std::string last_path = "";
	uint32_t last_flags;

	while(true)
	{
		const FILE_NOTIFY_INFORMATION& fni = *entry;

		std::wstring wpath(fni.FileName, fni.FileName + fni.FileNameLength/sizeof(fni.FileName[0]));

		// If it could be a short filename, expand it.
		LPCWSTR wszFilename = ::PathFindFileNameW(wpath.c_str());
		int len = lstrlenW(wszFilename);
		// The maximum length of an 8.3 filename is twelve, including the dot.
		if( len <= 12 && wcschr(wszFilename, L'~') )
		{
			// Convert to the long filename form. Unfortunately, this
			// does not work for deletions, so it's an imperfect fix.
			wchar_t wbuf[MAX_PATH];
			if (::GetLongPathNameW(wpath.c_str(), wbuf, _countof(wbuf)) > 0)
				wpath = wbuf;
		}

		std::string path;
		path.assign(wpath.begin(), wpath.end());
		path = info->m_DirPath + "/" + path;

		if( last_path == path )
		{
			last_flags |= convert_flags(fni.Action);
		}
		else
		{
			if( !last_path.empty() )
			{
				// now, check if the user wanted the event, then send it
				if( last_flags & info->m_Mask )
					info->m_FES->m_Callback( last_path.c_str(), (EFileEvents)last_flags, info->m_FES->m_CallbackCtx );
			}

			last_flags = convert_flags(fni.Action) | get_filetype_flags(path.c_str());
			last_path = path;
		}

		if( fni.NextEntryOffset == 0 )
			break;

		++entry;
	};

	if( !last_path.empty() )
	{
		// now, check if the user wanted the event, then send it
		if( last_flags & info->m_Mask )
			info->m_FES->m_Callback( last_path.c_str(), (EFileEvents)last_flags, info->m_FES->m_CallbackCtx );
	}
}

static void CALLBACK readdirectory_callback(  DWORD dwErrorCode,
												DWORD dwNumberOfBytesTransfered,
												LPOVERLAPPED lpOverlapped)
{
	SWatchInfo* info = (SWatchInfo*)lpOverlapped->hEvent;

	if( dwErrorCode == ERROR_OPERATION_ABORTED )
	{
		::CloseHandle(info->m_Directory);
		delete info;
		return;
	}

	if( dwErrorCode != ERROR_SUCCESS )
	{
		fprintf(stderr, "Watch failed with %d for path %s", dwErrorCode, info->m_Path.c_str());
		::CloseHandle(info->m_Directory);
		delete info;
		return;
	}

	// This might mean overflow? Not sure.
	//if( dwNumberOfBytesTransfered == 0)
	{
		// NOTE: When reading from a network drive (i.e. samba) it may or may not support this.
		// In my case, it just returned 0, and then nothing else
	}

	if( dwNumberOfBytesTransfered )
		memcpy(info->m_DoubleBuffer, info->m_Buffer, dwNumberOfBytesTransfered);

	// refresh the watcher
	start_request(info);

	if( dwNumberOfBytesTransfered )
		process_request(info);
}


static void CALLBACK add_watch( SWatchInfo* info )
{
	SFileEventSystem* hfes = info->m_FES;

	std::lock_guard<std::mutex> lock(hfes->m_Lock);

    start_request(info);
}


void platform_thread_run(SFileEventSystem* hfes)
{
	static int i = 0;
	while( !hfes->m_Cancel )
	{
		// Need to put the thread in an alertable state
		::SleepEx(1000, TRUE);
	}
}

SPlatformData* fe_platform_init(const SFileEventSystem* hfes)
{
	(void)hfes;
	SPlatformData* pfdata = new SPlatformData;
	return pfdata;
}

void fe_platform_close(const SFileEventSystem* hfes)
{
	for(const auto &pair : hfes->m_PlatformData->m_Watchers)
	{
		// The actual deletion is done in the notification callback
		stop_request( pair.second );
	}
	delete hfes->m_PlatformData;
}

int fe_platform_add_watch(const SFileEventSystem* hfes, HFESWatchID watchid, const char* path, uint32_t mask)
{
	// Check if it already exists, then update the mask
	int i = 0;
    for(const auto &pair : hfes->m_PlatformData->m_Watchers)
    {
    	SWatchInfo* info = pair.second;
    	if( info->m_Path == path )
    	{
    		info->m_Mask = mask;
    		return 0;
    	}

        ++i;
    }

    SWatchInfo* info = new SWatchInfo;
    info->m_Mask = mask;
	info->m_DirPath = path;
	info->m_Path = path;
	info->m_IsDir = is_dir(path);
	if( info->m_IsDir )
		info->m_DirPath = info->m_Path;
	else
		info->m_DirPath = get_dir_name(info->m_Path);

	info->m_FES = const_cast<SFileEventSystem*>(hfes);

	if( !create_directory(info) )
	{
		fprintf(stderr, "Failed to create directory watcher for '%s'\n", info->m_DirPath.c_str());
		return -1;
	}

	info->m_BufferSize = sizeof(FILE_NOTIFY_INFORMATION) * s_MaxNumInfos;
	info->m_AllocatedBuffer = malloc(info->m_BufferSize + sizeof(DWORD));
	info->m_Buffer = (FILE_NOTIFY_INFORMATION*)(( (size_t)info->m_AllocatedBuffer + (sizeof(DWORD)-1) ) & (~(sizeof(DWORD)-1)));
	info->m_DoubleBuffer = (FILE_NOTIFY_INFORMATION*)malloc(info->m_BufferSize);
	assert( ( (uintptr_t)info->m_Buffer & 3) == 0 );

	memset(&info->m_Overlapped, 0, sizeof(info->m_Overlapped));

	// The hEvent isn't used, so we put our user context there.
	info->m_Overlapped.hEvent = info;

	hfes->m_PlatformData->m_Watchers[watchid] = info;

	::QueueUserAPC( (PAPCFUNC)add_watch, info->m_FES->m_Thread.native_handle(), (ULONG_PTR)info );

	return 0;
}

void fe_platform_remove_watch(const SFileEventSystem* hfes, HFESWatchID id)
{
	std::map< HFESWatchID, SWatchInfo* >::iterator it = hfes->m_PlatformData->m_Watchers.find( id );
	if( it != hfes->m_PlatformData->m_Watchers.end() )
	{
		// The actual deletion is done in the notification callback
		stop_request( it->second );
		hfes->m_PlatformData->m_Watchers.erase( it );
	}
}

bool fe_is_running(const SFileEventSystem* hfes)
{
	return hfes != 0 && hfes->m_PlatformData != 0;
}
#endif //
