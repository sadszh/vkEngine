#ifndef TRANSPARENT_PASS_H
#define TRANSPARENT_PASS_H

#include <render/camera.hpp>
#include <render/hdr_color.hpp>
#include <render/light_manager.hpp>
#include <render/renderinfo.hpp>
#include <render/shadow_manager.hpp>
#include <render/skybox.hpp>
#include <render/subpass.hpp>
#include <ds/id_allocator.hpp>

namespace vke_render
{
    class TransparentPass : public RenderPassBase
    {
    public:
        TransparentPass(RenderContext *ctx, VkDescriptorSet *globalDescriptorSets,
                        LightManager *lightManager, SkyboxManager *skyboxManager,
                        ShadowManager *shadowManager, HDRColorManager *hdrColorManager,
                        const CameraInfo *cameraInfo);
        ~TransparentPass() override = default;

        void Reset() override
        {
            units.clear();
            materialStates.clear();
            unitAllocator = vke_ds::NaiveIDAllocator<vke_ds::id64_t>(1);
        }

        void Init(int subpassID, FrameGraph &frameGraph,
                  std::map<std::string, vke_ds::id32_t> &blackboard,
                  ResourceNodeIDMap &currentResourceNodeID) override;
        vke_ds::id64_t AddUnit(std::shared_ptr<Material> &material, RenderUnit *unit);
        void RemoveUnit(vke_ds::id64_t id);
        void Render(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer,
                    uint32_t currentFrame, uint32_t imageIndex) override;

    private:
        struct MaterialState
        {
            ~MaterialState()
            {
                VulkanResourceManager::FreeDescriptorSet(commonDescriptorSet);
                for (auto set : environmentDescriptorSets)
                    VulkanResourceManager::FreeDescriptorSet(set);
            }

            std::shared_ptr<Material> material;
            std::unique_ptr<GraphicsPipeline> pipeline;
            VkDescriptorSet commonDescriptorSet = VK_NULL_HANDLE;
            VkDescriptorSet environmentDescriptorSets[MAX_FRAMES_IN_FLIGHT]{};
        };

        struct UnitEntry
        {
            Material *material;
            RenderUnit *unit;
        };

        struct SortedUnit
        {
            UnitEntry *entry;
            float viewDepth;
        };

        LightManager *lightManager;
        SkyboxManager *skyboxManager;
        ShadowManager *shadowManager;
        HDRColorManager *hdrColorManager;
        const CameraInfo *cameraInfo;
        std::shared_ptr<Texture2D> brdfLUT;
        std::map<Material *, std::unique_ptr<MaterialState>> materialStates;
        std::map<vke_ds::id64_t, UnitEntry> units;
        vke_ds::NaiveIDAllocator<vke_ds::id64_t> unitAllocator;
        vke_ds::id32_t taskNodeID;
        uint32_t hdrColorImageIndex;

        void constructFrameGraph(FrameGraph &frameGraph,
                                 std::map<std::string, vke_ds::id32_t> &blackboard,
                                 ResourceNodeIDMap &currentResourceNodeID);
        void registerMaterial(std::shared_ptr<Material> &material);
        void createGraphicsPipeline(MaterialState &state);
        void updateEnvironmentDescriptorSet(MaterialState &state, uint32_t currentFrame);
        void onTransientResourcesReady(FrameGraph &frameGraph, uint32_t currentFrame);
    };
}

#endif
