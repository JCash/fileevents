
The FileEvents library
======================

The file events library started as an experiment to try to learn things I haven't tried before:
Using file events (on any platform) and using C++11 threads.

Goals
=====

There were a few distinct goals that inspired the fileevents library

* One library that works for "all" platforms
* Tiny code.
* C++11 threads
* No Update() loop. All events must be processed by a separate thread


Building it
===========

Using waf to build requires you have python in your path:
> ./waf configure build test

The library is built with all warnings turned on (pedantic) to make
it easier to integrate into your own projects.
 


Notes
=====

There are some things that are good to know.

The implementations for each platform are based on these apis:
* Windows - [ReadDirectoryChangesW](http://msdn.microsoft.com/en-us/library/windows/desktop/aa365465(v=vs.85).aspx)
* OSX - [FSEvents](https://developer.apple.com/library/mac/documentation/Darwin/Reference/FSEvents_Ref/Reference/reference.html)
* Linux - [inotify](http://perkamon.alioth.debian.org/online/man7/inotify.7.php)

Symlinks
--------

File operations regarding symlinks are resolved to use the actual name.

E.g. this will trigger a create event for 'foo/test.txt'
> mkdir foo
> ln -s foo bar
> touch bar/test.txt

And (of course), removing the symlink, won't trigger events for the subdirectories or files.


Differences
===========

There are a few notable differences between the platform specific file events libraries.

Windows
-------



OSX
------

Darwin has the best granularity of events and also, it's a complete
journaling system, which allows you to get events from a given time.

In the FSEvent system, you can watch folders (and their sub folders)
by passing in the top folder.  


One thing that FSEvents can do, is pass along a flag which says if the path is a file or a folder.
This might be useful, especially if the the file or folder was removed.


Linux
-----

Unfortunately, the [inotify](http://perkamon.alioth.debian.org/online/man7/inotify.7.php) library
doesn't have the ability to watch subdirectories recursively. So ``fileevents`` has to detect
existing directories and directory creation, and add these to the watch.
 