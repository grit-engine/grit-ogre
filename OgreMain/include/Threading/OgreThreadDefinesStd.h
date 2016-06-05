/*-------------------------------------------------------------------------
This source file is a part of OGRE
(Object-oriented Graphics Rendering Engine)

For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2013 Torus Knot Software Ltd
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE
-------------------------------------------------------------------------*/

#ifndef __OgreThreadDefinesStd_H__
#define __OgreThreadDefinesStd_H__

#include <mutex>
#include <condition_variable>

#define OGRE_AUTO_MUTEX mutable std::recursive_mutex OGRE_AUTO_MUTEX_NAME;
#define OGRE_LOCK_AUTO_MUTEX std::unique_lock<std::recursive_mutex> ogreAutoMutexLock(OGRE_AUTO_MUTEX_NAME);
#define OGRE_MUTEX(name) mutable std::recursive_mutex name;
#define OGRE_STATIC_MUTEX(name) static std::recursive_mutex name;
#define OGRE_STATIC_MUTEX_INSTANCE(name) std::recursive_mutex name;
#define OGRE_LOCK_MUTEX(name) std::unique_lock<std::recursive_mutex> ogrenameLock(name);
#define OGRE_LOCK_MUTEX_NAMED(mutexName, lockName) std::unique_lock<std::recursive_mutex> lockName(mutexName);
// like OGRE_AUTO_MUTEX but mutex held by pointer
#define OGRE_AUTO_SHARED_MUTEX mutable std::recursive_mutex *OGRE_AUTO_MUTEX_NAME;
#define OGRE_LOCK_AUTO_SHARED_MUTEX assert(OGRE_AUTO_MUTEX_NAME); std::unique_lock<std::recursive_mutex> ogreAutoMutexLock(*OGRE_AUTO_MUTEX_NAME);
#define OGRE_NEW_AUTO_SHARED_MUTEX assert(!OGRE_AUTO_MUTEX_NAME); OGRE_AUTO_MUTEX_NAME = new std::recursive_mutex();
#define OGRE_DELETE_AUTO_SHARED_MUTEX assert(OGRE_AUTO_MUTEX_NAME); delete OGRE_AUTO_MUTEX_NAME;
#define OGRE_COPY_AUTO_SHARED_MUTEX(from) assert(!OGRE_AUTO_MUTEX_NAME); OGRE_AUTO_MUTEX_NAME = from;
#define OGRE_SET_AUTO_SHARED_MUTEX_NULL OGRE_AUTO_MUTEX_NAME = 0;
#define OGRE_MUTEX_CONDITIONAL(mutex) if (mutex)
#define OGRE_THREAD_SYNCHRONISER(sync) std::condition_variable_any sync;
#define OGRE_THREAD_WAIT(sync, mutex, lock) sync.wait(lock);
#define OGRE_THREAD_NOTIFY_ONE(sync) sync.notify_one(); 
#define OGRE_THREAD_NOTIFY_ALL(sync) sync.notify_all(); 

// Read-write mutex
namespace Ogre {

    class ReadWriteLock {
        
        OGRE_AUTO_MUTEX;
        OGRE_THREAD_SYNCHRONISER(read)
        OGRE_THREAD_SYNCHRONISER(write)

        unsigned readers, writers, readWaiters, writeWaiters;

        public:

        ReadWriteLock (void)
          : readers(0), writers(0), readWaiters(0), writeWaiters(0)
        { }

        void readLock (void) {
            OGRE_LOCK_AUTO_MUTEX
            if (writers || writeWaiters) {
                readWaiters++;
                do {
                    OGRE_THREAD_WAIT(read, OGRE_AUTO_MUTEX_NAME, ogreAutoMutexLock)
                } while (writers || writeWaiters);
                readWaiters--;
            }
            readers++;
        }

        void readUnlock (void) {
            OGRE_LOCK_AUTO_MUTEX
            readers--;
            if (readers==0 && writeWaiters)
                OGRE_THREAD_NOTIFY_ONE(write)
        }

        void writeLock (void) {
            OGRE_LOCK_AUTO_MUTEX
            if (readers || writers) {
                writeWaiters++;
                do {
                    OGRE_THREAD_WAIT(write, OGRE_AUTO_MUTEX_NAME, ogreAutoMutexLock)
                } while (readers || writers);
                writeWaiters--;
            }
            writers = 1;
        }

        void writeUnlock (void) {
            OGRE_LOCK_AUTO_MUTEX
            writers = 0;
            if (writeWaiters)
                OGRE_THREAD_NOTIFY_ONE(write)
            else if (readWaiters)
                OGRE_THREAD_NOTIFY_ALL(read)
        }

        class ScopedRead {
            ReadWriteLock &lock;
            public:

            ScopedRead (ReadWriteLock &lock)
              : lock(lock)
            {
                lock.readLock();
            }

            ~ScopedRead (void)
            {
                lock.readUnlock();
            }
                
        };

        class ScopedWrite {
            ReadWriteLock &lock;
            public:

            ScopedWrite (ReadWriteLock &lock)
              : lock(lock)
            {
                lock.writeLock();
            }

            ~ScopedWrite (void)
            {
                lock.writeUnlock();
            }
                
        };
    };
}


#define OGRE_RW_MUTEX(name) mutable Ogre::ReadWriteLock name;
#define OGRE_LOCK_RW_MUTEX_READ(name) Ogre::ReadWriteLock::ScopedRead ogrenameLock(name);
#define OGRE_LOCK_RW_MUTEX_WRITE(name) Ogre::ReadWriteLock::ScopedWrite ogrenameLock(name);

// Thread-local pointer
// Grit does not use material scripts , and that is the only usecase for these.
#define OGRE_THREAD_POINTER(T, var) T *var
#define OGRE_THREAD_POINTER_INIT(var) var(NULL)
#define OGRE_THREAD_POINTER_SET(var, expr) var = expr
#define OGRE_THREAD_POINTER_GET(var) var
#define OGRE_THREAD_POINTER_DELETE(var) OGRE_DELETE(var)
// Static Thread-local pointer
#define OGRE_THREAD_POINTER_STATIC_VAR(T, var) static thread_local T var;
#define OGRE_THREAD_POINTER_STATIC_SET(var, expr) var = expr
#define OGRE_THREAD_POINTER_STATIC_GET(var) var
// Thread objects and related functions
#define OGRE_THREAD_TYPE std::thread
#define OGRE_THREAD_CREATE(name, worker) std::thread* name = OGRE_NEW_T(std::thread, MEMCATEGORY_GENERAL)(worker);
#define OGRE_THREAD_DESTROY(name) OGRE_DELETE_T(name, thread, MEMCATEGORY_GENERAL);
#define OGRE_THREAD_HARDWARE_CONCURRENCY std::thread::hardware_concurrency();
#define OGRE_THREAD_CURRENT_ID std::this_thread::get_id()
#define OGRE_THREAD_WORKER_INHERIT
// Utility
#define OGRE_THREAD_SLEEP(ms) std::this_thread::sleep_for(std::chrono::milliseconds(1));


#endif
