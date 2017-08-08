#include <thread>
#include <mutex>
#include <map>
#include <string>
#include <stdio.h>

#include "fileevents.h"
#include "fileevents_internal.h"

#define EVENT_SIZE  	( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN   ( 1024 * ( EVENT_SIZE + 16 ) )

struct SPlatformData
{
	int	m_Fd;	// the inotify instance

	// Maps watch id to file handle
	std::map<HFESWatchID, int> m_WatchHandles;

	bool m_IsRunning;
	bool _padding[7];
};

static void _print_flags(FSEventStreamEventFlags flags)
{
	/*
	if( flags & kFSEventStreamEventFlagItemCreated ) 		printf("Created, ");
	if( flags & kFSEventStreamEventFlagItemRemoved ) 		printf("Removed, ");
	if( flags & kFSEventStreamEventFlagItemInodeMetaMod ) 	printf("InodeMetaMod, ");
	if( flags & kFSEventStreamEventFlagItemRenamed ) 		printf("Renamed, ");
	if( flags & kFSEventStreamEventFlagItemModified ) 		printf("Modified, ");
	if( flags & kFSEventStreamEventFlagItemFinderInfoMod ) 	printf("FinderInfoMod, ");
	if( flags & kFSEventStreamEventFlagItemChangeOwner ) 	printf("ChangeOwner, ");
	if( flags & kFSEventStreamEventFlagItemXattrMod ) 		printf("XattrMod, ");
	if( flags & kFSEventStreamEventFlagItemIsFile ) 		printf("IsFile, ");
	if( flags & kFSEventStreamEventFlagItemIsDir ) 			printf("IsDir, ");
	if( flags & kFSEventStreamEventFlagItemIsSymlink ) 		printf("IsSymlink, ");

	if( flags & kFSEventStreamEventFlagMustScanSubDirs ) 	printf("MustScanSubdirs, ");
	if( flags & kFSEventStreamEventFlagUserDropped ) 		printf("UserDropped, ");
	if( flags & kFSEventStreamEventFlagKernelDropped ) 		printf("KernelDropped, ");
	if( flags & kFSEventStreamEventFlagEventIdsWrapped ) 	printf("EventIdsWrapped, ");
	if( flags & kFSEventStreamEventFlagHistoryDone ) 		printf("HistoryDone, ");
	if( flags & kFSEventStreamEventFlagRootChanged ) 		printf("RootChanged, ");
	if( flags & kFSEventStreamEventFlagMount ) 				printf("Mount, ");
	if( flags & kFSEventStreamEventFlagUnmount ) 			printf("Unmount, ");*/

	printf("\n");
}

static EFileEvents convert_flags(FSEventStreamEventFlags flags)
{
	uint64_t out = 0;
	/*
	if( flags & kFSEventStreamEventFlagItemCreated ) 		out |= FE_CREATED;
	if( flags & kFSEventStreamEventFlagItemRemoved ) 		out |= FE_REMOVED;
	if( flags & kFSEventStreamEventFlagItemRenamed ) 		out |= FE_RENAMED;
	if( flags & kFSEventStreamEventFlagItemModified ) 		out |= FE_MODIFIED;

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

// taken from http://womble.decadent.org.uk/readdir_r-advisory.html
size_t dirent_buf_size(DIR * dirp)
{
    long name_max;
    size_t name_end;
#   if defined(HAVE_FPATHCONF) && defined(HAVE_DIRFD) \
       && defined(_PC_NAME_MAX)
        name_max = fpathconf(dirfd(dirp), _PC_NAME_MAX);
        if (name_max == -1)
#           if defined(NAME_MAX)
                name_max = (NAME_MAX > 255) ? NAME_MAX : 255;
#           else
                return (size_t)(-1);
#           endif
#   else
#       if defined(NAME_MAX)
            name_max = (NAME_MAX > 255) ? NAME_MAX : 255;
#       else
#           error "buffer size for readdir_r cannot be determined"
#       endif
#   endif
    name_end = (size_t)offsetof(struct dirent, d_name) + name_max + 1;
    return (name_end > sizeof(struct dirent) ? name_end : sizeof(struct dirent));
}


static void add_dir(SFileEventSystem* hfes, const char* path)
{
	// scan dir, and for each item, send callback event
	// for sub directories, recurse!

	DIR* dir = opendir(path);
	if( !dir )
		return;
	size_t size = dirent_buf_size(dir);
	struct dirent* buf = (struct dirent*)alloca(size);
	struct dirent* ent;

	printf("dir %s\n", path);
	while( readdir_r(dir, buf, &ent) == 0 && ent != 0 )
	{
		printf("name %s", ent->d_name);

		struct stat st;
		lstat(ent->d_name, &st);
		if(S_ISDIR(st.st_mode))
		   printf("\t DIRECTORY\n");
		else
		   printf("\t FILE\n");
	}

	closedir(dir);
}

static void remove_dir(SFileEventSystem* hfes, const char* path)
{

}

static void start_stream(SFileEventSystem* hfes)
{
}

static void stop_stream(SFileEventSystem* hfes)
{
}

void platform_thread_run(SFileEventSystem* hfes)
{
	while( !hfes->m_Cancel )
	{
		if( hfes->m_Updated )
		{
			std::lock_guard<std::mutex> lock(hfes->m_Lock);

			stop_stream(hfes);
			start_stream(hfes);
			hfes->m_Updated = false;
		}

		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
	}
	CFRunLoopStop(CFRunLoopGetCurrent());
	std::lock_guard<std::mutex> lock(hfes->m_Lock);
	stop_stream(hfes);
}

SPlatformData* fe_platform_init(const SFileEventSystem* hfes)
{
	(void)hfes;
	SPlatformData* pfdata = new SPlatformData;
	pfdata->m_Fd = inotify_init();
	pfdata->m_IsRunning = false;
	return pfdata;
}

void fe_platform_close(const SFileEventSystem* hfes)
{
	close(hfes->m_PlatformData->m_Fd);
	delete hfes->m_PlatformData;
}

bool fe_is_running(const SFileEventSystem* hfes)
{
	return hfes->m_PlatformData->m_IsRunning;
}

int fe_platform_add_watch(const SFileEventSystem* hfes, HFESWatchID watchid, const char* path, uint32_t mask)
{

	return 0;
}

