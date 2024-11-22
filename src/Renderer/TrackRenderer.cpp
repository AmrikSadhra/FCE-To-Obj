#include "TrackRenderer.h"

namespace OpenNFS {
    void TrackRenderer::Render(const std::vector<std::shared_ptr<CarAgent>> &racers,
                               const BaseCamera &camera,
                               GLuint trackTextureArrayID,
                               const std::vector<std::shared_ptr<Entity>> &visibleEntities,
                               const std::vector<std::shared_ptr<LibOpenNFS::BaseLight>> &lights,
                               const ParamData &userParams,
                               GLuint depthTextureID,
                               float ambientFactor) {
        m_trackShader.use();
        // This shader state doesn't change during a track renderpass
        m_trackShader.setClassic(userParams.useClassicGraphics);
        m_trackShader.loadProjectionViewMatrices(camera.projectionMatrix, camera.viewMatrix);
        m_trackShader.loadSpecular(userParams.trackSpecDamper, userParams.trackSpecReflectivity);
        m_trackShader.bindTextureArray(trackTextureArrayID);
        m_trackShader.loadShadowMapTexture(depthTextureID);
        m_trackShader.loadAmbientFactor(ambientFactor);
        m_trackShader.loadLights(lights);
        // m_trackShader.loadSpotlight(car->leftHeadlight);

        // TODO: Again, super silly.
        // for (auto &light : lights) {
        //    if (light->type == LibOpenNFS::LightType::GLOBAL_LIGHT) {
        //        m_trackShader.loadLightSpaceMatrix(std::static_pointer_cast<GlobalLight>(light)->lightSpaceMatrix);
        //    }
        //}

        // Render the per-trackblock data
        for (auto &entity : visibleEntities) {
            m_trackShader.loadTransformMatrix(entity->ModelMatrix);
            entity->Render();
        }

        m_trackShader.unbind();
        m_trackShader.HotReload();
    }

    void TrackRenderer::RenderLights(const BaseCamera &camera, const std::vector<std::shared_ptr<LibOpenNFS::BaseLight>> &lights) {
        m_billboardShader.use();

        for (auto &light : lights) {
            if (light->type == LibOpenNFS::LightType::TRACK_LIGHT) {
                std::shared_ptr<LibOpenNFS::TrackLight> trackLight = std::static_pointer_cast<LibOpenNFS::TrackLight>(light);
                m_billboardShader.loadMatrices(camera.projectionMatrix, camera.viewMatrix);
                m_billboardShader.loadLight(trackLight);
                // trackLight->model.render();
            }
        }
        m_billboardShader.unbind();
        m_billboardShader.HotReload();
    }

    TrackRenderer::~TrackRenderer() {
        // Cleanup VBOs and shaders
        m_trackShader.cleanup();
        m_billboardShader.cleanup();
    }
} // namespace OpenNFS
