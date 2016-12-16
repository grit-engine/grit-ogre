/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2017 Torus Knot Software Ltd

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

#include "OgreStableHeaders.h"

#include "OgreForwardPlusBase.h"
#include "OgreSceneManager.h"

#include "Vao/OgreVaoManager.h"
#include "Vao/OgreTexBufferPacked.h"

#include "OgreHlms.h"

namespace Ogre
{
    //Six variables * 4 (padded vec3) * 4 (bytes) * numLights
    const size_t ForwardPlusBase::NumBytesPerLight = 6 * 4 * 4;

    ForwardPlusBase::ForwardPlusBase( SceneManager *sceneManager ) :
        mVaoManager( 0 ),
        mSceneManager( sceneManager ),
        mDebugMode( false ),
        mFadeAttenuationRange( true )
    {
    }
    //-----------------------------------------------------------------------------------
    ForwardPlusBase::~ForwardPlusBase()
    {
        CachedGridVec::iterator itor = mCachedGrid.begin();
        CachedGridVec::iterator end  = mCachedGrid.end();

        while( itor != end )
        {
            if( itor->gridBuffer )
            {
                if( itor->gridBuffer->getMappingState() != MS_UNMAPPED )
                    itor->gridBuffer->unmap( UO_UNMAP_ALL );
                mVaoManager->destroyTexBuffer( itor->gridBuffer );
                itor->gridBuffer = 0;
            }

            if( itor->globalLightListBuffer )
            {
                if( itor->globalLightListBuffer->getMappingState() != MS_UNMAPPED )
                    itor->globalLightListBuffer->unmap( UO_UNMAP_ALL );
                mVaoManager->destroyTexBuffer( itor->globalLightListBuffer );
                itor->globalLightListBuffer = 0;
            }

            ++itor;
        }
    }
    //-----------------------------------------------------------------------------------
    void ForwardPlusBase::_changeRenderSystem( RenderSystem *newRs )
    {
        CachedGridVec::iterator itor = mCachedGrid.begin();
        CachedGridVec::iterator end  = mCachedGrid.end();

        while( itor != end )
        {
            if( itor->gridBuffer )
            {
                if( itor->gridBuffer->getMappingState() != MS_UNMAPPED )
                    itor->gridBuffer->unmap( UO_UNMAP_ALL );
                mVaoManager->destroyTexBuffer( itor->gridBuffer );
                itor->gridBuffer = 0;
            }

            if( itor->globalLightListBuffer )
            {
                if( itor->globalLightListBuffer->getMappingState() != MS_UNMAPPED )
                    itor->globalLightListBuffer->unmap( UO_UNMAP_ALL );
                mVaoManager->destroyTexBuffer( itor->globalLightListBuffer );
                itor->globalLightListBuffer = 0;
            }

            ++itor;
        }

        mVaoManager = 0;

        if( newRs )
        {
            mVaoManager = newRs->getVaoManager();
        }
    }
    //-----------------------------------------------------------------------------------
    void ForwardPlusBase::fillGlobalLightListBuffer( Camera *camera,
                                                     TexBufferPacked *globalLightListBuffer )
    {
        //const LightListInfo &globalLightList = mSceneManager->getGlobalLightList();
        const size_t numLights = mCurrentLightList.size();

        if( !numLights )
            return;

        Matrix4 viewMatrix = camera->getViewMatrix();
        Matrix3 viewMatrix3;
        viewMatrix.extract3x3Matrix( viewMatrix3 );

        float * RESTRICT_ALIAS lightData = reinterpret_cast<float * RESTRICT_ALIAS>(
                    globalLightListBuffer->map( 0, NumBytesPerLight * numLights ) );
        LightArray::const_iterator itLights = mCurrentLightList.begin();
        LightArray::const_iterator enLights = mCurrentLightList.end();

        while( itLights != enLights )
        {
            const Light *light = *itLights;

            Vector3 lightPos = light->getParentNode()->_getDerivedPosition();
            lightPos = viewMatrix * lightPos;

            //vec3 lights[numLights].position
            *lightData++ = lightPos.x;
            *lightData++ = lightPos.y;
            *lightData++ = lightPos.z;
            *lightData++ = static_cast<float>( light->getType() );

            //vec3 lights[numLights].diffuse
            ColourValue colour = light->getDiffuseColour() *
                                 light->getPowerScale();
            *lightData++ = colour.r;
            *lightData++ = colour.g;
            *lightData++ = colour.b;
            ++lightData;

            //vec3 lights[numLights].specular
            colour = light->getSpecularColour() * light->getPowerScale();
            *lightData++ = colour.r;
            *lightData++ = colour.g;
            *lightData++ = colour.b;
            ++lightData;

            //vec3 lights[numLights].attenuation;
            Real attenRange     = light->getAttenuationRange();
            Real attenLinear    = light->getAttenuationLinear();
            Real attenQuadratic = light->getAttenuationQuadric();
            *lightData++ = attenRange;
            *lightData++ = attenLinear;
            *lightData++ = attenQuadratic;
            *lightData++ = 1.0f / attenRange;

            //vec3 lights[numLights].spotDirection;
            Vector3 spotDir = viewMatrix3 * light->getDerivedDirection();
            *lightData++ = spotDir.x;
            *lightData++ = spotDir.y;
            *lightData++ = spotDir.z;
            ++lightData;

            //vec3 lights[numLights].spotParams;
            Radian innerAngle = light->getSpotlightInnerAngle();
            Radian outerAngle = light->getSpotlightOuterAngle();
            *lightData++ = 1.0f / ( cosf( innerAngle.valueRadians() * 0.5f ) -
                                     cosf( outerAngle.valueRadians() * 0.5f ) );
            *lightData++ = cosf( outerAngle.valueRadians() * 0.5f );
            *lightData++ = light->getSpotlightFalloff();
            ++lightData;

            ++itLights;
        }

        globalLightListBuffer->unmap( UO_KEEP_PERSISTENT );
    }
    //-----------------------------------------------------------------------------------
    bool ForwardPlusBase::getCachedGridFor( Camera *camera, CachedGrid **outCachedGrid )
    {
        const CompositorShadowNode *shadowNode = mSceneManager->getCurrentShadowNode();

        CachedGridVec::iterator itor = mCachedGrid.begin();
        CachedGridVec::iterator end  = mCachedGrid.end();

        while( itor != end )
        {
            if( itor->camera == camera &&
                itor->reflection == camera->isReflected() &&
                (itor->aspectRatio - camera->getAspectRatio()) < 1e-6f &&
                itor->shadowNode == shadowNode )
            {
                bool upToDate = itor->lastFrame == mVaoManager->getFrameCount();
                itor->lastFrame = mVaoManager->getFrameCount();
                *outCachedGrid = &(*itor);

                //Not only this causes bugs see http://www.ogre3d.org/forums/viewtopic.php?f=25&t=88776
                //as far as I can't tell this is not needed anymore.
                //if( mSceneManager->isCurrentShadowNodeReused() )
                //    upToDate = false; //We can't really be sure the cache is up to date

                return upToDate;
            }

            ++itor;
        }

        //If we end up here, the entry doesn't exist. Create a new one.
        CachedGrid cachedGrid;
        cachedGrid.camera      = camera;
        cachedGrid.reflection  = camera->isReflected();
        cachedGrid.aspectRatio = camera->getAspectRatio();
        cachedGrid.shadowNode  = mSceneManager->getCurrentShadowNode();
        cachedGrid.lastFrame   = mVaoManager->getFrameCount();

        cachedGrid.gridBuffer               = 0;
        cachedGrid.globalLightListBuffer    = 0;

        mCachedGrid.push_back( cachedGrid );

        *outCachedGrid = &mCachedGrid.back();

        return false;
    }
    //-----------------------------------------------------------------------------------
    bool ForwardPlusBase::getCachedGridFor( Camera *camera, const CachedGrid **outCachedGrid ) const
    {
        CachedGridVec::const_iterator itor = mCachedGrid.begin();
        CachedGridVec::const_iterator end  = mCachedGrid.end();

        while( itor != end )
        {
            if( itor->camera == camera &&
                itor->reflection == camera->isReflected() &&
                (itor->aspectRatio - camera->getAspectRatio()) < 1e-6f &&
                itor->shadowNode == mSceneManager->getCurrentShadowNode() )
            {
                bool upToDate = itor->lastFrame == mVaoManager->getFrameCount();
                *outCachedGrid = &(*itor);

                return upToDate;
            }

            ++itor;
        }

        return false;
    }
    //-----------------------------------------------------------------------------------
    TexBufferPacked* ForwardPlusBase::getGridBuffer( Camera *camera ) const
    {
        CachedGrid const *cachedGrid = 0;

#ifdef NDEBUG
        getCachedGridFor( camera, &cachedGrid );
#else
        bool upToDate = getCachedGridFor( camera, &cachedGrid );
        assert( upToDate && "You must call ForwardPlusBase::collectLights first!" );
#endif

        return cachedGrid->gridBuffer;
    }
    //-----------------------------------------------------------------------------------
    TexBufferPacked* ForwardPlusBase::getGlobalLightListBuffer( Camera *camera ) const
    {
        CachedGrid const *cachedGrid = 0;

#ifdef NDEBUG
        getCachedGridFor( camera, &cachedGrid );
#else
        bool upToDate = getCachedGridFor( camera, &cachedGrid );
        assert( upToDate && "You must call ForwardPlusBase::collectLights first!" );
#endif

        return cachedGrid->globalLightListBuffer;
    }
    //-----------------------------------------------------------------------------------
    void ForwardPlusBase::setHlmsPassProperties( Hlms *hlms )
    {
        //ForwardPlus should be overriden by derived class to set the method in use.
        hlms->_setProperty( HlmsBaseProp::ForwardPlus,                  1 );
        hlms->_setProperty( HlmsBaseProp::ForwardPlusDebug,             mDebugMode );
        hlms->_setProperty( HlmsBaseProp::ForwardPlusFadeAttenRange,    mFadeAttenuationRange );
        hlms->_setProperty( HlmsBaseProp::VPos, 1 );

        hlms->_setProperty( HlmsBaseProp::Forward3D,        HlmsBaseProp::Forward3D.mHash );
        hlms->_setProperty( HlmsBaseProp::ForwardClustered, HlmsBaseProp::ForwardClustered.mHash );
    }
}
