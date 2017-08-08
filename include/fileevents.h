/**
 * read more:
 *
 * inotify: http://www.ibm.com/developerworks/library/l-inotify/
 *
 */

#pragma once

#include <stdint.h>

#if defined(_MSC_VER)
	#define DLL_EXPORT __declspec(dllexport)
#else
	#define DLL_EXPORT
#endif

extern "C" {

enum EFileEvents
{
	FE_CREATED		= 0x00000001,
	FE_REMOVED		= 0x00000002,
	FE_RENAMED		= 0x00000004,
	FE_MODIFIED		= 0x00000008,

	//!< Darwin: kFSEventStreamEventFlagItemInodeMetaMod, kFSEventStreamEventFlagItemFinderInfoMod, kFSEventStreamEventFlagItemChangeOwner, kFSEventStreamEventFlagItemXattrMod
	//!< Linux: IN_ATTRIB
	FE_ATTRIBUTE	= 0x00000010,

	FE_IS_FILE 		= 0x00010000,
	FE_IS_DIR 		= 0x00020000,
	FE_IS_SYMLINK	= 0x00040000,

	FE_ALL = FE_CREATED | FE_REMOVED | FE_RENAMED | FE_MODIFIED
};


/* Windows: http://msdn.microsoft.com/en-us/library/cc246556.aspx
 * Darwin: https://developer.apple.com/library/mac/#documentation/Darwin/Reference/FSEvents_Ref/Reference/reference.html#//apple_ref/c/func/FSEventStreamCreate
 *
 */


struct SFileEventSystem;

/// Handle to a file event system instance
typedef SFileEventSystem* HFES;
typedef int64_t HFESWatchID;


/** The callback function type
 * @param path	The path of the file/folder
 * @param flags	The flags making up the type of the event
 * @param ctx	The user supplied context that was registered to fe_init()
 */
typedef int (*fe_callback)( const char* path, EFileEvents flags, void* ctx );


/** Used for initialization of the system
 */
struct SFileEventsCreateParams
{
	SFileEventsCreateParams();

	fe_callback	m_Callback;		//!< The callback that receives file events
	void*		m_CallbackCtx;	//!< A user specified context that is passed on to the callback with each event.
	bool 		m_Verbose;		//!< Enables debug print outs
	bool		_padding[7];
};


/** Creates a file event system. At least one watch must be added before any events are sent.
 *
 * @param params	The creation params
 * @return 			Non zero if the call succeeded, 0 if the call failed.
 */
DLL_EXPORT HFES fe_init(const SFileEventsCreateParams& params);

/** Shuts down the file event system.
 *
 * @note:	It is not a requirement to remove all watchers before closing down
 *
 * @param handle	The handle returned by fe_init()
 */
DLL_EXPORT void fe_close(HFES handle);



/** Registers a path to the watch list
 *
 * @note:	It is not a requirement to remove all watchers before closing down
 * @note:	It is possible to update the event mask for a previously registered path by calling this function a second time
 *
 * @param handle	The file events system
 * @param path		The path to watch (folder or file)
 * @param mask		The events that should be caught for the path. 0 means all events.
 * @return:	On success, it returns a watch descriptor (ID). On failure, it returns -1.
 */
DLL_EXPORT HFESWatchID fe_add_watch(HFES handle, const char* path, uint32_t mask);


/** Removes a previously registered path from the watch list
 *
 * @param handle	The file events system
 * @param id		The file watcher returned by fe_add_watch
 * @return:	On success, it returns 0. On failure, it returns -1
 */
DLL_EXPORT int32_t fe_remove_watch(HFES handle, HFESWatchID id);

} // extern C

