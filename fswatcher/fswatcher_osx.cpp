#include "fswatcher.h"
#include <assert.h>
#include <stdlib.h> // malloc
#include <CoreServices/CoreServices.h>

struct fswatcher
{
	fswatcher_create_flags	 flags;
	fswatcher_event_type	 types;
	FSEventStreamRef		 stream;
	FSEventStreamEventId	 last_id;
	fswatcher_event_handler* handler;
	fswatcher_allocator* 	 allocator;
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

static fswatcher_event_type convert_flags(FSEventStreamEventFlags flags)
{
	unsigned int out = 0;
	if( flags & kFSEventStreamEventFlagItemRemoved ) 			out |= FSWATCHER_EVENT_FILE_REMOVE;
	else if( flags & kFSEventStreamEventFlagItemCreated ) 		out |= FSWATCHER_EVENT_FILE_CREATE;
	else if( flags & kFSEventStreamEventFlagItemRenamed ) 		out |= FSWATCHER_EVENT_FILE_MOVED;
	else if( flags & kFSEventStreamEventFlagItemModified ) 		out |= FSWATCHER_EVENT_FILE_MODIFY;
	else if( flags & kFSEventStreamEventFlagItemInodeMetaMod ) 	out |= FSWATCHER_EVENT_FILE_MODIFY;
	else if( flags & kFSEventStreamEventFlagItemFinderInfoMod ) out |= FSWATCHER_EVENT_FILE_MODIFY;
	else if( flags & kFSEventStreamEventFlagItemChangeOwner ) 	out |= FSWATCHER_EVENT_FILE_MODIFY;
	else if( flags & kFSEventStreamEventFlagItemXattrMod ) 		out |= FSWATCHER_EVENT_FILE_MODIFY;

	/*
	if( flags & kFSEventStreamEventFlagItemIsFile ) 		out |= FE_IS_FILE;
	if( flags & kFSEventStreamEventFlagItemIsDir ) 			out |= FE_IS_DIR;
	if( flags & kFSEventStreamEventFlagItemIsSymlink ) 		out |= FE_IS_SYMLINK;
	*/

	return (fswatcher_event_type)out;
}

static void handle_event(ConstFSEventStreamRef stream,
                 void* _ctx,
                 size_t numEvents,
                 const char* paths[],
                 const FSEventStreamEventFlags eventFlags[],
                 const FSEventStreamEventId eventIds[]
                 )
{
	(void)stream;

	fswatcher* ctx = (fswatcher*)_ctx;
	const char* renamedpath = 0;
	for( size_t i = 0; i < numEvents; ++i )
	{
		if( eventFlags[i] & kFSEventStreamEventFlagHistoryDone)
			continue;

		if( eventIds[i] <= ctx->last_id )
		{
			printf("Duplicate event found:\n");
			printf("\t%s\t", paths[i]);
			_print_flags(eventFlags[i]);
			continue;
		}

		FSEventStreamEventFlags eventflags = eventFlags[i];
		const char* src = paths[i];
		const char* dst = 0;

		printf("%s\t", src);
		_print_flags(eventFlags[i]);

		// Rename comes as two events: renamed, create
		if( eventflags & kFSEventStreamEventFlagItemRenamed )
		{
			if( !renamedpath )
			{
				renamedpath = paths[i];
				continue;
			}
			else
			{
				src = renamedpath;
				dst = paths[i];
				renamedpath = 0;
			}
		}

		// We mask out meta events that we don't support
		fswatcher_event_type flags = convert_flags(eventflags);

		printf("flags      %x\n", (unsigned int)flags);
		printf("ctx->flags %x\n", (unsigned int)ctx->flags);


		// now, check if the user wanted the event, then send it
		if( ctx->types & flags )
			ctx->handler->callback( ctx->handler, flags, src, dst );

		ctx->last_id = eventIds[i];
	}
}

fswatcher_t fswatcher_create( fswatcher_create_flags flags, fswatcher_event_type types, const char* watch_dir, fswatcher_allocator* allocator )
{
	fswatcher* ctx = 0;
	if( allocator )
		ctx = (fswatcher*)allocator->realloc( allocator, 0, 0, sizeof(fswatcher) );
	else
		ctx = (fswatcher*)malloc(sizeof(fswatcher));
	if( !ctx )
		return 0;

	ctx->flags   	= flags;
	ctx->types   	= types;
	ctx->stream	 	= 0;
	ctx->last_id 	= FSEventsGetCurrentEventId();
	ctx->handler 	= 0;
	ctx->allocator	= allocator;

	FSEventStreamContext context = {0, (void*)ctx, NULL, NULL, NULL};

	int numpaths = 1;
	CFMutableArrayRef cfpaths = CFArrayCreateMutable(kCFAllocatorDefault, (CFIndex)numpaths, &kCFTypeArrayCallBacks);
	if( cfpaths == 0 )
	{
		free(ctx);
		return 0;
	}

	CFStringRef cfstr = CFStringCreateWithCString(kCFAllocatorDefault, watch_dir, kCFStringEncodingUTF8);
	CFArraySetValueAtIndex(cfpaths, 0, cfstr);
	CFRelease(cfstr);

	ctx->stream = FSEventStreamCreate( kCFAllocatorDefault,
										 (FSEventStreamCallback)&handle_event,
										 &context,
										 (CFArrayRef) cfpaths,
										 ctx->last_id,	// Restart at the last collected "time"
										 0,
										 kFSEventStreamCreateFlagWatchRoot |
										 kFSEventStreamCreateFlagFileEvents
										 //| kFSEventStreamCreateFlagNoDefer
										 );
	CFRelease(cfpaths);

	FSEventStreamScheduleWithRunLoop(ctx->stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	Boolean started = FSEventStreamStart(ctx->stream);
	if( !started )
	{
		FSEventStreamUnscheduleFromRunLoop(ctx->stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
		FSEventStreamInvalidate(ctx->stream);
		FSEventStreamRelease(ctx->stream);
		free(ctx);
		return 0;
	}
	return ctx;
}

void fswatcher_destroy( fswatcher* ctx )
{
	if( ctx->stream )
	{
		FSEventStreamStop(ctx->stream);
		FSEventStreamUnscheduleFromRunLoop(ctx->stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
		FSEventStreamInvalidate(ctx->stream);
		FSEventStreamRelease(ctx->stream);
		ctx->stream = 0;
	}

	fswatcher_allocator* allocator = ctx->allocator;
	if( allocator )
		allocator->free( allocator, ctx );
	else
		free(ctx);
}


void fswatcher_poll( fswatcher* ctx, fswatcher_event_handler* handler, fswatcher_allocator* allocator )
{
	(void)allocator;
	if( !handler )
		return;

	ctx->handler = handler;

	// TODO: Decide what to do for blocking mode
	/*if( ctx->flags & FSWATCHER_CREATE_BLOCKING )
		FSEventStreamFlushSync(ctx->stream);
	else*/
		FSEventStreamFlushSync(ctx->stream);

	ctx->handler = 0;
}



