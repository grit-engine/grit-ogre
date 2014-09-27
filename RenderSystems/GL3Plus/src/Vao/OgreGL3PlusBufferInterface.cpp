/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org

Copyright (c) 2000-2014 Torus Knot Software Ltd

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
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "Vao/OgreGL3PlusBufferInterface.h"
#include "Vao/OgreGL3PlusVaoManager.h"
#include "Vao/OgreGL3PlusStagingBuffer.h"

namespace Ogre
{
    GL3PlusBufferInterface::GL3PlusBufferInterface( size_t vboPoolIdx, GLenum target, GLuint vboName ) :
        mVboPoolIdx( vboPoolIdx ),
        mTarget( target ),
        mVboName( vboName ),
        mMappedPtr( 0 )
    {
    }
    //-----------------------------------------------------------------------------------
    GL3PlusBufferInterface::~GL3PlusBufferInterface()
    {
    }
    //-----------------------------------------------------------------------------------
    void GL3PlusBufferInterface::_firstUpload( void *data, size_t elementStart, size_t elementCount )
    {
        //In OpenGL; immutable buffers are a charade. They're mostly there to satisfy D3D11's needs.
        //However we emulate the behavior and trying to upload to an immutable buffer will result
        //in an exception or an assert, thus we temporarily change the type.
        BufferType originalBufferType = mBuffer->mBufferType;
        if( mBuffer->mBufferType == BT_IMMUTABLE )
            mBuffer->mBufferType = BT_DEFAULT;

        upload( data, elementStart, elementCount );

        mBuffer->mBufferType = originalBufferType;
    }
    //-----------------------------------------------------------------------------------
    DECL_MALLOC void* GL3PlusBufferInterface::map( size_t elementStart, size_t elementCount,
                                                   MappingState prevMappingState, bool bAdvanceFrame )
    {
        size_t bytesPerElement = mBuffer->mBytesPerElement;

        GL3PlusVaoManager *vaoManager = static_cast<GL3PlusVaoManager*>( mBuffer->mVaoManager );
        bool canPersistentMap = vaoManager->supportsArbBufferStorage();

        vaoManager->waitForTailFrameToFinish();

        size_t dynamicCurrentFrame = advanceFrame( bAdvanceFrame );

        if( prevMappingState == MS_UNMAPPED || !canPersistentMap )
        {
            GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
                                GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_FLUSH_EXPLICIT_BIT;

            //Non-persistent buffers just map the small region they'll need.
            size_t offset = mBuffer->mInternalBufferStart + elementStart +
                            mBuffer->mNumElements * dynamicCurrentFrame;
            size_t length = elementCount;

            if( mBuffer->mMappingState >= MS_PERSISTENT_INCOHERENT && canPersistentMap )
            {
                //Persistent buffers map the *whole* assigned buffer,
                //we later care for the offsets and lengths
                offset = mBuffer->mInternalBufferStart;
                length = mBuffer->mNumElements * vaoManager->getDynamicBufferMultiplier();

                flags |= GL_MAP_PERSISTENT_BIT;

                if( mBuffer->mMappingState == MS_PERSISTENT_COHERENT )
                    flags |= GL_MAP_COHERENT_BIT;
            }

            mBuffer->mMappingStart = offset;
            mBuffer->mMappingCount = length;

            glBindBuffer( mTarget, mVboName );
            OCGE(
                mMappedPtr = glMapBufferRange( mTarget,
                                               offset * bytesPerElement,
                                               length * bytesPerElement,
                                               flags ) );
        }

        //For regular maps, mLastMappingStart is 0. So that we can later flush correctly.
        mBuffer->mLastMappingStart = 0;
        mBuffer->mLastMappingCount = elementCount;

        char *retVal = (char*)mMappedPtr;

        if( mBuffer->mMappingState >= MS_PERSISTENT_INCOHERENT && canPersistentMap )
        {
            //For persistent maps, we've mapped the whole 3x size of the buffer. mLastMappingStart
            //points to the right offset so that we can later flush correctly.
            size_t lastMappingStart = elementStart + mBuffer->mNumElements * dynamicCurrentFrame;
            mBuffer->mLastMappingStart = lastMappingStart;
            retVal += lastMappingStart * bytesPerElement;
        }

        return retVal;
    }
    //-----------------------------------------------------------------------------------
    void GL3PlusBufferInterface::unmap( UnmapOptions unmapOption,
                                        size_t flushStartElem, size_t flushSizeElem )
    {
        assert( flushStartElem < mBuffer->mLastMappingCount &&
                "Flush starts after the end of the mapped region!" );
        assert( flushStartElem + flushSizeElem <= mBuffer->mLastMappingCount &&
                "Flush region out of bounds!" );

        bool canPersistentMap = static_cast<GL3PlusVaoManager*>( mBuffer->mVaoManager )->
                                                                supportsArbBufferStorage();

        if( mBuffer->mMappingState <= MS_PERSISTENT_INCOHERENT ||
            unmapOption == UO_UNMAP_ALL || !canPersistentMap )
        {
            if( !flushSizeElem )
                flushSizeElem = mBuffer->mLastMappingCount - flushStartElem;

            OCGE( glBindBuffer( mTarget, mVboName ) );
            OCGE( glFlushMappedBufferRange( mTarget,
                                             mBuffer->mLastMappingStart + flushStartElem,
                                             flushSizeElem * mBuffer->mBytesPerElement ) );

            if( unmapOption == UO_UNMAP_ALL || !canPersistentMap || mBuffer->mMappingState == MS_MAPPED )
            {
                OCGE( glUnmapBuffer( mTarget ) );
                mMappedPtr = 0;
            }
        }
    }
    //-----------------------------------------------------------------------------------
    void GL3PlusBufferInterface::advanceFrame(void)
    {
        advanceFrame( true );
    }
    //-----------------------------------------------------------------------------------
    size_t GL3PlusBufferInterface::advanceFrame( bool bAdvanceFrame )
    {
        GL3PlusVaoManager *vaoManager = static_cast<GL3PlusVaoManager*>( mBuffer->mVaoManager );
        size_t dynamicCurrentFrame = mBuffer->mFinalBufferStart - mBuffer->mInternalBufferStart;
        dynamicCurrentFrame /= mBuffer->mNumElements;

        if( bAdvanceFrame )
            dynamicCurrentFrame = (dynamicCurrentFrame + 1) % vaoManager->getDynamicBufferMultiplier();

        mBuffer->mFinalBufferStart = mBuffer->mInternalBufferStart +
                                        dynamicCurrentFrame * mBuffer->mNumElements;

        return dynamicCurrentFrame;
    }
}
