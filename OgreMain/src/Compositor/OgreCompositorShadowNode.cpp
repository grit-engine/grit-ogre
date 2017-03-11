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

#include "OgreStableHeaders.h"

#include "Compositor/OgreCompositorShadowNode.h"
#include "Compositor/OgreCompositorManager2.h"
#include "Compositor/OgreCompositorWorkspace.h"

#include "Compositor/Pass/PassScene/OgreCompositorPassScene.h"

#include "OgreTextureManager.h"
#include "OgreHardwarePixelBuffer.h"
#include "OgreRenderSystem.h"
#include "OgreRenderTexture.h"
#include "OgreSceneManager.h"
#include "OgreViewport.h"
#include "OgrePass.h"
#include "OgreDepthBuffer.h"

#include "OgreShadowCameraSetupFocused.h"
#include "OgreShadowCameraSetupPSSM.h"

#if OGRE_COMPILER == OGRE_COMPILER_MSVC
    #include <intrin.h>
    #pragma intrinsic(_BitScanReverse)
#endif

namespace Ogre
{
    const Matrix4 PROJECTIONCLIPSPACE2DTOIMAGESPACE_PERSPECTIVE(
        0.5,    0,    0,  0.5,
        0,   -0.5,    0,  0.5,
        0,      0,    1,    0,
        0,      0,    0,    1);

    inline uint32 ctz( uint32 value )
    {
        if( value == 0 )
            return 32u;

#if OGRE_COMPILER == OGRE_COMPILER_MSVC
        DWORD trailingZero = 0;
        _BitScanForward( &trailingZero, value );
        return trailingZero;
#else
        return __builtin_ctz( value );
#endif
    }

    CompositorShadowNode::CompositorShadowNode( IdType id, const CompositorShadowNodeDef *definition,
                                                CompositorWorkspace *workspace, RenderSystem *renderSys,
                                                const RenderTarget *finalTarget ) :
            CompositorNode( id, definition->getName(), definition, workspace, renderSys, finalTarget ),
            mDefinition( definition ),
            mLastCamera( 0 ),
            mLastFrame( -1 ),
            mNumActiveShadowMapCastingLights( 0 )
    {
        mShadowMapCameras.reserve( definition->mShadowMapTexDefinitions.size() );
        mLocalTextures.reserve( mLocalTextures.size() + definition->mShadowMapTexDefinitions.size() );

        SceneManager *sceneManager = workspace->getSceneManager();
        SceneNode *pseudoRootNode = 0;

        if( !definition->mShadowMapTexDefinitions.empty() )
            pseudoRootNode = sceneManager->createSceneNode( SCENE_DYNAMIC );

        //Create the local textures
        CompositorShadowNodeDef::ShadowMapTexDefVec::const_iterator itor =
                                                            definition->mShadowMapTexDefinitions.begin();
        CompositorShadowNodeDef::ShadowMapTexDefVec::const_iterator end  =
                                                            definition->mShadowMapTexDefinitions.end();

        while( itor != end )
        {
            // One map, one camera
            const size_t shadowMapIdx = itor - definition->mShadowMapTexDefinitions.begin();
            ShadowMapCamera shadowMapCamera;
            shadowMapCamera.camera = sceneManager->createCamera( "ShadowNode Camera ID " +
                                                StringConverter::toString( id ) + " Map " +
                                                StringConverter::toString( shadowMapIdx ), false );
            shadowMapCamera.camera->setFixedYawAxis( false );
            shadowMapCamera.camera->setAutoAspectRatio( true );
            shadowMapCamera.minDistance = 0.0f;
            shadowMapCamera.maxDistance = 100000.0f;
            for( size_t i=0; i<Light::NUM_LIGHT_TYPES; ++i )
                shadowMapCamera.scenePassesViewportSize[i] = -Vector2::UNIT_SCALE;

            {
                //Find out the index to our texture in both mLocalTextures & mContiguousShadowMapTex
                size_t index;
                TextureDefinitionBase::TextureSource textureSource;
                mDefinition->getTextureSource( itor->getTextureName(), index, textureSource );

                // CompositorShadowNodeDef should've prevented this from not being true.
                assert( textureSource == TextureDefinitionBase::TEXTURE_LOCAL );

                shadowMapCamera.idxToLocalTextures = static_cast<uint32>( index );

                if( itor->mrtIndex >= mLocalTextures[index].textures.size() )
                {
                    OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS, "Texture " +
                                 itor->getTextureNameStr() + " does not have MRT index " +
                                 StringConverter::toString( itor->mrtIndex ),
                                 "CompositorShadowNode::CompositorShadowNode" );
                }

                TexturePtr &refTex = mLocalTextures[index].textures[itor->mrtIndex];
                TextureVec::const_iterator itContig = std::find( mContiguousShadowMapTex.begin(),
                                                                 mContiguousShadowMapTex.end(), refTex );
                if( itContig == mContiguousShadowMapTex.end() )
                {
                    mContiguousShadowMapTex.push_back( refTex );
                    itContig = mContiguousShadowMapTex.end() - 1u;
                }

                shadowMapCamera.idxToContiguousTex = static_cast<uint32>(
                            itContig - mContiguousShadowMapTex.begin() );
            }


            {
                //Attach the camera to a node that exists outside the scene, so that it
                //doesn't get affected by relative origins (otherwise we'll be setting
                //the relative origin *twice*)
                shadowMapCamera.camera->detachFromParent();
                pseudoRootNode->attachObject( shadowMapCamera.camera );
            }


            const size_t sharingSetupIdx = itor->getSharesSetupWith();
            if( sharingSetupIdx != std::numeric_limits<size_t>::max() )
            {
                shadowMapCamera.shadowCameraSetup = mShadowMapCameras[sharingSetupIdx].shadowCameraSetup;
            }
            else
            {
                switch( itor->shadowMapTechnique )
                {
                case SHADOWMAP_UNIFORM:
                    shadowMapCamera.shadowCameraSetup =
                                    ShadowCameraSetupPtr( OGRE_NEW DefaultShadowCameraSetup() );
                    break;
                /*case SHADOWMAP_PLANEOPTIMAL:
                    break;*/
                case SHADOWMAP_FOCUSED:
                    {
                        FocusedShadowCameraSetup *setup = OGRE_NEW FocusedShadowCameraSetup();
                        shadowMapCamera.shadowCameraSetup = ShadowCameraSetupPtr( setup );
                    }
                    break;
                case SHADOWMAP_PSSM:
                    {
                        PSSMShadowCameraSetup *setup = OGRE_NEW PSSMShadowCameraSetup();
                        shadowMapCamera.shadowCameraSetup = ShadowCameraSetupPtr( setup );
                        setup->calculateSplitPoints( itor->numSplits, 0.1f, 100.0f, 0.95f );
                        setup->setSplitPadding( itor->splitPadding );
                    }
                    break;
                default:
                    OGRE_EXCEPT( Exception::ERR_NOT_IMPLEMENTED,
                                "Shadow Map technique not implemented or not recognized.",
                                "CompositorShadowNode::CompositorShadowNode");
                    break;
                }
            }

            mShadowMapCameras.push_back( shadowMapCamera );

            ++itor;
        }

        // Shadow Nodes don't have input; and global textures should be ready by
        // the time we get created. Therefore, we can safely initialize now as our
        // output may be used in regular nodes and we're created on-demand (as soon
        // as a Node discovers it needs us for the first time, we get created)
        createPasses();

        mShadowMapCastingLights.resize( mDefinition->mNumLights );
    }
    //-----------------------------------------------------------------------------------
    CompositorShadowNode::~CompositorShadowNode()
    {
        SceneNode *pseudoRootNode = 0;
        SceneManager *sceneManager = mWorkspace->getSceneManager();

        ShadowMapCameraVec::const_iterator itor = mShadowMapCameras.begin();
        ShadowMapCameraVec::const_iterator end  = mShadowMapCameras.end();

        while( itor != end )
        {
            pseudoRootNode = itor->camera->getParentSceneNode();
            sceneManager->destroyCamera( itor->camera );
            ++itor;
        }

        if( pseudoRootNode )
            sceneManager->destroySceneNode( pseudoRootNode );
    }
    //-----------------------------------------------------------------------------------
    //An Input Iterator that is the same as doing vector<int> val( N ); and goes in increasing
    //order (i.e. val[0] = 0; val[1] = 1; val[n-1] = n-1) but doesn't occupy N elements in memory,
    //just one.
    struct MemoryLessInputIterator : public std::iterator<std::input_iterator_tag, size_t>
    {
        size_t index;
        MemoryLessInputIterator( size_t startValue ) : index( startValue ) {}

        MemoryLessInputIterator& operator ++() //Prefix increment
        {
            ++index;
            return *this;
        }

        MemoryLessInputIterator operator ++(int) //Postfix increment
        {
            MemoryLessInputIterator copy = *this;
            ++index;
            return copy;
        }

        size_t operator *() const   { return index; }

        bool operator == (const MemoryLessInputIterator &r) const    { return index == r.index; }
        bool operator != (const MemoryLessInputIterator &r) const    { return index != r.index; }
    };

    class ShadowMappingLightCmp
    {
        LightListInfo const *mLightList;
        uint32              mCombinedVisibilityFlags;
        Vector3             mCameraPos;

    public:
        ShadowMappingLightCmp( LightListInfo const *lightList, uint32 combinedVisibilityFlags,
                               const Vector3 &cameraPos ) :
            mLightList( lightList ), mCombinedVisibilityFlags( combinedVisibilityFlags ), mCameraPos( cameraPos )
        {
        }

        bool operator()( size_t _l, size_t _r ) const
        {
            uint32 visibilityMaskL = mLightList->visibilityMask[_l];
            uint32 visibilityMaskR = mLightList->visibilityMask[_r];

            if( (visibilityMaskL & mCombinedVisibilityFlags) &&
                !(visibilityMaskR & mCombinedVisibilityFlags) )
            {
                return true;
            }
            else if( !(visibilityMaskL & mCombinedVisibilityFlags) &&
                     (visibilityMaskR & mCombinedVisibilityFlags) )
            {
                return false;
            }
            else if( (visibilityMaskL & VisibilityFlags::LAYER_SHADOW_CASTER) &&
                    !(visibilityMaskR & VisibilityFlags::LAYER_SHADOW_CASTER) )
            {
                return true;
            }
            else if( !(visibilityMaskL & VisibilityFlags::LAYER_SHADOW_CASTER) &&
                     (visibilityMaskR & VisibilityFlags::LAYER_SHADOW_CASTER) )
            {
                return false;
            }

            Real fDistL = mCameraPos.distance( mLightList->boundingSphere[_l].getCenter() ) -
                          mLightList->boundingSphere[_l].getRadius();
            Real fDistR = mCameraPos.distance( mLightList->boundingSphere[_r].getCenter() ) -
                          mLightList->boundingSphere[_r].getRadius();
            return fDistL < fDistR;
        }
    };
    void CompositorShadowNode::buildClosestLightList( Camera *newCamera, const Camera *lodCamera )
    {
        const size_t currentFrameCount = mWorkspace->getFrameCount();
        if( mLastCamera == newCamera && mLastFrame == currentFrameCount )
        {
            return;
        }

        mLastFrame = currentFrameCount;

        mLastCamera = newCamera;

        const Viewport *viewport = newCamera->getLastViewport();
        const SceneManager *sceneManager = newCamera->getSceneManager();
        const LightListInfo &globalLightList = sceneManager->getGlobalLightList();

        uint32 combinedVisibilityFlags = viewport->getVisibilityMask() &
                                            sceneManager->getVisibilityMask();

        size_t startIndex = 0;
        mShadowMapCastingLights.clear(); //TODO: Do not clear statically updated lights.
        mShadowMapCastingLights.resize( mDefinition->mNumLights );
        mNumActiveShadowMapCastingLights = 0;
        size_t begEmptyLightIdx = 0;
        size_t nxtEmptyLightIdx = 0;
        findNextEmptyShadowCastingLightEntry( 1u << Light::LT_DIRECTIONAL,
                                              &begEmptyLightIdx, &nxtEmptyLightIdx );

        mAffectedLights.clear();
        mAffectedLights.resize( globalLightList.lights.size(), false );

        {
            //SceneManager puts the directional lights first. Add them first as casters.
            LightArray::const_iterator itor = globalLightList.lights.begin();
            LightArray::const_iterator end  = globalLightList.lights.end();

            uint32 const * RESTRICT_ALIAS visibilityMask = globalLightList.visibilityMask;

            while( itor != end && (*itor)->getType() == Light::LT_DIRECTIONAL &&
                   nxtEmptyLightIdx < mShadowMapCastingLights.size() )
            {
                if( (*visibilityMask & combinedVisibilityFlags) &&
                    (*visibilityMask & VisibilityFlags::LAYER_SHADOW_CASTER) )
                {
                    const size_t listIdx = itor - globalLightList.lights.begin();
                    mAffectedLights[listIdx] = true;
                    mShadowMapCastingLights[nxtEmptyLightIdx] = LightClosest( *itor, listIdx, 0 );
                    findNextEmptyShadowCastingLightEntry( 1u << Light::LT_DIRECTIONAL,
                                                          &begEmptyLightIdx, &nxtEmptyLightIdx );
                    ++mNumActiveShadowMapCastingLights;
                }

                ++visibilityMask;
                ++itor;
            }

            //Reach the end of directional lights section
            while( itor != end && (*itor)->getType() == Light::LT_DIRECTIONAL )
                ++itor;

            startIndex = itor - globalLightList.lights.begin();
        }

        const Vector3 &camPos( newCamera->getDerivedPosition() );

        mTmpSortedIndexes.resize( mShadowMapCastingLights.size() - begEmptyLightIdx, ~0 );
        std::partial_sort_copy( MemoryLessInputIterator( startIndex ),
                            MemoryLessInputIterator( globalLightList.lights.size() ),
                            mTmpSortedIndexes.begin(), mTmpSortedIndexes.end(),
                            ShadowMappingLightCmp( &globalLightList, combinedVisibilityFlags, camPos ) );

        vector<size_t>::type::const_iterator itor = mTmpSortedIndexes.begin();
        vector<size_t>::type::const_iterator end  = mTmpSortedIndexes.end();

        while( itor != end )
        {
            uint32 visibilityMask = globalLightList.visibilityMask[*itor];
            if( !(visibilityMask & combinedVisibilityFlags) ||
                !(visibilityMask & VisibilityFlags::LAYER_SHADOW_CASTER) ||
                begEmptyLightIdx >= mShadowMapCastingLights.size() )
            {
                break;
            }

            findNextEmptyShadowCastingLightEntry( 1u << globalLightList.lights[*itor]->getType(),
                                                  &begEmptyLightIdx, &nxtEmptyLightIdx );

            if( nxtEmptyLightIdx < mShadowMapCastingLights.size() )
            {
                mAffectedLights[*itor] = true;
                mShadowMapCastingLights[nxtEmptyLightIdx] =
                        LightClosest( globalLightList.lights[*itor], *itor, 0 );
                ++mNumActiveShadowMapCastingLights;
            }
            ++itor;
        }

        mCastersBox = sceneManager->_calculateCurrentCastersBox( viewport->getVisibilityMask(),
                                                                 mDefinition->mMinRq,
                                                                 mDefinition->mMaxRq );
    }
    //-----------------------------------------------------------------------------------
    void CompositorShadowNode::findNextEmptyShadowCastingLightEntry(
            uint8 lightTypeMask,
            size_t * RESTRICT_ALIAS startIdx,
            size_t * RESTRICT_ALIAS entryToUse ) const
    {
        size_t lightIdx = *startIdx;

        size_t newStartIdx = mShadowMapCastingLights.size();

        LightClosestArray::const_iterator itCastingLight = mShadowMapCastingLights.begin() + lightIdx;
        LightClosestArray::const_iterator enCastingLight = mShadowMapCastingLights.end();
        while( itCastingLight != enCastingLight )
        {
            if( !itCastingLight->light )
            {
                newStartIdx = std::min( lightIdx, newStartIdx );
                if( mDefinition->mLightTypesMask[lightIdx] & lightTypeMask )
                {
                    *startIdx = newStartIdx;
                    *entryToUse = lightIdx;
                    return;
                }
            }

            ++lightIdx;
            ++itCastingLight;
        }

        //If we get here entryToUse == mShadowMapCastingLights.size() but startIdx may still
        //be valid (we found no entry that supports the requested light type but there could
        //still be empty entries for other types of light)
        *startIdx = newStartIdx;
        *entryToUse = lightIdx;
    }
    //-----------------------------------------------------------------------------------
    void CompositorShadowNode::_update( Camera* camera, const Camera *lodCamera,
                                        SceneManager *sceneManager )
    {
        ShadowMapCameraVec::iterator itShadowCamera = mShadowMapCameras.begin();
        const Viewport *viewport    = camera->getLastViewport();

        buildClosestLightList( camera, lodCamera );

        //Setup all the cameras
        CompositorShadowNodeDef::ShadowMapTexDefVec::const_iterator itor =
                                                            mDefinition->mShadowMapTexDefinitions.begin();
        CompositorShadowNodeDef::ShadowMapTexDefVec::const_iterator end  =
                                                            mDefinition->mShadowMapTexDefinitions.end();

        while( itor != end )
        {
            Light const *light = mShadowMapCastingLights[itor->light].light;

            if( light )
            {
                Camera *texCamera = itShadowCamera->camera;

                //Use the material scheme of the main viewport
                //This is required to pick up the correct shadow_caster_material and similar properties.
                //dark_sylinc: removed. It's losing usefulness (Hlms), and it's broken (CompositorPassScene
                //will overwrite it anyway)
                //texCamera->getLastViewport()->setMaterialScheme( viewport->getMaterialScheme() );

                // Associate main view camera as LOD camera
                texCamera->setLodCamera( lodCamera );

                // set base
                if( light->getType() != Light::LT_POINT )
                {
                    texCamera->setOrientation( light->getParentNode()->_getDerivedOrientation() );
                }
                if( light->getType() != Light::LT_DIRECTIONAL )
                {
                    texCamera->setPosition( light->getParentNode()->_getDerivedPosition() );
                }

                if( itor->shadowMapTechnique == SHADOWMAP_PSSM )
                {
                    assert( dynamic_cast<PSSMShadowCameraSetup*>
                            ( itShadowCamera->shadowCameraSetup.get() ) );

                    PSSMShadowCameraSetup *pssmSetup = static_cast<PSSMShadowCameraSetup*>
                                                        ( itShadowCamera->shadowCameraSetup.get() );
                    if( pssmSetup->getSplitPoints()[0] != camera->getNearClipDistance() ||
                        pssmSetup->getSplitPoints()[itor->numSplits-1] != light->getShadowFarDistance() )
                    {
                        pssmSetup->calculateSplitPoints( itor->numSplits, camera->getNearClipDistance(),
                                                    light->getShadowFarDistance(), itor->pssmLambda );
                    }
                }

                //Set the viewport to 0, to explictly crash if accidentally using it. Compositors
                //may have many passes of different sizes and resolutions that affect the same shadow
                //map and it's impossible to tell which one is "the main one" (if there's any)
                texCamera->_notifyViewport( 0 );

                const Vector2 vpRealSize = itShadowCamera->scenePassesViewportSize[light->getType()];
                itShadowCamera->shadowCameraSetup->getShadowCamera( sceneManager, camera, light,
                                                                    texCamera, itor->split,
                                                                    vpRealSize );

                itShadowCamera->minDistance = itShadowCamera->shadowCameraSetup->getMinDistance();
                itShadowCamera->maxDistance = itShadowCamera->shadowCameraSetup->getMaxDistance();
            }
            //Else... this shadow map shouldn't be rendered and when used, return a blank one.
            //The Nth closest lights don't cast shadows

            ++itShadowCamera;
            ++itor;
        }

        SceneManager::IlluminationRenderStage previous = sceneManager->_getCurrentRenderStage();
        sceneManager->_setCurrentRenderStage( SceneManager::IRS_RENDER_TO_TEXTURE );

        //Now render all passes
        CompositorNode::_update( lodCamera, sceneManager );

        sceneManager->_setCurrentRenderStage( previous );
    }
    //-----------------------------------------------------------------------------------
    void CompositorShadowNode::postInitializePass( CompositorPass *pass )
    {
        const CompositorPassDef *passDef = pass->getDefinition();

        //passDef->mShadowMapIdx may be invalid if this is not a pass
        //tied to a shadow map in particular (e.g. clearing an atlas)
        if( passDef->mShadowMapIdx < mShadowMapCameras.size() )
        {
            if( passDef->getType() == PASS_SCENE )
            {
                ShadowMapCamera &smCamera = mShadowMapCameras[passDef->mShadowMapIdx];

                const Viewport *vp = pass->getViewport();
                const Vector2 vpSize = Vector2( vp->getActualWidth(), vp->getActualHeight() );

                const CompositorTargetDef *targetPass = passDef->getParentTargetDef();
                uint8 lightTypesLeft = targetPass->getShadowMapSupportedLightTypes();

                //Get the viewport size set for this shadow node (which may vary per light type,
                //but for the same light type, it must remain constant for all passes to the
                //same shadow map)
                uint32 firstBitSet = ctz( lightTypesLeft );
                while( firstBitSet != 32u )
                {
                    assert( smCamera.scenePassesViewportSize[firstBitSet].x < Real( 0.0 ) ||
                            smCamera.scenePassesViewportSize[firstBitSet].x < Real( 0.0 ) ||
                            smCamera.scenePassesViewportSize[firstBitSet] == vpSize &&
                            "Two scene passes to the same shadow map have different viewport sizes! "
                            "Ogre cannot determine how to prevent jittering. Maybe you meant assign "
                            "assign each light types to different passes but you assigned more than "
                            "one light type (or the wrong one) to the same pass?" );

                    smCamera.scenePassesViewportSize[firstBitSet] = vpSize;

                    lightTypesLeft &= ~(1u << ((uint8)firstBitSet));
                    firstBitSet = ctz( lightTypesLeft );
                }

                assert( dynamic_cast<CompositorPassScene*>(pass) );
                static_cast<CompositorPassScene*>(pass)->_setCustomCamera( smCamera.camera );
                static_cast<CompositorPassScene*>(pass)->_setCustomCullCamera( smCamera.camera );
            }
        }
    }
    //-----------------------------------------------------------------------------------
    const LightList* CompositorShadowNode::setShadowMapsToPass( Renderable* rend, const Pass* pass,
                                                                AutoParamDataSource *autoParamDataSource,
                                                                size_t startLight )
    {
        const size_t lightsPerPass = pass->getMaxSimultaneousLights();

        mCurrentLightList.clear();
        mCurrentLightList.reserve( lightsPerPass );

        const LightList& renderableLights = rend->getLights();

        size_t shadowMapStart = std::min( startLight, mShadowMapCastingLights.size() );
        size_t shadowMapEnd   = std::min( startLight + lightsPerPass, mShadowMapCastingLights.size() );

        //Push **all** shadow casting lights first.
        {
            LightClosestArray::const_iterator itor = mShadowMapCastingLights.begin() + shadowMapStart;
            LightClosestArray::const_iterator end  = mShadowMapCastingLights.begin() + shadowMapEnd;
            while( itor != end )
            {
                mCurrentLightList.push_back( *itor );
                ++itor;
            }
        }

        //Now again, but push non-shadow casting lights (if there's room left)
        {
            size_t slotsToSkip  = std::max<ptrdiff_t>( startLight - mCurrentLightList.size(), 0 );
            size_t slotsLeft    = std::max<ptrdiff_t>( lightsPerPass - (shadowMapEnd - shadowMapStart), 0 );
            LightList::const_iterator itor = renderableLights.begin();
            LightList::const_iterator end  = renderableLights.end();
            while( itor != end && slotsLeft > 0 )
            {
                if( !mAffectedLights[itor->globalIndex] )
                {
                    if( slotsToSkip > 0 )
                    {
                        --slotsToSkip;
                    }
                    else
                    {
                        mCurrentLightList.push_back( *itor );
                        --slotsLeft;
                    }
                }
                ++itor;
            }
        }

        //Set the shadow map texture units
        {
            CompositorManager2 *compoMgr = mWorkspace->getCompositorManager();

            assert( shadowMapStart < mDefinition->mShadowMapTexDefinitions.size() );

            size_t shadowIdx=0;
            CompositorShadowNodeDef::ShadowMapTexDefVec::const_iterator shadowTexItor =
                                        mDefinition->mShadowMapTexDefinitions.begin() + shadowMapStart;
            CompositorShadowNodeDef::ShadowMapTexDefVec::const_iterator shadowTexItorEnd  =
                                        mDefinition->mShadowMapTexDefinitions.end();
            while( shadowTexItor != shadowTexItorEnd && shadowIdx < pass->getNumShadowContentTextures() )
            {
                size_t texUnitIdx = pass->_getTextureUnitWithContentTypeIndex(
                                                    TextureUnitState::CONTENT_SHADOW, shadowIdx );
                // I know, nasty const_cast
                TextureUnitState *texUnit = const_cast<TextureUnitState*>(
                                                    pass->getTextureUnitState(texUnitIdx) );

                // Projective texturing needs to be disabled explicitly when using vertex shaders.
                texUnit->setProjectiveTexturing( false, (const Frustum*)0 );
                autoParamDataSource->setTextureProjector( mShadowMapCameras[shadowIdx].camera,
                                                            shadowIdx );

                //TODO: textures[0] is out of bounds when using shadow atlas. Also see how what
                //changes need to be done so that UV calculations land on the right place
                const TexturePtr& shadowTex = mLocalTextures[shadowIdx].textures[0];
                texUnit->_setTexturePtr( shadowTex );

                ++shadowIdx;
                ++shadowTexItor;
            }

            for( ; shadowIdx<pass->getNumShadowContentTextures(); ++shadowIdx )
            {
                //If we're here, the material supports more shadow maps than the
                //shadow node actually renders. This probably smells slopy setup.
                //Put blank textures
                size_t texUnitIdx = pass->_getTextureUnitWithContentTypeIndex(
                                                    TextureUnitState::CONTENT_SHADOW, shadowIdx );
                // I know, nasty const_cast
                TextureUnitState *texUnit = const_cast<TextureUnitState*>(
                                                    pass->getTextureUnitState(texUnitIdx) );
                texUnit->_setTexturePtr( compoMgr->getNullShadowTexture( PF_R8G8B8A8 ) );

                // Projective texturing needs to be disabled explicitly when using vertex shaders.
                texUnit->setProjectiveTexturing( false, (const Frustum*)0 );
                autoParamDataSource->setTextureProjector( 0, shadowIdx );
            }
        }

        return &mCurrentLightList;
    }
    //-----------------------------------------------------------------------------------
    bool CompositorShadowNode::isShadowMapIdxInValidRange( uint32 shadowMapIdx ) const
    {
        return shadowMapIdx < mDefinition->mShadowMapTexDefinitions.size();
    }
    //-----------------------------------------------------------------------------------
    bool CompositorShadowNode::isShadowMapIdxActive( uint32 shadowMapIdx ) const
    {
        if( shadowMapIdx < mDefinition->mShadowMapTexDefinitions.size() )
        {
            const ShadowTextureDefinition &shadowTexDef =
                    mDefinition->mShadowMapTexDefinitions[shadowMapIdx];
            return mShadowMapCastingLights[shadowTexDef.light].light != 0;
        }
        else
        {
            return true;
        }
    }
    //-----------------------------------------------------------------------------------
    uint8 CompositorShadowNode::getShadowMapLightTypeMask( uint32 shadowMapIdx ) const
    {
        const ShadowTextureDefinition &shadowTexDef =
                mDefinition->mShadowMapTexDefinitions[shadowMapIdx];
        return 1u << mShadowMapCastingLights[shadowTexDef.light].light->getType();
    }
    //-----------------------------------------------------------------------------------
    void CompositorShadowNode::getMinMaxDepthRange( const Frustum *shadowMapCamera,
                                                    Real &outMin, Real &outMax ) const
    {
        ShadowMapCameraVec::const_iterator itor = mShadowMapCameras.begin();
        ShadowMapCameraVec::const_iterator end  = mShadowMapCameras.end();

        while( itor != end )
        {
            if( itor->camera == shadowMapCamera )
            {
                outMin = itor->minDistance;
                outMax = itor->maxDistance;
                return;
            }
            ++itor;
        }

        outMin = 0.0f;
        outMax = 100000.0f;
    }
    //-----------------------------------------------------------------------------------
    void CompositorShadowNode::getMinMaxDepthRange( size_t shadowMapIdx,
                                                    Real &outMin, Real &outMax ) const
    {
        outMin = mShadowMapCameras[shadowMapIdx].minDistance;
        outMax = mShadowMapCameras[shadowMapIdx].maxDistance;
    }
    //-----------------------------------------------------------------------------------
    Matrix4 CompositorShadowNode::getViewProjectionMatrix( size_t shadowMapIdx ) const
    {
        const ShadowTextureDefinition &shadowTexDef =
                mDefinition->mShadowMapTexDefinitions[shadowMapIdx];
        Matrix4 clipToImageSpace;

        Vector3 vScale(  0.5f * shadowTexDef.uvLength.x,
                        -0.5f * shadowTexDef.uvLength.y, 1.0f );
        clipToImageSpace.makeTransform( Vector3(  vScale.x + shadowTexDef.uvOffset.x,
                                                 -vScale.y + shadowTexDef.uvOffset.y, 0.0f ),
                                        Vector3(  vScale.x, vScale.y, 1.0f ),
                                        Quaternion::IDENTITY );

        return /*PROJECTIONCLIPSPACE2DTOIMAGESPACE_PERSPECTIVE*/clipToImageSpace *
                mShadowMapCameras[shadowMapIdx].camera->getProjectionMatrixWithRSDepth() *
                mShadowMapCameras[shadowMapIdx].camera->getViewMatrix( true );
    }
    //-----------------------------------------------------------------------------------
    const vector<Real>::type* CompositorShadowNode::getPssmSplits( size_t shadowMapIdx ) const
    {
        vector<Real>::type const *retVal = 0;

        if( shadowMapIdx < mShadowMapCastingLights.size() )
        {
            if( mDefinition->mShadowMapTexDefinitions[shadowMapIdx].shadowMapTechnique ==
                SHADOWMAP_PSSM &&
                isShadowMapIdxActive( shadowMapIdx ) )
            {
                assert( dynamic_cast<PSSMShadowCameraSetup*>(
                        mShadowMapCameras[shadowMapIdx].shadowCameraSetup.get() ) );

                PSSMShadowCameraSetup *pssmSetup = static_cast<PSSMShadowCameraSetup*>(
                                            mShadowMapCameras[shadowMapIdx].shadowCameraSetup.get() );
                retVal = &pssmSetup->getSplitPoints();
            }
        }

        return retVal;
    }
    //-----------------------------------------------------------------------------------
    uint32 CompositorShadowNode::getIndexToContiguousShadowMapTex( size_t shadowMapIdx ) const
    {
        return mShadowMapCameras[shadowMapIdx].idxToContiguousTex;
    }
    //-----------------------------------------------------------------------------------
    void CompositorShadowNode::finalTargetResized( const RenderTarget *finalTarget )
    {
        CompositorNode::finalTargetResized( finalTarget );

        mContiguousShadowMapTex.clear();

        CompositorShadowNodeDef::ShadowMapTexDefVec::const_iterator itDef =
                mDefinition->mShadowMapTexDefinitions.begin();
        ShadowMapCameraVec::const_iterator itor = mShadowMapCameras.begin();
        ShadowMapCameraVec::const_iterator end  = mShadowMapCameras.end();

        while( itor != end )
        {
            if( itor->idxToContiguousTex >= mContiguousShadowMapTex.size() )
            {
                mContiguousShadowMapTex.push_back(
                            mLocalTextures[itor->idxToLocalTextures].textures[itDef->mrtIndex] );
            }

            ++itDef;
            ++itor;
        }
    }
}
