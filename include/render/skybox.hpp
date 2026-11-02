#ifndef SKYBOX_H
#define SKYBOX_H

#include <render/cubemap.hpp>
#include <render/frame_graph.hpp>
#include <render/light_manager.hpp>
#include <render/render_config.hpp>
#include <render/texture.hpp>
#include <array>

namespace vke_render
{
    struct AtmospherePushConstants
    {
        glm::vec4 mainLightDir;
        glm::vec4 mainLightColorIntensity;
    };

    class SkyboxManager
    {
    public:
        SkyboxManager(LightManager *lightManager, const AtmosphereParameter &atmosphereParameter,
                      const SkyLUTConfig &lutConfig)
            : lightManager(lightManager), atmosphereParameter(atmosphereParameter), lutConfig(lutConfig)
        {
            initResources();
            createDescriptorSet();
        }

        void ConstructFrameGraph(FrameGraph &frameGraph,
                                 std::map<std::string, vke_ds::id32_t> &blackboard,
                                 ResourceNodeIDMap &currentResourceNodeID)
        {
            constructFrameGraph(frameGraph, blackboard, currentResourceNodeID);
        }

        CubeMap *GetSkyLUT(uint32_t currentFrame) { return skyLUTs[currentFrame].get(); }
        CubeMap *GetSkyIrradianceLUT(uint32_t currentFrame) { return skyIrradianceLUTs[currentFrame].get(); }
        CubeMap *GetSkySpecularLUT(uint32_t currentFrame) { return skySpecularLUTs[currentFrame].get(); }
        Texture2D *GetTransmittanceLUT() { return transmittanceLUT.get(); }
        Texture2D *GetScatteringLUT() { return scatteringLUT.get(); }
        DeviceBuffer *GetAtmosphereParamBuffer() { return atmosphereParamBuffer.get(); }
        void Update(float cameraWorldHeight);
        const AtmospherePushConstants &GetAtmospherePushConstants() const { return atmospherePushConstants; }

    private:
        static constexpr uint32_t SKY_IRRADIANCE_CUBE_SIZE = 64;
        static constexpr uint32_t SKY_VIEW_CUBE_SIZE = 256;
        static constexpr uint32_t SKY_SPECULAR_CUBE_SIZE = 128;
        static constexpr uint32_t SKY_SPECULAR_MIP_LEVELS = 6;
        static constexpr uint32_t TRANSMITTANCE_LUT_WIDTH = 256;
        static constexpr uint32_t TRANSMITTANCE_LUT_HEIGHT = 64;
        static constexpr uint32_t SCATTERING_LUT_SIZE = 32;

        struct alignas(16) SkyLUTPushConstants
        {
            AtmospherePushConstants atmosphere;
            glm::vec4 groundColorBrightness;
            float cameraHeight;
            float groundHorizonSin;
            float groundCurve;
        };

        LightManager *lightManager;
        AtmospherePushConstants atmospherePushConstants{
            glm::normalize(glm::vec4(1, 2, 0, 0)),
            glm::vec4(1.0f, 1.0f, 1.0f, 7.4f)};
        AtmosphereParameter atmosphereParameter;
        SkyLUTConfig lutConfig;
        SkyLUTPushConstants lutParameters{};
        bool hasLUTParameters = false;
        std::array<bool, MAX_FRAMES_IN_FLIGHT> lutNeedsUpdate{};
        std::unique_ptr<ComputePipeline> skyLUTGenerationPipeline;
        std::unique_ptr<ComputePipeline> iblLUTGenerationPipeline;
        std::unique_ptr<ComputePipeline> atmosphereLUTGenerationPipeline;
        std::unique_ptr<DeviceBuffer> atmosphereParamBuffer;
        std::unique_ptr<Texture2D> transmittanceLUT;
        std::unique_ptr<Texture2D> scatteringLUT;
        std::unique_ptr<CubeMap> skyLUTs[MAX_FRAMES_IN_FLIGHT];
        std::unique_ptr<CubeMap> skyIrradianceLUTs[MAX_FRAMES_IN_FLIGHT];
        std::unique_ptr<CubeMap> skySpecularLUTs[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSet skyLUTDescriptorSets[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSet skyIBLDescriptorSets[MAX_FRAMES_IN_FLIGHT];

        void constructFrameGraph(FrameGraph &frameGraph,
                                 std::map<std::string, vke_ds::id32_t> &blackboard,
                                 ResourceNodeIDMap &currentResourceNodeID);
        void initResources();
        void createDescriptorSet();
        void updateAtmospherePushConstants();
        void generateAtmosphereLUTs();
        void generateLUT(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex);
        void generateIBLLUT(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex);
    };
}

#endif
