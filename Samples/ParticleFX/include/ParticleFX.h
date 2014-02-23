#ifndef __ParticleFX_H__
#define __ParticleFX_H__

#include "SdkSample.h"
#include "OgreParticleSystem.h"

using namespace Ogre;
using namespace OgreBites;

class _OgreSampleClassExport Sample_ParticleFX : public SdkSample
{
public:

    Sample_ParticleFX()
    {
        mInfo["Title"] = "Particle Effects";
        mInfo["Description"] = "Demonstrates the creation and usage of particle effects.";
        mInfo["Thumbnail"] = "thumb_particles.png";
        mInfo["Category"] = "Effects";
        mInfo["Help"] = "Use the checkboxes to toggle visibility of the individual particle systems.";
    }

    bool frameRenderingQueued(const FrameEvent& evt)
    {
        mFountainPivot->yaw(Degree(evt.timeSinceLastFrame * 30));   // spin the fountains around

        return SdkSample::frameRenderingQueued(evt);   // don't forget the parent class updates!
    }

    void checkBoxToggled(CheckBox* box)
    {
        // show or hide the particle system with the same name as the check box
        mParticleSystems[box->getName()]->setVisible(box->isChecked());
    }

protected:

    void setupContent()
    {
        // setup some basic lighting for our scene
        mSceneMgr->setAmbientLight(ColourValue(0.3, 0.3, 0.3));
        SceneNode *lightNode = mSceneMgr->getRootSceneNode()->createChildSceneNode();
        lightNode->attachObject( mSceneMgr->createLight() );
        lightNode->setPosition(20, 80, 50);

        // set our camera to orbit around the origin and show cursor
        mCameraMan->setStyle(CS_ORBIT);
        mCameraMan->setYawPitchDist(Degree(0), Degree(15), 250);
        mTrayMgr->showCursor();

        // create an ogre head entity and place it at the origin
        Entity* ent = mSceneMgr->createEntity( "ogrehead.mesh",
                                                ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME,
                                                SCENE_STATIC );
        mSceneMgr->getRootSceneNode( SCENE_STATIC )->attachObject(ent);
        
        setupParticles();   // setup particles
        setupTogglers();    // setup particle togglers
    }

    void setupParticles()
    {
        ParticleSystem::setDefaultNonVisibleUpdateTimeout(5);  // set nonvisible timeout

        ParticleSystem* ps;

        // create some nice fireworks and place it at the origin
        ps = mSceneMgr->createParticleSystem("Examples/Fireworks");
        mSceneMgr->getRootSceneNode()->attachObject(ps);
        mParticleSystems["Fireworks"] = ps;

        // create a green nimbus around the ogre head
        ps = mSceneMgr->createParticleSystem("Examples/GreenyNimbus");
        mSceneMgr->getRootSceneNode()->attachObject(ps);
        mParticleSystems["Nimbus"] = ps;
       
        ps = mSceneMgr->createParticleSystem("Examples/Rain");  // create a rainstorm
        ps->fastForward(5);   // fast-forward the rain so it looks more natural
        mSceneMgr->getRootSceneNode()->createChildSceneNode(SCENE_DYNAMIC, Vector3(0, 1000, 0))->attachObject(ps);
        mParticleSystems["Rain"] = ps;

        // create aureola around ogre head perpendicular to the ground
        ps = mSceneMgr->createParticleSystem("Examples/Aureola");
        mSceneMgr->getRootSceneNode()->attachObject(ps);
        mParticleSystems["Aureola"] = ps;

        // create shared pivot node for spinning the fountains
        mFountainPivot = mSceneMgr->getRootSceneNode()->createChildSceneNode();

        ps = mSceneMgr->createParticleSystem("Examples/PurpleFountain");  // create fountain 1
        // attach the fountain to a child node of the pivot at a distance and angle
        mFountainPivot->createChildSceneNode(SCENE_DYNAMIC, Vector3(200, -100, 0), Quaternion(Degree(20), Vector3::UNIT_Z))->attachObject(ps);
        mParticleSystems["Fountain1"] = ps;
        
        ps = mSceneMgr->createParticleSystem("Examples/PurpleFountain");  // create fountain 2
        // attach the fountain to a child node of the pivot at a distance and angle
        mFountainPivot->createChildSceneNode(SCENE_DYNAMIC, Vector3(-200, -100, 0), Quaternion(Degree(-20), Vector3::UNIT_Z))->attachObject(ps);
        mParticleSystems["Fountain2"] = ps;
    }

    void setupTogglers()
    {
        // create check boxes to toggle the visibility of our particle systems
        mTrayMgr->createLabel(TL_TOPLEFT, "VisLabel", "Particles");
        mTrayMgr->createCheckBox(TL_TOPLEFT, "Fireworks", "Fireworks", 130)->setChecked(true);
        mTrayMgr->createCheckBox(TL_TOPLEFT, "Fountain1", "Fountain A", 130)->setChecked(true);
        mTrayMgr->createCheckBox(TL_TOPLEFT, "Fountain2", "Fountain B", 130)->setChecked(true);
        mTrayMgr->createCheckBox(TL_TOPLEFT, "Aureola", "Aureola", 130)->setChecked(false);
        mTrayMgr->createCheckBox(TL_TOPLEFT, "Nimbus", "Nimbus", 130)->setChecked(false);
        mTrayMgr->createCheckBox(TL_TOPLEFT, "Rain", "Rain", 130)->setChecked(false);
    }

    SceneNode* mFountainPivot;
    map<IdString, ParticleSystem*>::type mParticleSystems;
};

#endif
