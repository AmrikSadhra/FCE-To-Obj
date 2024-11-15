#include "PhysicsEngine.h"

#include "CollisionMasks.h"

namespace OpenNFS {
    WorldRay ScreenPosToWorldRay(int mouseX, int mouseY, int screenWidth, int screenHeight, glm::mat4 ViewMatrix, glm::mat4 ProjectionMatrix) {
        // The ray Start and End positions, in Normalized Device Coordinates
        glm::vec4 lRayStart_NDC(((float) mouseX / (float) screenWidth - 0.5f) * 2.0f,
                                ((float) mouseY / (float) screenHeight - 0.5f) * 2.0f,
                                -1.0, // The near plane maps to Z=-1 in Normalized Device Coordinates
                                1.0f);
        glm::vec4 lRayEnd_NDC(((float) mouseX / (float) screenWidth - 0.5f) * 2.0f, ((float) mouseY / (float) screenHeight - 0.5f) * 2.0f, 0.0, 1.0f);

        // The Projection matrix goes from Camera Space to NDC.
        // So inverse(ProjectionMatrix) goes from NDC to Camera Space.
        glm::mat4 InverseProjectionMatrix = glm::inverse(ProjectionMatrix);

        // The View Matrix goes from World Space to Camera Space.
        // So inverse(ViewMatrix) goes from Camera Space to World Space.
        glm::mat4 InverseViewMatrix = glm::inverse(ViewMatrix);

        // Faster way (just one inverse)
        glm::mat4 M               = glm::inverse(ProjectionMatrix * ViewMatrix);
        glm::vec4 lRayStart_world = M * lRayStart_NDC;
        lRayStart_world /= lRayStart_world.w;
        glm::vec4 lRayEnd_world = M * lRayEnd_NDC;
        lRayEnd_world /= lRayEnd_world.w;

        glm::vec3 lRayDir_world(lRayEnd_world - lRayStart_world);
        lRayDir_world = glm::normalize(lRayDir_world);

        WorldRay worldRay;
        worldRay.origin = glm::vec3(lRayStart_world);
        worldRay.direction = glm::normalize(lRayDir_world);

        return worldRay;
    }

    PhysicsEngine::PhysicsEngine() : debugDrawer(std::make_shared<BulletDebugDrawer>()) {
        m_pBroadphase = std::make_unique<btDbvtBroadphase>();
        // Set up the collision configuration and dispatcher
        m_pCollisionConfiguration = std::make_unique<btDefaultCollisionConfiguration>();
        m_pDispatcher             = std::make_unique<btCollisionDispatcher>(m_pCollisionConfiguration.get());
        // The actual physics solver
        m_pSolver = std::make_unique<btSequentialImpulseConstraintSolver>();
        // The world.
        m_pDynamicsWorld = std::make_unique<btDiscreteDynamicsWorld>(m_pDispatcher.get(), m_pBroadphase.get(), m_pSolver.get(), m_pCollisionConfiguration.get());
        m_pDynamicsWorld->setGravity(btVector3(0, -9.81f, 0));
        m_pDynamicsWorld->setDebugDrawer(debugDrawer.get());
    }

    void PhysicsEngine::StepSimulation(float time, const std::vector<uint32_t> &racerResidentTrackblockIDs) {
        m_pDynamicsWorld->stepSimulation(time, 100);

        for (auto &car : m_activeVehicles) {
            car->Update(m_pDynamicsWorld.get());
        }

        // TrackModel updates propagate for active track blocks, based upon track blocks racer vehicles are on
        // for (auto &residentTrackblockID : racerResidentTrackblockIDs) {
        for (auto &entity : m_track->entities) {
            entity->Update();
        }
    }

    Entity *PhysicsEngine::CheckForPicking(const glm::mat4 &viewMatrix, const glm::mat4 &projectionMatrix, bool &entityTargeted) {
        WorldRay worldRayFromScreenPosition =
          ScreenPosToWorldRay(Config::get().resX / 2, Config::get().resY / 2, Config::get().resX, Config::get().resY, viewMatrix, projectionMatrix);
        glm::vec3 outEnd = worldRayFromScreenPosition.origin + worldRayFromScreenPosition.direction * 1000.0f;

        btCollisionWorld::ClosestRayResultCallback rayCallback(Utils::glmToBullet(worldRayFromScreenPosition.origin), Utils::glmToBullet(outEnd));
        rayCallback.m_collisionFilterMask = COL_CAR | COL_TRACK | COL_DYNAMIC_TRACK;

        m_pDynamicsWorld->rayTest(Utils::glmToBullet(worldRayFromScreenPosition.origin), Utils::glmToBullet(outEnd), rayCallback);

        if (rayCallback.hasHit()) {
            entityTargeted = true;
            return static_cast<Entity *>(rayCallback.m_collisionObject->getUserPointer());
        } else {
            entityTargeted = false;
            return nullptr;
        }
    }

    void PhysicsEngine::RegisterTrack(const std::shared_ptr<Track> &track) {
        m_track = track;

        for (auto &entity : m_track->entities) {
            int collisionMask = COL_RAY | COL_CAR;
            if (!entity->track_entity->collideable) {
                continue;
            }
            if (entity->track_entity->dynamic) {
                collisionMask |= COL_TRACK;
                // Move Rigid body to correct place in world
                btTransform initialTransform = Utils::MakeTransform(entity->model->geometry->initialPosition, entity->model->geometry->orientation);
                entity->rigidBody->setWorldTransform(initialTransform);
                m_pDynamicsWorld->addRigidBody(entity->rigidBody.get(), COL_DYNAMIC_TRACK, collisionMask);
            } else {
                m_pDynamicsWorld->addRigidBody(entity->rigidBody.get(), COL_TRACK, collisionMask);
            }
        }
    }

    void PhysicsEngine::RegisterVehicle(const std::shared_ptr<Car> &car) {
        car->SetRaycaster(std::make_unique<btDefaultVehicleRaycaster>(m_pDynamicsWorld.get()));
        car->SetVehicle(std::make_unique<btRaycastVehicle>(car->tuning, car->GetVehicleRigidBody(), car->GetRaycaster()));
        car->GetVehicle()->setCoordinateSystem(0, 1, 2);

        m_pDynamicsWorld->getBroadphase()->getOverlappingPairCache()->cleanProxyFromPairs(car->GetVehicleRigidBody()->getBroadphaseHandle(), m_pDynamicsWorld->getDispatcher());
        m_pDynamicsWorld->addRigidBody(car->GetVehicleRigidBody(), COL_CAR, COL_TRACK | COL_RAY | COL_DYNAMIC_TRACK | COL_VROAD | COL_CAR);
        m_pDynamicsWorld->addVehicle(car->GetVehicle());

        // Wire up the wheels
        float wheelRadius    = car->vehicleProperties.wheelRadius;
        btScalar sRestLength = car->vehicleProperties.suspensionRestLength;
        btVector3 wheelDirectionCS0(0, -1, 0);
        btVector3 wheelAxleCS(-1, 0, 0);
        // Fronties
        car->GetVehicle()->addWheel(Utils::glmToBullet(car->leftFrontWheelModel.geometry->position), wheelDirectionCS0, wheelAxleCS, sRestLength, wheelRadius, car->tuning, true);
        car->GetVehicle()->addWheel(Utils::glmToBullet(car->rightFrontWheelModel.geometry->position), wheelDirectionCS0, wheelAxleCS, sRestLength, wheelRadius, car->tuning, true);
        // Rearies
        car->GetVehicle()->addWheel(Utils::glmToBullet(car->leftRearWheelModel.geometry->position), wheelDirectionCS0, wheelAxleCS, sRestLength, wheelRadius, car->tuning, false);
        car->GetVehicle()->addWheel(Utils::glmToBullet(car->rightRearWheelModel.geometry->position), wheelDirectionCS0, wheelAxleCS, sRestLength, wheelRadius, car->tuning, false);

        for (auto wheelIdx = 0; wheelIdx < car->GetVehicle()->getNumWheels(); ++wheelIdx) {
            btWheelInfo &wheel               = car->GetVehicle()->getWheelInfo(wheelIdx);
            wheel.m_suspensionStiffness      = car->vehicleProperties.suspensionStiffness;
            wheel.m_wheelsDampingRelaxation  = car->vehicleProperties.suspensionDamping;
            wheel.m_wheelsDampingCompression = car->vehicleProperties.suspensionCompression;
            wheel.m_frictionSlip             = car->vehicleProperties.wheelFriction;
            wheel.m_rollInfluence            = car->vehicleProperties.rollInfluence;
        }

        // Add vehicle to active vehicles list so they can be updated on step of physics engine
        m_activeVehicles.push_back(car);
    }

    btDiscreteDynamicsWorld *PhysicsEngine::GetDynamicsWorld() {
        return m_pDynamicsWorld.get();
    }

    PhysicsEngine::~PhysicsEngine() {
        for (auto &car : m_activeVehicles) {
            m_pDynamicsWorld->removeVehicle(car->GetVehicle());
        }
        for (auto &entity : m_track->entities) {
            m_pDynamicsWorld->removeRigidBody(entity->rigidBody.get());
        }
    }

    void PhysicsEngine::_GenerateVroadBarriers() {
        /*if ((m_track->nfsVersion == NFS_3 || m_track->nfsVersion == NFS_4) && !Config::get().sparkMode)
        {
            uint32_t nVroad = std::get<std::shared_ptr<NFS3_4_DATA::TRACK>>(m_track->trackData)->col.vroadHead.nrec;
            for (uint32_t vroad_Idx = 0; vroad_Idx < nVroad; ++vroad_Idx)
            {
                if (vroad_Idx < nVroad - 1)
                {
                    glm::quat rotationMatrix = glm::normalize(glm::quat(glm::vec3(-SIMD_PI / 2, 0, 0)));

                    COLVROAD curVroad = std::get<std::shared_ptr<NFS3_4_DATA::TRACK>>(m_track->trackData)->col.vroad[vroad_Idx];
                    COLVROAD nextVroad = std::get<std::shared_ptr<NFS3_4_DATA::TRACK>>(m_track->trackData)->col.vroad[vroad_Idx + 1];
                    INTPT curVroadRefPt = curVroad.refPt;
                    INTPT nextVroadRefPt = nextVroad.refPt;

                    // Transform NFS3/4 coords into ONFS 3d space
                    glm::vec3 curVroadPoint = rotationMatrix * Utils::FixedToFloat(Utils::PointToVec(curVroadRefPt)) / NFS3_SCALE_FACTOR;
                    glm::vec3 nextVroadPoint = rotationMatrix *Utils::FixedToFloat(Utils::PointToVec(nextVroadRefPt)) / NFS3_SCALE_FACTOR;

                    // Get VROAD right vector
                    glm::vec3 curVroadRightVec = rotationMatrix * Utils::PointToVec(curVroad.right) / 128.f;
                    glm::vec3 nextVroadRightVec = rotationMatrix * Utils::PointToVec(nextVroad.right) / 128.f;

                    glm::vec3 curVroadLeftWall = ((curVroad.leftWall / 65536.0f) / 10.f) * curVroadRightVec;
                    glm::vec3 curVroadRightWall = ((curVroad.rightWall / 65536.0f) / 10.f) * curVroadRightVec;
                    glm::vec3 nextVroadLeftWall = ((nextVroad.leftWall / 65536.0f) / 10.f) * nextVroadRightVec;
                    glm::vec3 nextVroadRightWall = ((nextVroad.rightWall / 65536.0f) / 10.f) * nextVroadRightVec;

                    bool useFullVroad = Config::get().useFullVroad;

                    // Get edges of road by adding to vroad right vector to vroad reference point
                    glm::vec3 curLeftVroadEdge = curVroadPoint - (useFullVroad ? curVroadLeftWall : curVroadRightVec);
                    glm::vec3 curRightVroadEdge = curVroadPoint + (useFullVroad ? curVroadRightWall : curVroadRightVec);
                    glm::vec3 nextLeftVroadEdge = nextVroadPoint - (useFullVroad ? nextVroadLeftWall : nextVroadRightVec);
                    glm::vec3 nextRightVroadEdge = nextVroadPoint + (useFullVroad ? nextVroadRightWall : nextVroadRightVec);

                    // Add them to the physics world
                    Entity leftVroadBarrier = Entity(99, 99, NFSVer::NFS_3, VROAD, nullptr, 0, curLeftVroadEdge, nextLeftVroadEdge);
                    Entity rightVroadBarrier = Entity(99, 99, NFSVer::NFS_3, VROAD, nullptr, 0, curRightVroadEdge, nextRightVroadEdge);
                    Entity vroadCeiling = Entity(99, 99, NFSVer::NFS_3, VROAD_CEIL, nullptr, 0, curLeftVroadEdge, nextLeftVroadEdge,
        curRightVroadEdge, nextRightVroadEdge); m_pDynamicsWorld->addRigidBody(leftVroadBarrier.rigidBody, COL_VROAD, COL_RAY | COL_CAR);
                    m_pDynamicsWorld->addRigidBody(rightVroadBarrier.rigidBody, COL_VROAD, COL_RAY | COL_CAR);
                    m_pDynamicsWorld->addRigidBody(vroadCeiling.rigidBody, COL_VROAD_CEIL, COL_RAY);

                    // Keep track of them so can clean up later
                    m_track->vroadBarriers.emplace_back(leftVroadBarrier);
                    m_track->vroadBarriers.emplace_back(rightVroadBarrier);
                    m_track->vroadBarriers.emplace_back(vroadCeiling);
                }
            }
        }*/
    }
} // namespace OpenNFS
