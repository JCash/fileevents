#include <Python.h>
#include <signal.h>
#include "fileevents.h"

#define DBG()	{ printf("%s( %d )\n", __FILE__, __LINE__); fflush(stdout); }


struct SFileEventsInfo
{
	HFES m_FES;
    PyObject* m_Callback;
    PyThreadState* m_State;
};


class PyStateLock
{
    PyThreadState* m_OldState;
public:

	PyStateLock(PyThreadState* state)
	{
	    PyEval_AcquireLock();
		m_OldState = PyThreadState_Swap(state);
	}

	~PyStateLock()
	{
		PyThreadState_Swap(m_OldState);
	    PyEval_ReleaseLock();
	}
};

/*
class IncRefLock
{
	PyObject* m_Object;
public:
	IncRefLock(PyObject* obj) : m_Object(obj)
	{
		Py_XINCREF(m_Object);
	}
	~IncRefLock()
	{
		Py_XDECREF(m_Object);
	}
};
*/


static int pyfileevents_callback(const char* path, EFileEvents flags, void* ctx)
{
	printf("path %s  %x\n", path, flags);
	fflush(stdout);

	DBG();

    SFileEventsInfo* info = (SFileEventsInfo*)PyCapsule_GetPointer((PyObject*)ctx, "fileevents_handle");

	DBG();

    if( !info )
    {
    	DBG();
    	return 0;
    }

	DBG();

    PyStateLock statelock(info->m_State);

	DBG();

    PyObject* retval = PyObject_CallFunction(info->m_Callback, "si", path, flags);

	DBG();

	if( retval != 0 )
		Py_DECREF(retval);

	return 0;
}

static PyObject* pyfileevents_init(PyObject* self, PyObject* args)
{
    PyObject* callback;
    if( !PyArg_ParseTuple(args, "O", &callback) )
	{
    	PyErr_SetString(PyExc_ValueError, "No callback specified");
    	return 0;
	}

    if( !PyCallable_Check(callback) )
    {
    	PyErr_SetString(PyExc_ValueError, "Callback is not callable!");
    	return 0;
    }

    if( !PyEval_ThreadsInitialized() )
    {
    	PyEval_InitThreads();
    }

    SFileEventsInfo* info = new SFileEventsInfo;
    PyObject* pyinfo = PyCapsule_New((void*)info, "fileevents_handle", 0);

    info->m_FES = fe_init(pyfileevents_callback, pyinfo);
    if( info->m_FES == 0 )
    {
    	delete info;
    	PyErr_SetString(PyExc_ValueError, "Failed to initialize the file event system.");
    	Py_DECREF(pyinfo);
    	return 0;
    }

    info->m_State = PyThreadState_Get();
    info->m_Callback = callback;
    Py_INCREF(callback);

    return pyinfo;
}

static PyObject* pyfileevents_close(PyObject* self, PyObject* args)
{
    PyObject* pyinfo;
    if( !PyArg_ParseTuple(args, "O", &pyinfo) )
	{
    	PyErr_SetString(PyExc_ValueError, "No handle specified");
    	return 0;
	}

    if( pyinfo == Py_None )
    {
    	PyErr_SetString(PyExc_ValueError, "Handle is None");
    	return 0;
    }

    SFileEventsInfo* info = (SFileEventsInfo*)PyCapsule_GetPointer(pyinfo, "fileevents_handle");
    if( !info )
    {
    	PyErr_SetString(PyExc_ValueError, "Handle is of wrong type!");
    	return 0;
    }

    fe_close(info->m_FES);
    Py_DECREF(info->m_Callback);
    delete info;

	Py_RETURN_NONE;
}

static PyObject* pyfileevents_add_watch(PyObject* self, PyObject* args)
{
    PyObject* pyinfo;
    char* path;
    int flags;
    if( !PyArg_ParseTuple(args, "Osi", &pyinfo, &path, &flags) )
    {
    	return 0;
    }

	DBG();

    SFileEventsInfo* info = (SFileEventsInfo*)PyCapsule_GetPointer(pyinfo, "fileevents_handle");
    if( !info )
    {
    	PyErr_SetString(PyExc_ValueError, "Handle is of wrong type!");
    	return 0;
    }

    HFESWatchID watchid = fe_add_watch(info->m_FES, path, (EFileEvents)flags);
    if( watchid == -1 )
    {
    	PyErr_SetString(PyExc_ValueError, "Error adding watch");
        return 0;
    }

    PyObject* pywatchid = PyCapsule_New((void*)watchid, "fileevents_watchhandle", 0);
    return pywatchid;
}

static PyObject* pyfileevents_remove_watch(PyObject* self, PyObject* args)
{
    PyObject* pyinfo;
    PyObject* pywatchid;
    if( !PyArg_ParseTuple(args, "OO", &pyinfo, &pywatchid) )
    {
        Py_RETURN_NONE;
    }

    SFileEventsInfo* info = (SFileEventsInfo*)PyCapsule_GetPointer(pyinfo, "fileevents_handle");
    HFESWatchID watchid = (HFESWatchID)PyCapsule_GetPointer(pywatchid, "fileevents_watchhandle");

    fe_remove_watch(info->m_FES, watchid);

    Py_RETURN_NONE;
}

/*
static PyObject* pyfsevents_loop(PyObject* self, PyObject* args) {
    PyObject* thread;
    if (!PyArg_ParseTuple(args, "O:loop", &thread))
        return NULL;

    PyEval_InitThreads();

    // allocate info and store thread state
    PyObject* value = PyDict_GetItem(loops, thread);

    if (value == NULL) {
        CFRunLoopRef loop = CFRunLoopGetCurrent();
        value = PyCObject_FromVoidPtr((void*) loop, PyMem_Free);
        PyDict_SetItem(loops, thread, value);
        Py_INCREF(thread);
        Py_INCREF(value);
    }

    // no timeout, block until events
    Py_BEGIN_ALLOW_THREADS;
    CFRunLoopRun();
    Py_END_ALLOW_THREADS;

    // cleanup state info data
    if (PyDict_DelItem(loops, thread) == 0) {
        Py_DECREF(thread);
        Py_INCREF(value);
    }

    if (PyErr_Occurred()) return NULL;

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* pyfsevents_schedule(PyObject* self, PyObject* args) {
    PyObject* thread;
    PyObject* stream;
    PyObject* paths;
    PyObject* callback;
    PyObject* show_file_events;

    if (!PyArg_ParseTuple(args, "OOOOO:schedule",
                          &thread, &stream, &callback, &paths, &show_file_events))
        return NULL;

    // stream must not already have been scheduled
    if (PyDict_Contains(streams, stream) == 1) {
        return NULL;
    }

    // create path array
    CFMutableArrayRef cfArray;
    cfArray = CFArrayCreateMutable(kCFAllocatorDefault, 1,
                                   &kCFTypeArrayCallBacks);
    if (cfArray == NULL)
        return NULL;

    int i;
    Py_ssize_t size = PyList_Size(paths);
    const char* path;
    CFStringRef cfStr;
    for (i=0; i<size; i++) {
        path = PyString_AS_STRING(PyList_GetItem(paths, i));
        cfStr = CFStringCreateWithCString(kCFAllocatorDefault,
                                          path,
                                          kCFStringEncodingUTF8);
        CFArraySetValueAtIndex(cfArray, i, cfStr);
        CFRelease(cfStr);
    }

    // allocate stream info structure
    FSEventStreamInfo * info = PyMem_New(FSEventStreamInfo, 1);

    // create event stream
    FSEventStreamContext context = {0, (void*) info, NULL, NULL, NULL};
    FSEventStreamRef fsstream = NULL;

    UInt32 flags = kFSEventStreamCreateFlagNoDefer;
    if(show_file_events == Py_True){
      flags = flags | kFSEventStreamCreateFlagFileEvents;
    }


    fsstream = FSEventStreamCreate(kCFAllocatorDefault,
                                   (FSEventStreamCallback)&handler,
                                   &context,
                                   cfArray,
                                   kFSEventStreamEventIdSinceNow,
                                   0, // latency
				   flags);

    CFRelease(cfArray);

    PyObject* value = PyCObject_FromVoidPtr((void*) fsstream, PyMem_Free);
    PyDict_SetItem(streams, stream, value);

    // get runloop reference from observer info data or current
    value = PyDict_GetItem(loops, thread);
    CFRunLoopRef loop;
    if (value == NULL) {
        loop = CFRunLoopGetCurrent();
    } else {
        loop = (CFRunLoopRef) PyCObject_AsVoidPtr(value);
    }

    FSEventStreamScheduleWithRunLoop(fsstream, loop, kCFRunLoopDefaultMode);

    // set stream info for callback
    info->callback = callback;
    info->stream = fsstream;
    info->loop = loop;
    info->state = PyThreadState_Get();
    Py_INCREF(callback);

    // start event streams
    if (!FSEventStreamStart(fsstream)) {
        FSEventStreamInvalidate(fsstream);
        FSEventStreamRelease(fsstream);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* pyfsevents_unschedule(PyObject* self, PyObject* stream) {
    PyObject* value = PyDict_GetItem(streams, stream);
    PyDict_DelItem(streams, stream);
    FSEventStreamRef fsstream = PyCObject_AsVoidPtr(value);

    FSEventStreamStop(fsstream);
    FSEventStreamInvalidate(fsstream);
    FSEventStreamRelease(fsstream);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* pyfsevents_stop(PyObject* self, PyObject* thread) {
    PyObject* value = PyDict_GetItem(loops, thread);
    CFRunLoopRef loop = PyCObject_AsVoidPtr(value);

    // stop runloop
    if (loop) {
        CFRunLoopStop(loop);
    }

    Py_INCREF(Py_None);
    return Py_None;
}

*/


static PyMethodDef methods[] = {
	{"init", pyfileevents_init, METH_VARARGS, "Initializes the file event system. Returns a handle to the system."},
	{"close", pyfileevents_close, METH_VARARGS, "Stops and destroys the file event system"},
    {"add_watch", pyfileevents_add_watch, METH_VARARGS, "Adds a path (with flags) to the file event system. Returns watch handle."},
    {"remove_watch", pyfileevents_remove_watch, METH_VARARGS, "Removes a watch handle from the file event system."},
    {NULL},
};

static char doc[] = "A cross platform, threaded file system events interface. Inspired by the inotify interface.";

PyMODINIT_FUNC initfileevents(void) {
    PyObject* mod = Py_InitModule3("fileevents", methods, doc);
    if(mod == 0)
    	return;
    PyModule_AddIntConstant(mod, "FE_CREATED", FE_CREATED);
    PyModule_AddIntConstant(mod, "FE_REMOVED", FE_REMOVED);
    PyModule_AddIntConstant(mod, "FE_RENAMED", FE_RENAMED);
    PyModule_AddIntConstant(mod, "FE_MODIFIED", FE_MODIFIED);
    PyModule_AddIntConstant(mod, "FE_IS_FILE", FE_IS_FILE);
    PyModule_AddIntConstant(mod, "FE_IS_DIR", FE_IS_DIR);
    PyModule_AddIntConstant(mod, "FE_IS_DIR", FE_IS_DIR);
    PyModule_AddIntConstant(mod, "FE_ALL", FE_ALL);
}
