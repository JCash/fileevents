/*
 *
 * Good reads for learning:
 * http://developer.apple.com/library/mac/#featuredarticles/FileSystemEvents/_index.html
 * http://developer.apple.com/library/mac/#documentation/Darwin/Reference/FSEvents_Ref/Reference/reference.html
 * http://stackoverflow.com/questions/11556545/fsevents-c-example
 * https://github.com/ttilley/fsevent_watch/blob/master/fsevent_watch/main.c
 * http://bazaar.launchpad.net/~mikemc/+junk/python-macfsevents/view/head:/_fsevents.c
 *
 */

#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <string>
#include <CoreServices/CoreServices.h>

#include "fileevents.h"
#include "fileevents_internal.h"

struct SPlatformData
{
	FSEventStreamRef m_Stream;
	// Used when restarting the stream from a given point
	FSEventStreamEventId m_LastId;

	bool m_IsRunning;
	bool _padding[7];
};


static void _print_flags(FSEventStreamEventFlags flags)
{
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
	if( flags & kFSEventStreamEventFlagUnmount ) 			printf("Unmount, ");

	printf("\n");
}


static EFileEvents convert_flags(FSEventStreamEventFlags flags)
{
	uint64_t out = 0;
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

	return (EFileEvents)out;
}

static void handle_event(ConstFSEventStreamRef stream,
                 void* ctx,
                 size_t numEvents,
                 const char* paths[],
                 const FSEventStreamEventFlags eventFlags[],
                 const FSEventStreamEventId eventIds[]
                 )
{
	(void)stream;

	SFileEventSystem* hfes = (SFileEventSystem*)ctx;
	for( size_t i = 0; i < numEvents; ++i )
	{
		if( eventFlags[i] & kFSEventStreamEventFlagHistoryDone)
			continue;

		if( hfes->m_Verbose )
		{
			printf("FSEvents %s id: %llu   ", paths[i], eventIds[i] );
			_print_flags(eventFlags[i]);
		}

		// We mask out meta events that we don't support
		EFileEvents flags = convert_flags(eventFlags[i] & 0xFFFFFF00);

		// now, check if the user wanted the event, then send it
		if( flags & FE_ALL )
			hfes->m_Callback( paths[i], flags, hfes->m_CallbackCtx );

		hfes->m_PlatformData->m_LastId = eventIds[i];
	}
}

static void stop_stream(SFileEventSystem* hfes)
{
	if( hfes->m_PlatformData->m_Stream )
	{
		FSEventStreamFlushSync(hfes->m_PlatformData->m_Stream);
		hfes->m_PlatformData->m_IsRunning = false;
		FSEventStreamStop(hfes->m_PlatformData->m_Stream);
		FSEventStreamUnscheduleFromRunLoop(hfes->m_PlatformData->m_Stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
		FSEventStreamInvalidate(hfes->m_PlatformData->m_Stream);
		FSEventStreamRelease(hfes->m_PlatformData->m_Stream);
		hfes->m_PlatformData->m_Stream = 0;
	}
}

static void start_stream(SFileEventSystem* hfes)
{
	if( hfes->m_PathsToWatch.empty() )
		return;

	FSEventStreamContext context = {0, (void*)hfes, NULL, NULL, NULL};

	CFMutableArrayRef cfpaths;
	cfpaths = CFArrayCreateMutable(kCFAllocatorDefault, (CFIndex)hfes->m_PathsToWatch.size(), &kCFTypeArrayCallBacks);
	if( cfpaths == 0 )
		return;

	int i = 0;
    for(const auto &pair : hfes->m_PathsToWatch)
    {
    	const char* path = pair.second.first.c_str();
    	CFStringRef cfstr = CFStringCreateWithCString(kCFAllocatorDefault, path, kCFStringEncodingUTF8);
        CFArraySetValueAtIndex(cfpaths, i, cfstr);
        CFRelease(cfstr);
        ++i;
    }

    uint32_t flags = kFSEventStreamCreateFlagWatchRoot |
			 	 	 kFSEventStreamCreateFlagFileEvents;

	hfes->m_PlatformData->m_Stream = FSEventStreamCreate( kCFAllocatorDefault,
										 (FSEventStreamCallback)&handle_event,
										 &context,
										 (CFArrayRef) cfpaths,
										 hfes->m_PlatformData->m_LastId,
										 0,
										 flags | kFSEventStreamCreateFlagNoDefer
										 );
	CFRelease(cfpaths);

	FSEventStreamScheduleWithRunLoop(hfes->m_PlatformData->m_Stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

	Boolean startedOK = FSEventStreamStart(hfes->m_PlatformData->m_Stream);
	if( !startedOK )
	{
		fprintf(stderr, "Failed to start fsevent stream\n");
		FSEventStreamUnscheduleFromRunLoop(hfes->m_PlatformData->m_Stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
		FSEventStreamInvalidate(hfes->m_PlatformData->m_Stream);
		FSEventStreamRelease(hfes->m_PlatformData->m_Stream);
		hfes->m_PlatformData->m_Stream = 0;
		return;
	}

	FSEventStreamFlushSync(hfes->m_PlatformData->m_Stream);

	CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);

	hfes->m_PlatformData->m_IsRunning = true;
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
	pfdata->m_Stream = 0;
	pfdata->m_LastId = FSEventsGetCurrentEventId();
	pfdata->m_IsRunning = false;
	return pfdata;
}

void fe_platform_close(const SFileEventSystem* hfes)
{
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
