#ifndef GBUFFER_PASS_H
#define GUBFFER_PASS_H

#include <render/gbuffer.hpp>
#include <render/camera.hpp>
#include <render/renderinfo.hpp>
#include <render/subpass.hpp>

namespace vke_render
{

    class GBufferPass : public RenderPassBase
    {
    public:
        GBufferPass(RenderContext *ctx, VkDescriptorSet *globalDescriptorSets, const CameraInfo *cameraInfo)
            : RenderPassBase(GBUFFER_PASS, ctx, globalDescriptorSets), cameraInfo(cameraInfo) {}

        void Init(int subpassID,
                  FrameGraph &frameGraph,
                  std::map<std::string, vke_ds::id32_t> &blackboard,
                  ResourceNodeIDMap &currentResourceNodeID) override
        {
            RenderPassBase::Init(subpassID, frameGraph, blackboard, currentResourceNodeID);
            gbuffer = GBuffer::Init(context->width, context->height);
            constructFrameGraph(frameGraph, blackboard, currentResourceNodeID);
        }

        ~GBufferPass()
        {
            GBuffer::Dispose();
        }

        void Reset() override
        {
            renderInfoMap.clear();
        }

        void RegisterMaterial(std::shared_ptr<Material> &material, bool isSkin = false)
        {
            auto &rMap = renderInfoMap;
            Material *matp = material.get();
            if (rMap.find(matp) != rMap.end())
                return;

            RenderInfo *info = new RenderInfo(material);
            createGraphicsPipeline(*info, isSkin);
            rMap[matp] = std::move(std::unique_ptr<RenderInfo>(info));
            // return id;
        }

        vke_ds::id64_t AddUnit(std::shared_ptr<Material> &material, RenderUnit *unit, bool isSkin = false)
        {
            RegisterMaterial(material, isSkin);
            return renderInfoMap[material.get()]->AddUnit(unit);
        }

        void RemoveUnit(Material *material, vke_ds::id64_t id)
        {
            renderInfoMap[material]->RemoveUnit(id);
            // TODO renderInfoMap[material].size() == 0
        }

        void Render(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
        void OnWindowResize(FrameGraph &frameGraph, RenderContext *ctx) override;

    private:
        const CameraInfo *cameraInfo;
        std::map<Material *, std::unique_ptr<RenderInfo>> renderInfoMap;
        GBuffer *gbuffer;
        vke_ds::id32_t gbufferTaskNodeID;

        void constructFrameGraph(FrameGraph &frameGraph,
                                 std::map<std::string, vke_ds::id32_t> &blackboard,
                                 ResourceNodeIDMap &currentResourceNodeID);
        void createGraphicsPipeline(RenderInfo &renderInfo, bool isSkin);
        void onTransientResourcesReady(FrameGraph &frameGraph, uint32_t currentFrame);
    };
}

#endif
