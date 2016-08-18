/*
  -----------------------------------------------------------------------------
  This source file is part of OGRE
  (Object-oriented Graphics Rendering Engine)
  For the latest info, see http://www.ogre3d.org/

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
#ifndef __GLSLMonolithicProgramManager_H__
#define __GLSLMonolithicProgramManager_H__

#include "OgreGL3PlusPrerequisites.h"
#include "OgreSingleton.h"

#include "OgreGL3PlusGLSLExtSupport.h"
#include "OgreGLSLMonolithicProgram.h"
#include "OgreGLSLProgramManager.h"

namespace Ogre {

    /** Ogre assumes that there are separate vertex and fragment
        programs to deal with but GLSL has one program object that
        represents the active vertex and fragment shader objects
        during a rendering state.  GLSL Vertex and fragment shader
        objects are compiled separately and then attached to a program
        object and then the program object is linked.  Since Ogre can
        only handle one vertex program and one fragment program being
        active in a pass, the GLSL Link Program Manager does the same.
        The GLSL Link program manager acts as a state machine and
        activates a program object based on the active vertex and
        fragment program.  Previously created program objects are
        stored along with a unique key in a hash_map for quick
        retrieval the next time the program object is required.
    */

    class _OgreGL3PlusExport GL3PlusGLSLMonolithicProgramManager : public GL3PlusGLSLProgramManager, public Singleton<GL3PlusGLSLMonolithicProgramManager>
    {

    private:

        typedef map<uint32, GL3PlusGLSLMonolithicProgram*>::type MonolithicProgramMap;
        typedef MonolithicProgramMap::iterator MonolithicProgramIterator;

        /// container holding previously created program objects
        MonolithicProgramMap mMonolithicPrograms;

        /// active objects defining the active rendering gpu state
        GL3PlusGLSLMonolithicProgram* mActiveMonolithicProgram;

        typedef map<String, GLenum>::type StringToEnumMap;
        StringToEnumMap mTypeEnumMap;

    public:

        GL3PlusGLSLMonolithicProgramManager(const GL3PlusSupport& support);

        ~GL3PlusGLSLMonolithicProgramManager(void);

        /** Get the program object that links the two active shader
            objects together if a program object was not already
            created and linked a new one is created and linked
        */
        GL3PlusGLSLMonolithicProgram* getActiveMonolithicProgram(void);

        /** Set the active vertex shader for the next rendering state.
            The active program object will be cleared.  Normally
            called from the GL3PlusGLSLShader::bindProgram and
            unbindProgram methods
        */
        void setActiveVertexShader(GL3PlusGLSLShader* vertexGpuProgram);
        /** Set the active hull(control) shader for the next rendering
            state.  The active program object will be cleared.
            Normally called from the GL3PlusGLSLShader::bindProgram and
            unbindProgram methods
        */
        void setActiveHullShader(GL3PlusGLSLShader* hullGpuProgram);
        /** Set the active domain(evaluation) shader for the next
            rendering state.  The active program object will be
            cleared.  Normally called from the
            GL3PlusGLSLShader::bindProgram and unbindProgram methods
        */
        void setActiveDomainShader(GL3PlusGLSLShader* domainGpuProgram);
        /** Set the active geometry shader for the next rendering
            state.  The active program object will be cleared.
            Normally called from the GL3PlusGLSLShader::bindProgram and
            unbindProgram methods
        */
        void setActiveGeometryShader(GL3PlusGLSLShader* geometryGpuProgram);
        /** Set the active fragment shader for the next rendering
            state.  The active program object will be cleared.
            Normally called from the GL3PlusGLSLShader::bindProgram and
            unbindProgram methods
        */
        void setActiveFragmentShader(GL3PlusGLSLShader* fragmentGpuProgram);
        /** Set the active compute shader for the next rendering
            state.  The active program object will be cleared.
            Normally called from the GL3PlusGLSLShader::bindProgram and
            unbindProgram methods
        */
        void setActiveComputeShader(GL3PlusGLSLShader* computeGpuProgram);

        static GL3PlusGLSLMonolithicProgramManager& getSingleton(void);
        static GL3PlusGLSLMonolithicProgramManager* getSingletonPtr(void);
    };

}

#endif // __GLSLMonolithicProgramManager_H__
