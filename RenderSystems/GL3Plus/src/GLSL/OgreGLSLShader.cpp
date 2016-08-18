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
#include "OgreGpuProgram.h"
#include "OgreHighLevelGpuProgramManager.h"
#include "OgreLogManager.h"
#include "OgreRoot.h"
#include "OgreStringConverter.h"

#include "OgreGLSLShader.h"
#include "OgreGLSLShader.h"
#include "OgreGLSLMonolithicProgramManager.h"
#include "OgreGLSLSeparableProgramManager.h"
#include "OgreGL3PlusGLSLPreprocessor.h"
#include "OgreGL3PlusUtil.h"

namespace Ogre {

    String operationTypeToString(RenderOperation::OperationType val);
    RenderOperation::OperationType parseOperationType(const String& val);

    GL3PlusGLSLShader::CmdPreprocessorDefines GL3PlusGLSLShader::msCmdPreprocessorDefines;
    GL3PlusGLSLShader::CmdAttach GL3PlusGLSLShader::msCmdAttach;
    GL3PlusGLSLShader::CmdColumnMajorMatrices GL3PlusGLSLShader::msCmdColumnMajorMatrices;
    GL3PlusGLSLShader::CmdInputOperationType GL3PlusGLSLShader::msInputOperationTypeCmd;
    GL3PlusGLSLShader::CmdOutputOperationType GL3PlusGLSLShader::msOutputOperationTypeCmd;
    GL3PlusGLSLShader::CmdMaxOutputVertices GL3PlusGLSLShader::msMaxOutputVerticesCmd;
    
    GLuint GL3PlusGLSLShader::mShaderCount = 0;

    GL3PlusGLSLShader::GL3PlusGLSLShader(
        ResourceManager* creator,
        const String& name, ResourceHandle handle,
        const String& group, bool isManual, ManualResourceLoader* loader)
        : HighLevelGpuProgram(creator, name, handle, group, isManual, loader)
        , mGLShaderHandle(0)
        , mGLProgramHandle(0)
        , mCompiled(0)
        , mColumnMajorMatrices(true)
    {
        if (createParamDictionary("GL3PlusGLSLShader"))
        {
            setupBaseParamDictionary();
            ParamDictionary* dict = getParamDictionary();

            dict->addParameter(ParameterDef(
                "preprocessor_defines",
                "Preprocessor defines use to compile the program.",
                PT_STRING), &msCmdPreprocessorDefines);
            dict->addParameter(ParameterDef(
                "attach",
                "name of another GLSL program needed by this program",
                PT_STRING), &msCmdAttach);
            dict->addParameter(ParameterDef(
                "column_major_matrices",
                "Whether matrix packing in column-major order.",
                PT_BOOL), &msCmdColumnMajorMatrices);
            dict->addParameter(
                ParameterDef(
                    "input_operation_type",
                    "The input operation type for this geometry program. "
                    "Can be 'point_list', 'line_list', 'line_strip', 'triangle_list', "
                    "'triangle_strip' or 'triangle_fan'",
                    PT_STRING), &msInputOperationTypeCmd);
            dict->addParameter(
                ParameterDef("output_operation_type",
                             "The input operation type for this geometry program. "
                             "Can be 'point_list', 'line_strip' or 'triangle_strip'",
                             PT_STRING), &msOutputOperationTypeCmd);
            dict->addParameter(
                ParameterDef("max_output_vertices",
                             "The maximum number of vertices a single run "
                             "of this geometry program can output",
                             PT_INT), &msMaxOutputVerticesCmd);
        }

        mType = GPT_VERTEX_PROGRAM; // default value, to be corrected after the constructor with GpuProgram::setType()
        mSyntaxCode = "glsl" + StringConverter::toString(Root::getSingleton().getRenderSystem()->getNativeShadingLanguageVersion());

        mLinked = 0;
        // Increase shader counter and use as ID
        mShaderID = ++mShaderCount;        
        
        // Transfer skeletal animation status from parent
        mSkeletalAnimation = isSkeletalAnimationIncluded();
        // There is nothing to load
        mLoadFromFile = false;
    }


    GL3PlusGLSLShader::~GL3PlusGLSLShader()
    {
        // Have to call this here rather than in Resource destructor
        // since calling virtual methods in base destructors causes crash
        if (isLoaded())
        {
            unload();
        }
        else
        {
            unloadHighLevel();
        }
    }


    void GL3PlusGLSLShader::loadFromSource(void)
    {
        // Preprocess the GLSL shader in order to get a clean source
        CPreprocessor cpp;

        // Pass all user-defined macros to preprocessor
        if (!mPreprocessorDefines.empty ())
        {
            String::size_type pos = 0;
            while (pos != String::npos)
            {
                // Find delims
                String::size_type endPos = mPreprocessorDefines.find_first_of(";,=", pos);
                if (endPos != String::npos)
                {
                    String::size_type macro_name_start = pos;
                    size_t macro_name_len = endPos - pos;
                    pos = endPos;

                    // Check definition part
                    if (mPreprocessorDefines[pos] == '=')
                    {
                        // Set up a definition, skip delim
                        ++pos;
                        String::size_type macro_val_start = pos;
                        size_t macro_val_len;

                        endPos = mPreprocessorDefines.find_first_of(";,", pos);
                        if (endPos == String::npos)
                        {
                            macro_val_len = mPreprocessorDefines.size () - pos;
                            pos = endPos;
                        }
                        else
                        {
                            macro_val_len = endPos - pos;
                            pos = endPos+1;
                        }
                        cpp.Define (
                            mPreprocessorDefines.c_str () + macro_name_start, macro_name_len,
                            mPreprocessorDefines.c_str () + macro_val_start, macro_val_len);
                    }
                    else
                    {
                        // No definition part, define as "1"
                        ++pos;
                        cpp.Define (
                            mPreprocessorDefines.c_str () + macro_name_start, macro_name_len, 1);
                    }
                }
                else
                    pos = endPos;
            }
        }

        size_t out_size = 0;
        const char *src = mSource.c_str ();
        size_t src_len = mSource.size ();
        char *out = cpp.Parse (src, src_len, out_size);
        if (!out || !out_size)
            // Failed to preprocess, break out
            OGRE_EXCEPT (Exception::ERR_RENDERINGAPI_ERROR,
                         "Failed to preprocess shader " + mName,
                         __FUNCTION__);

        mSource = String (out, out_size);
        if (out < src || out > src + src_len)
            free (out);
    }


    bool GL3PlusGLSLShader::compile(const bool checkErrors)
    {
        if (mCompiled == 1)
        {
            return true;
        }

        // Create shader object.
        GLenum GLShaderType = getGLShaderType(mType);
        OGRE_CHECK_GL_ERROR(mGLShaderHandle = glCreateShader(GLShaderType));

        //TODO GL 4.3 KHR_debug

        // if (getGLSupport()->checkExtension("GL_KHR_debug") || gl3wIsSupported(4, 3))
        //     glObjectLabel(GL_SHADER, mGLShaderHandle, 0, mName.c_str());

        // if (Root::getSingleton().getRenderSystem()->getCapabilities()->hasCapability(RSC_SEPARATE_SHADER_OBJECTS))
        // {
        //     OGRE_CHECK_GL_ERROR(mGLProgramHandle = glCreateProgram());
        //     if(getGLSupport()->checkExtension("GL_KHR_debug") || gl3wIsSupported(4, 3))
        //         glObjectLabel(GL_PROGRAM, mGLProgramHandle, 0, mName.c_str());
        // }

        // Add boiler plate code and preprocessor extras, then
        // submit shader source to OpenGL.
        if (!mSource.empty())
        {
            // Add standard shader input and output blocks, if missing.
            if (Root::getSingleton().getRenderSystem()->getCapabilities()->hasCapability(RSC_SEPARATE_SHADER_OBJECTS))
            {
                // Assume blocks are missing if gl_Position is missing.
                if (mSource.find("vec4 gl_Position") == String::npos)
                {
                    size_t mainPos = mSource.find("void main");
                    // Only add blocks if shader is not a child
                    // shader, i.e. has a main function.
                    if (mainPos != String::npos)
                    {
                        size_t versionPos = mSource.find("#version");
                        int shaderVersion = StringConverter::parseInt(mSource.substr(versionPos+9, 3));
                        if (shaderVersion >= 150)
                        {
                            size_t belowVersionPos = mSource.find("\n", versionPos) + 1;
                            switch (mType)
                            {
                            case GPT_VERTEX_PROGRAM:
                                mSource.insert(belowVersionPos, "out gl_PerVertex\n{\nvec4 gl_Position;\nfloat gl_PointSize;\nfloat gl_ClipDistance[];\n};\n\n");
                                break;
                            case GPT_GEOMETRY_PROGRAM:
                                mSource.insert(belowVersionPos, "out gl_PerVertex\n{\nvec4 gl_Position;\nfloat gl_PointSize;\nfloat gl_ClipDistance[];\n};\n\n");
                                mSource.insert(belowVersionPos, "in gl_PerVertex\n{\nvec4 gl_Position;\nfloat gl_PointSize;\nfloat gl_ClipDistance[];\n} gl_in[];\n\n");
                                break;
                            case GPT_DOMAIN_PROGRAM:
                                mSource.insert(belowVersionPos, "out gl_PerVertex\n{\nvec4 gl_Position;\nfloat gl_PointSize;\nfloat gl_ClipDistance[];\n};\n\n");
                                mSource.insert(belowVersionPos, "in gl_PerVertex\n{\nvec4 gl_Position;\nfloat gl_PointSize;\nfloat gl_ClipDistance[];\n} gl_in[];\n\n");
                                break;
                            case GPT_HULL_PROGRAM:
                                mSource.insert(belowVersionPos, "out gl_PerVertex\n{\nvec4 gl_Position;\nfloat gl_PointSize;\nfloat gl_ClipDistance[];\n} gl_out[];\n\n");
                                mSource.insert(belowVersionPos, "in gl_PerVertex\n{\nvec4 gl_Position;\nfloat gl_PointSize;\nfloat gl_ClipDistance[];\n} gl_in[];\n\n");
                                break;
                            case GPT_FRAGMENT_PROGRAM:
                            case GPT_COMPUTE_PROGRAM:
                                // Fragment and compute shaders do
                                // not have standard blocks.
                                break;
                            }
                        }
                    }
                }
            }
            // Submit shader source.
            const char *source = mSource.c_str();
            OGRE_CHECK_GL_ERROR(glShaderSource(mGLShaderHandle, 1, &source, NULL));
        }

        OGRE_CHECK_GL_ERROR(glCompileShader(mGLShaderHandle));

        // Check for compile errors
        OGRE_CHECK_GL_ERROR(glGetShaderiv(mGLShaderHandle, GL_COMPILE_STATUS, &mCompiled));
        if (!mCompiled && checkErrors)
        {
            String message = logObjectInfo("GLSL compile log: " + mName, mGLShaderHandle);
            checkAndFixInvalidDefaultPrecisionError(message);
        }

        // Log a message that the shader compiled successfully.
        if (mCompiled && checkErrors)
            logObjectInfo("GLSL compiled: " + mName, mGLShaderHandle);

        if (!mCompiled)
        {
            String shaderType = getShaderTypeLabel(mType);
            StringUtil::toTitleCase(shaderType);
            OGRE_EXCEPT(Exception::ERR_RENDERINGAPI_ERROR,
                        shaderType + " Program " + mName +
                        " failed to compile. See compile log above for details.",
                        "GL3PlusGLSLShader::compile");
        }

        return (mCompiled == 1);
    }


    void GL3PlusGLSLShader::createLowLevelImpl(void)
    {
        // mAssemblerProgram = GpuProgramPtr(OGRE_NEW GL3PlusGLSLShader(this));
        // // Shader params need to be forwarded to low level implementation
        // mAssemblerProgram->setAdjacencyInfoRequired(isAdjacencyInfoRequired());
        // mAssemblerProgram->setComputeGroupDimensions(getComputeGroupDimensions());
    }


    void GL3PlusGLSLShader::unloadImpl()
    {
        // We didn't create mAssemblerProgram through a manager, so override this
        // implementation so that we don't try to remove it from one. Since getCreator()
        // is used, it might target a different matching handle!
        // mAssemblerProgram.setNull();

        unloadHighLevel();
    }


    void GL3PlusGLSLShader::unloadHighLevelImpl(void)
    {
        OGRE_CHECK_GL_ERROR(glDeleteShader(mGLShaderHandle));

        if (Root::getSingleton().getRenderSystem()->getCapabilities()->hasCapability(RSC_SEPARATE_SHADER_OBJECTS) && mGLProgramHandle)
        {
            OGRE_CHECK_GL_ERROR(glDeleteProgram(mGLProgramHandle));
        }

        mGLShaderHandle = 0;
        mGLProgramHandle = 0;
        mCompiled = 0;
    }


    void GL3PlusGLSLShader::populateParameterNames(GpuProgramParametersSharedPtr params)
    {
        getConstantDefinitions();
        params->_setNamedConstants(mConstantDefs);
        // Don't set logical / physical maps here, as we can't access parameters by logical index in GLSL.
    }


    void GL3PlusGLSLShader::buildConstantDefinitions() const
    {
        // We need an accurate list of all the uniforms in the shader, but we
        // can't get at them until we link all the shaders into a program object.

        // Therefore instead parse the source code manually and extract the uniforms.
        createParameterMappingStructures(true);
        if (Root::getSingleton().getRenderSystem()->getCapabilities()->hasCapability(RSC_SEPARATE_SHADER_OBJECTS))
        {
            GL3PlusGLSLSeparableProgramManager::getSingleton().extractUniformsFromGLSL(mSource, *mConstantDefs.get(), mName);
        }
        else
        {
            GL3PlusGLSLMonolithicProgramManager::getSingleton().extractUniformsFromGLSL(mSource, *mConstantDefs.get(), mName);
        }

        // Also parse any attached sources.
        for (GLSLShaderContainer::const_iterator i = mAttachedGLSLShaders.begin();
             i != mAttachedGLSLShaders.end(); ++i)
        {
            GL3PlusGLSLShader* childShader = *i;

            if (Root::getSingleton().getRenderSystem()->getCapabilities()->hasCapability(RSC_SEPARATE_SHADER_OBJECTS))
            {
                GL3PlusGLSLSeparableProgramManager::getSingleton().extractUniformsFromGLSL(childShader->getSource(),
                                                                                    *mConstantDefs.get(), childShader->getName());
            }
            else
            {
                GL3PlusGLSLMonolithicProgramManager::getSingleton().extractUniformsFromGLSL(childShader->getSource(),
                                                                                     *mConstantDefs.get(), childShader->getName());
            }
        }
    }


    inline bool GL3PlusGLSLShader::getPassSurfaceAndLightStates(void) const
    {
        // Scenemanager should pass on light & material state to the rendersystem.
        return true;
    }

    inline bool GL3PlusGLSLShader::getPassTransformStates(void) const
    {
        // Scenemanager should pass on transform state to the rendersystem.
        return true;
    }

    inline bool GL3PlusGLSLShader::getPassFogStates(void) const
    {
        // Scenemanager should pass on fog state to the rendersystem.
        return true;
    }


    String GL3PlusGLSLShader::CmdAttach::doGet(const void *target) const
    {
        return (static_cast<const GL3PlusGLSLShader*>(target))->getAttachedShaderNames();
    }
    void GL3PlusGLSLShader::CmdAttach::doSet(void *target, const String& shaderNames)
    {
        // Get all the shader program names: there could be more than one.
        StringVector vecShaderNames = StringUtil::split(shaderNames, " \t", 0);

        size_t programNameCount = vecShaderNames.size();
        for ( size_t i = 0; i < programNameCount; ++i)
        {
            static_cast<GL3PlusGLSLShader*>(target)->attachChildShader(vecShaderNames[i]);
        }
    }


    String GL3PlusGLSLShader::CmdColumnMajorMatrices::doGet(const void *target) const
    {
        return StringConverter::toString(static_cast<const GL3PlusGLSLShader*>(target)->getColumnMajorMatrices());
    }
    void GL3PlusGLSLShader::CmdColumnMajorMatrices::doSet(void *target, const String& val)
    {
        static_cast<GL3PlusGLSLShader*>(target)->setColumnMajorMatrices(StringConverter::parseBool(val));
    }


    String GL3PlusGLSLShader::CmdPreprocessorDefines::doGet(const void *target) const
    {
        return static_cast<const GL3PlusGLSLShader*>(target)->getPreprocessorDefines();
    }
    void GL3PlusGLSLShader::CmdPreprocessorDefines::doSet(void *target, const String& val)
    {
        static_cast<GL3PlusGLSLShader*>(target)->setPreprocessorDefines(val);
    }


    String GL3PlusGLSLShader::CmdInputOperationType::doGet(const void* target) const
    {
        const GL3PlusGLSLShader* t = static_cast<const GL3PlusGLSLShader*>(target);
        return operationTypeToString(t->getInputOperationType());
    }
    void GL3PlusGLSLShader::CmdInputOperationType::doSet(void* target, const String& val)
    {
        GL3PlusGLSLShader* t = static_cast<GL3PlusGLSLShader*>(target);
        t->setInputOperationType(parseOperationType(val));
    }


    String GL3PlusGLSLShader::CmdOutputOperationType::doGet(const void* target) const
    {
        const GL3PlusGLSLShader* t = static_cast<const GL3PlusGLSLShader*>(target);
        return operationTypeToString(t->getOutputOperationType());
    }
    void GL3PlusGLSLShader::CmdOutputOperationType::doSet(void* target, const String& val)
    {
        GL3PlusGLSLShader* t = static_cast<GL3PlusGLSLShader*>(target);
        t->setOutputOperationType(parseOperationType(val));
    }


    String GL3PlusGLSLShader::CmdMaxOutputVertices::doGet(const void* target) const
    {
        const GL3PlusGLSLShader* t = static_cast<const GL3PlusGLSLShader*>(target);
        return StringConverter::toString(t->getMaxOutputVertices());
    }
    void GL3PlusGLSLShader::CmdMaxOutputVertices::doSet(void* target, const String& val)
    {
        GL3PlusGLSLShader* t = static_cast<GL3PlusGLSLShader*>(target);
        t->setMaxOutputVertices(StringConverter::parseInt(val));
    }


    void GL3PlusGLSLShader::attachChildShader(const String& name)
    {
        // Is the name valid and already loaded?
        // Check with the high level program manager to see if it was loaded.
        HighLevelGpuProgramPtr hlProgram = HighLevelGpuProgramManager::getSingleton().getByName(name);
        if (!hlProgram.isNull())
        {
            if (hlProgram->getSyntaxCode() == "glsl")
            {
                // Make sure attached program source gets loaded and compiled
                // don't need a low level implementation for attached shader objects
                // loadHighLevelImpl will only load the source and compile once
                // so don't worry about calling it several times.
                GL3PlusGLSLShader* childShader = static_cast<GL3PlusGLSLShader*>(hlProgram.getPointer());
                // Load the source and attach the child shader.
                childShader->loadHighLevelImpl();
                // Add to the container.
                mAttachedGLSLShaders.push_back(childShader);
                mAttachedShaderNames += name + " ";
            }
        }
    }


    void GL3PlusGLSLShader::attachToProgramObject(const GLuint programObject)
    {
        // attach child objects
        GLSLShaderContainerIterator childProgramCurrent = mAttachedGLSLShaders.begin();
        GLSLShaderContainerIterator childProgramEnd = mAttachedGLSLShaders.end();

        for (; childProgramCurrent != childProgramEnd; ++childProgramCurrent)
        {
            GL3PlusGLSLShader* childShader = *childProgramCurrent;
            childShader->compile(true);
            childShader->attachToProgramObject(programObject);
        }
        OGRE_CHECK_GL_ERROR(glAttachShader(programObject, mGLShaderHandle));
    }


    void GL3PlusGLSLShader::detachFromProgramObject(const GLuint programObject)
    {
        OGRE_CHECK_GL_ERROR(glDetachShader(programObject, mGLShaderHandle));
        logObjectInfo( "Error detaching " + mName + " shader object from GLSL Program Object", programObject);
        // attach child objects
        GLSLShaderContainerIterator childprogramcurrent = mAttachedGLSLShaders.begin();
        GLSLShaderContainerIterator childprogramend = mAttachedGLSLShaders.end();

        while (childprogramcurrent != childprogramend)
        {
            GL3PlusGLSLShader* childShader = *childprogramcurrent;
            childShader->detachFromProgramObject(programObject);
            ++childprogramcurrent;
        }
    }


    const String& GL3PlusGLSLShader::getLanguage(void) const
    {
        static const String language = "glsl";

        return language;
    }


    Ogre::GpuProgramParametersSharedPtr GL3PlusGLSLShader::createParameters(void)
    {
        GpuProgramParametersSharedPtr params = HighLevelGpuProgram::createParameters();
        return params;
    }


    void GL3PlusGLSLShader::checkAndFixInvalidDefaultPrecisionError(String &message)
    {
        String precisionQualifierErrorString = ": 'Default Precision Qualifier' :  invalid type Type for default precision qualifier can be only float or int";
        vector< String >::type linesOfSource = StringUtil::split(mSource, "\n");
        if (message.find(precisionQualifierErrorString) != String::npos)
        {
            LogManager::getSingleton().logMessage("Fixing invalid type Type for default precision qualifier by deleting bad lines the re-compiling", LML_CRITICAL);

            // remove relevant lines from source
            vector< String >::type errors = StringUtil::split(message, "\n");

            // going from the end so when we delete a line the numbers of the lines before will not change
            for (int i = (int)errors.size() - 1 ; i != -1 ; i--)
            {
                String & curError = errors[i];
                size_t foundPos = curError.find(precisionQualifierErrorString);
                if (foundPos != String::npos)
                {
                    String lineNumber = curError.substr(0, foundPos);
                    size_t posOfStartOfNumber = lineNumber.find_last_of(':');
                    if (posOfStartOfNumber != String::npos)
                    {
                        lineNumber = lineNumber.substr(posOfStartOfNumber +     1, lineNumber.size() - (posOfStartOfNumber + 1));
                        if (StringConverter::isNumber(lineNumber))
                        {
                            int iLineNumber = StringConverter::parseInt(lineNumber);
                            linesOfSource.erase(linesOfSource.begin() + iLineNumber - 1);
                        }
                    }
                }
            }
            // rebuild source
            StringStream newSource;
            for (size_t i = 0; i < linesOfSource.size()  ; i++)
            {
                newSource << linesOfSource[i] << "\n";
            }
            mSource = newSource.str();

            const char *source = mSource.c_str();
            OGRE_CHECK_GL_ERROR(glShaderSource(mGLShaderHandle, 1, &source, NULL));
            // Check for load errors
            if (compile(true))
            {
                LogManager::getSingleton().logMessage("The removing of the lines fixed the invalid type Type for default precision qualifier error.", LML_CRITICAL);
            }
            else
            {
                LogManager::getSingleton().logMessage("The removing of the lines didn't help.", LML_CRITICAL);
            }
        }
    }


    RenderOperation::OperationType parseOperationType(const String& val)
    {
        if (val == "point_list")
        {
            return RenderOperation::OT_POINT_LIST;
        }
        else if (val == "line_list")
        {
            return RenderOperation::OT_LINE_LIST;
        }
        else if (val == "line_strip")
        {
            return RenderOperation::OT_LINE_STRIP;
        }
        else if (val == "triangle_strip")
        {
            return RenderOperation::OT_TRIANGLE_STRIP;
        }
        else if (val == "triangle_fan")
        {
            return RenderOperation::OT_TRIANGLE_FAN;
        }
        else
        {
            // Triangle list is the default fallback. Keep it this way?
            return RenderOperation::OT_TRIANGLE_LIST;
        }
    }


    String operationTypeToString(RenderOperation::OperationType val)
    {
        switch (val)
        {
        case RenderOperation::OT_POINT_LIST:
            return "point_list";
            break;
        case RenderOperation::OT_LINE_LIST:
            return "line_list";
            break;
        case RenderOperation::OT_LINE_STRIP:
            return "line_strip";
            break;
        case RenderOperation::OT_TRIANGLE_STRIP:
            return "triangle_strip";
            break;
        case RenderOperation::OT_TRIANGLE_FAN:
            return "triangle_fan";
            break;
        case RenderOperation::OT_TRIANGLE_LIST:
        default:
            return "triangle_list";
            break;
        }
    }


    GLenum GL3PlusGLSLShader::getGLShaderType(GpuProgramType programType)
    {
        //TODO Convert to map, or is speed different negligible?
        switch (programType)
        {
        case GPT_VERTEX_PROGRAM:
            return GL_VERTEX_SHADER;
        case GPT_HULL_PROGRAM:
            return GL_TESS_CONTROL_SHADER;
        case GPT_DOMAIN_PROGRAM:
            return GL_TESS_EVALUATION_SHADER;
        case GPT_GEOMETRY_PROGRAM:
            return GL_GEOMETRY_SHADER;
        case GPT_FRAGMENT_PROGRAM:
            return GL_FRAGMENT_SHADER;
        case GPT_COMPUTE_PROGRAM:
            return GL_COMPUTE_SHADER;
        }

        //TODO add warning or error
        return 0;
    }

    String GL3PlusGLSLShader::getShaderTypeLabel(GpuProgramType programType)
    {
        switch (programType)
        {
        case GPT_VERTEX_PROGRAM:
            return "vertex";
            break;
        case GPT_DOMAIN_PROGRAM:
            return "tessellation evaluation";
            break;
        case GPT_HULL_PROGRAM:
            return "tessellation control";
            break;
        case GPT_GEOMETRY_PROGRAM:
            return "geometry";
            break;
        case GPT_FRAGMENT_PROGRAM:
            return "fragment";
            break;
        case GPT_COMPUTE_PROGRAM:
            return "compute";
            break;
        }

        //TODO add warning or error
        return 0;
    }


    GLuint GL3PlusGLSLShader::getGLProgramHandle() {
        //TODO This should be removed and the compile() function
        // should use glCreateShaderProgramv
        // for separable programs which includes creating a program.
        if (mGLProgramHandle == 0)
        {
            OGRE_CHECK_GL_ERROR(mGLProgramHandle = glCreateProgram());
            if (mGLProgramHandle == 0)
            {
                //TODO error handling
            }
        }
        return mGLProgramHandle;
    }


    void GL3PlusGLSLShader::bind(void)
    {
        if (Root::getSingleton().getRenderSystem()->getCapabilities()->hasCapability(RSC_SEPARATE_SHADER_OBJECTS))
        {
            // Tell the Program Pipeline Manager what pipeline is to become active.
            switch (mType)
            {
            case GPT_VERTEX_PROGRAM:
                GL3PlusGLSLSeparableProgramManager::getSingleton().setActiveVertexShader(this);
                break;
            case GPT_FRAGMENT_PROGRAM:
                GL3PlusGLSLSeparableProgramManager::getSingleton().setActiveFragmentShader(this);
                break;
            case GPT_GEOMETRY_PROGRAM:
                GL3PlusGLSLSeparableProgramManager::getSingleton().setActiveGeometryShader(this);
                break;
            case GPT_HULL_PROGRAM:
                GL3PlusGLSLSeparableProgramManager::getSingleton().setActiveTessHullShader(this);
                break;
            case GPT_DOMAIN_PROGRAM:
                GL3PlusGLSLSeparableProgramManager::getSingleton().setActiveTessDomainShader(this);
                break;
            case GPT_COMPUTE_PROGRAM:
                GL3PlusGLSLSeparableProgramManager::getSingleton().setActiveComputeShader(this);
            default:
                break;
            }
        }
        else
        {
            // Tell the Link Program Manager what shader is to become active.
            switch (mType)
            {
            case GPT_VERTEX_PROGRAM:
                GL3PlusGLSLMonolithicProgramManager::getSingleton().setActiveVertexShader(this);
                break;
            case GPT_FRAGMENT_PROGRAM:
                GL3PlusGLSLMonolithicProgramManager::getSingleton().setActiveFragmentShader(this);
                break;
            case GPT_GEOMETRY_PROGRAM:
                GL3PlusGLSLMonolithicProgramManager::getSingleton().setActiveGeometryShader(this);
                break;
            case GPT_HULL_PROGRAM:
                GL3PlusGLSLMonolithicProgramManager::getSingleton().setActiveHullShader(this);
                break;
            case GPT_DOMAIN_PROGRAM:
                GL3PlusGLSLMonolithicProgramManager::getSingleton().setActiveDomainShader(this);
                break;
            case GPT_COMPUTE_PROGRAM:
                GL3PlusGLSLMonolithicProgramManager::getSingleton().setActiveComputeShader(this);
            default:
                break;
            }
        }
    }

    void GL3PlusGLSLShader::unbind(void)
    {
        if(Root::getSingleton().getRenderSystem()->getCapabilities()->hasCapability(RSC_SEPARATE_SHADER_OBJECTS))
        {
            // Tell the Program Pipeline Manager what pipeline is to become inactive.
            if (mType == GPT_VERTEX_PROGRAM)
            {
                GL3PlusGLSLSeparableProgramManager::getSingleton().setActiveVertexShader(NULL);
            }
            else if (mType == GPT_GEOMETRY_PROGRAM)
            {
                GL3PlusGLSLSeparableProgramManager::getSingleton().setActiveGeometryShader(NULL);
            }
            else if (mType == GPT_HULL_PROGRAM)
            {
                GL3PlusGLSLSeparableProgramManager::getSingleton().setActiveTessHullShader(NULL);
            }
            else if (mType == GPT_DOMAIN_PROGRAM)
            {
                GL3PlusGLSLSeparableProgramManager::getSingleton().setActiveTessDomainShader(NULL);
            }
            else if (mType == GPT_COMPUTE_PROGRAM)
            {
                GL3PlusGLSLSeparableProgramManager::getSingleton().setActiveComputeShader(NULL);
            }
            else // It's a fragment shader
            {
                GL3PlusGLSLSeparableProgramManager::getSingleton().setActiveFragmentShader(NULL);
            }
        }
        else
        {
            // Tell the Link Program Manager what shader is to become inactive.
            if (mType == GPT_VERTEX_PROGRAM)
            {
                GL3PlusGLSLMonolithicProgramManager::getSingleton().setActiveVertexShader(NULL);
            }
            else if (mType == GPT_GEOMETRY_PROGRAM)
            {
                GL3PlusGLSLMonolithicProgramManager::getSingleton().setActiveGeometryShader(NULL);
            }
            else if (mType == GPT_HULL_PROGRAM)
            {
                GL3PlusGLSLMonolithicProgramManager::getSingleton().setActiveHullShader(NULL);
            }
            else if (mType == GPT_DOMAIN_PROGRAM)
            {
                GL3PlusGLSLMonolithicProgramManager::getSingleton().setActiveDomainShader(NULL);
            }
            else if (mType == GPT_COMPUTE_PROGRAM)
            {
                GL3PlusGLSLMonolithicProgramManager::getSingleton().setActiveComputeShader(NULL);
            }
            else // It's a fragment shader
            {
                GL3PlusGLSLMonolithicProgramManager::getSingleton().setActiveFragmentShader(NULL);
            }
        }
    }


    void GL3PlusGLSLShader::bindParameters(GpuProgramParametersSharedPtr params, uint16 mask)
    {
        // Link can throw exceptions, ignore them at this point.
        try
        {
            if (Root::getSingleton().getRenderSystem()->getCapabilities()->hasCapability(RSC_SEPARATE_SHADER_OBJECTS))
            {
                // Activate the program pipeline object.
                GLSLSeparableProgram* separableProgram = GL3PlusGLSLSeparableProgramManager::getSingleton().getCurrentSeparableProgram();
                // Pass on parameters from params to program object uniforms.
                separableProgram->updateUniforms(params, mask, mType);
                separableProgram->updateAtomicCounters(params, mask, mType);
            }
            else
            {
                // Activate the link program object.
                GL3PlusGLSLMonolithicProgram* monolithicProgram = GL3PlusGLSLMonolithicProgramManager::getSingleton().getActiveMonolithicProgram();
                // Pass on parameters from params to program object uniforms.
                monolithicProgram->updateUniforms(params, mask, mType);
                //TODO add atomic counter support
                //monolithicProgram->updateAtomicCounters(params, mask, mType);
            }
        }
        catch (Exception&) {}
    }


    void GL3PlusGLSLShader::bindPassIterationParameters(GpuProgramParametersSharedPtr params)
    {
        if (Root::getSingleton().getRenderSystem()->getCapabilities()->hasCapability(RSC_SEPARATE_SHADER_OBJECTS))
        {
            // Activate the program pipeline object.
            GLSLSeparableProgram* separableProgram = GL3PlusGLSLSeparableProgramManager::getSingleton().getCurrentSeparableProgram();
            // Pass on parameters from params to program object uniforms.
            separableProgram->updatePassIterationUniforms(params);
        }
        else
        {
            // Activate the link program object.
            GL3PlusGLSLMonolithicProgram* monolithicProgram = GL3PlusGLSLMonolithicProgramManager::getSingleton().getActiveMonolithicProgram();
            // Pass on parameters from params to program object uniforms.
            monolithicProgram->updatePassIterationUniforms(params);
        }
    }


    void GL3PlusGLSLShader::bindSharedParameters(GpuProgramParametersSharedPtr params, uint16 mask)
    {
        // Link can throw exceptions, ignore them at this point.
        try
        {
            if (Root::getSingleton().getRenderSystem()->getCapabilities()->hasCapability(RSC_SEPARATE_SHADER_OBJECTS))
            {
                // Activate the program pipeline object.
                GLSLSeparableProgram* separableProgram = GL3PlusGLSLSeparableProgramManager::getSingleton().getCurrentSeparableProgram();
                // Pass on parameters from params to program object uniforms.
                separableProgram->updateUniformBlocks(params, mask, mType);
                // separableProgram->updateShaderStorageBlock(params, mask, mType);
            }
            else
            {
                // Activate the link program object.
                GL3PlusGLSLMonolithicProgram* monolithicProgram = GL3PlusGLSLMonolithicProgramManager::getSingleton().getActiveMonolithicProgram();
                // Pass on parameters from params to program object uniforms.
                monolithicProgram->updateUniformBlocks(params, mask, mType);
            }
        }
        catch (Exception&) {}
    }


    size_t GL3PlusGLSLShader::calculateSize(void) const
    {
        size_t memSize = 0;

        // Delegate names.
        memSize += sizeof(GLuint);
        memSize += sizeof(GLenum);
        memSize += GpuProgram::calculateSize();

        return memSize;
    }

}
