#! /usr/bin/env python
# encoding: utf-8

import sys, os
from waflib import Options

FRAMEWORKS=['Foundation', 'Cocoa']

def options(opt):
    opt.load('compiler_cxx')
    opt.add_option('--debug', action='store_false', default=True, help='enable debugging')

def configure(conf):
    if sys.platform in ('linux2', 'darwin'):
        conf.env.CC = 'clang'
        conf.env.CXX = 'clang++'
    elif sys.platform == 'win32':
        conf.env['MSVC_TARGETS'] = ['x86_amd64', 'x64']
        pass
    conf.load('compiler_cxx')
    
    if sys.platform in ('linux2', 'darwin'):
        conf.env.append_unique('CCFLAGS', '-g -Weverything -pedantic'.split())
        conf.env.append_unique('CXXFLAGS', '-std=c++11 -stdlib=libc++ -Wno-c++98-compat -Wno-c++98-compat-pedantic -Weverything -pedantic'.split())
        
        conf.env.append_unique('CCFLAGS', '-m64')
    
        if Options.options.debug:
            conf.env.CCFLAGS.extend(['-O0'])
        else:
            conf.env.CCFLAGS.extend(['-O3'])
          
    elif sys.platform in ('win32',):
        conf.env.append_unique('CXXFLAGS', '/EHsc'.split())
        conf.check_lib_msvc('shlwapi')
            
    if sys.platform == 'darwin':
        for f in FRAMEWORKS:
            conf.env['FRAMEWORK_%s' % f] = f

def build(bld):
    libs=[]
    if sys.platform == 'linux2':
        source = ['source/fileevents_linux.cpp']
    elif sys.platform == 'darwin':
        source = ['source/fileevents_darwin.cpp']
        libs += FRAMEWORKS
    elif sys.platform == 'win32':
        source = ['source/fileevents_windows.cpp']
        libs += ['SHLWAPI']
    
    source.append('source/fileevents.cpp')
    
    bld(features        = 'cxx cxxstlib',
        source          = source,
        includes        ='include source',
        export_includes = 'include',
        use             = libs,
        target          = 'fileevents')
    
            
    bld(features        = 'cxx cxxprogram',
        source          = 'source/filewatcher.cpp',
        includes        ='source',
        use             = libs + ['fileevents'],
        target          = 'filewatcher')
    
    
    bld(features        = 'cxx cxxprogram',
        source          = 'tests/test.cpp',
        includes        = 'source tests',
        use             = libs + ['fileevents', 'c'],
        target          = 'test')

    """
    bld(features        = 'cxx cxxprogram',
        source          = 'source/inotify.cpp',
        includes        = 'source',
        use             = ['inotify'],
        target          = 'inotifytest')"""
    
def test(ctx):
    builddir = os.path.abspath('build')

    filedir = os.path.join(builddir, 'files') 
    if not os.path.exists(filedir):
        os.makedirs(filedir)
        
    result = ctx.exec_command(os.path.abspath('build/test'), cwd=filedir)
    
    ctx.exec_command('rm *.txt', cwd=filedir)
    
    if result:
        ctx.fatal("tests failed")
    
    