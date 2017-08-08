#ifndef FSWATCHER_H_INCLUDED
#define FSWATCHER_H_INCLUDED

#include <stddef.h> // for size_t

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

enum fswatcher_create_flags
{
	FSWATCHER_CREATE_BLOCKING  = (1 << 1),
	FSWATCHER_CREATE_RECURSIVE = (1 << 2),
	FSWATCHER_CREATE_DEFAULT   = FSWATCHER_CREATE_RECURSIVE
};

/**
 *
 */
enum fswatcher_event_type
{
	FSWATCHER_EVENT_FILE_CREATE = (1 << 1),
	FSWATCHER_EVENT_FILE_REMOVE = (1 << 2),
	FSWATCHER_EVENT_FILE_MODIFY = (1 << 3),
	FSWATCHER_EVENT_FILE_MOVED  = (1 << 4),

	FSWATCHER_EVENT_ALL = FSWATCHER_EVENT_FILE_CREATE |
						  FSWATCHER_EVENT_FILE_REMOVE |
						  FSWATCHER_EVENT_FILE_MODIFY |
						  FSWATCHER_EVENT_FILE_MOVED,

	FSWATCHER_EVENT_BUFFER_OVERFLOW ///< doc me
};

/**
 *
 */
typedef struct fswatcher* fswatcher_t;

/**
 *
 */
struct fswatcher_allocator
{
	/**
	 *
	 */
	void* ( *realloc )( fswatcher_allocator* allocator, void* old_ptr, size_t old_size, size_t new_size );

	/**
	 *
	 */
	void  ( *free )( fswatcher_allocator* allocator, void* ptr );
};

/**
 *
 */
struct fswatcher_event_handler
{
	/**
	 *
	 */
	bool ( *callback )( fswatcher_event_handler* handler, fswatcher_event_type evtype, const char* src, const char* dst );
};

/**
 *
 */
fswatcher_t fswatcher_create( fswatcher_create_flags flags, fswatcher_event_type types, const char* watch_dir, fswatcher_allocator* allocator );

/**
 *
 */
void fswatcher_destroy( fswatcher_t );

/**
 *
 */
void fswatcher_poll( fswatcher_t watcher, fswatcher_event_handler* handler, fswatcher_allocator* allocator );

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif // FSWATCHER_H_INCLUDED
