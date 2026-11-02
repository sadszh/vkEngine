#ifndef RENDERER_H
#define RENDERER_H

#include <render/skybox.hpp>
#include <render/gbuffer_pass.hpp>
#include <render/shadow_pass.hpp>
#include <render/ssao.hpp>
#include <render/deferred_lighting.hpp>
#include <render/atmosphere_pass.hpp>
#include <render/transparent_pass.hpp>
#include <render/bloom.hpp>
#include <render/tone_mapping.hpp>
#include <render/skybox_render.hpp>
#include <render/hdr_color.hpp>
#include <render/layered_2d.hpp>
#include <editor/wireframe_collision_pass.hpp>
#include <render/light_manager.hpp>
#include <render/camera.hpp>
#include <event.hpp>

namespace vke_render
{
    const uint32_t GLOBAL_DESCRIPTOR_SET_NO_LIGHT = 0;
    const uint32_t GLOBAL_DESCRIPTOR_SET_LIGHT = 1;

    class Renderer
    {
    private:
        static Renderer *instance;
        Renderer()
            : cameraIDAllocator(1), currentCamera(1), cameraInfoUpdateCnt(0) {};
        ~Renderer() {}

    public:
        RenderContext *context;
        uint32_t currentFrame;
        uint32_t passcnt;

        vke_ds::id32_t currentCamera;
        std::unique_ptr<LightManager> lightManager;
        vke_common::EventHub<glm::vec2> resizeEventHub;

        static Renderer *GetInstance()
        {
            VKE_FATAL_IF(instance == nullptr, "Renderer not initialized!")
            return instance;
        }

        static Renderer *Init(RenderContext *ctx,
                              std::vector<PassType> &passes,
                              std::vector<std::unique_ptr<RenderPassBase>> &customPasses,
                              const RenderConfig &renderConfig)
        {
            instance = new Renderer();

            instance->context = ctx;
            instance->currentFrame = 0;
            instance->passcnt = passes.size();

            ctx->resizeEventHub->AddEventListener(instance,
                                                  vke_common::EventHub<RenderContext>::callback_t(OnWindowResize));

            for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
                instance->camInfoBuffers.emplace_back(sizeof(CameraInfo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

            instance->blackboard.clear();
            instance->currentResourceNodeID.clear();
            instance->constructFrameGraph(instance->blackboard, instance->currentResourceNodeID);

            instance->lightManager = std::make_unique<LightManager>(
                ctx, *(instance->frameGraph), &instance->hostCameraInfo, renderConfig.directionalShadow);

            instance->initDescriptorSet();

            instance->skyboxManager = std::make_unique<SkyboxManager>(
                instance->lightManager.get(),
                renderConfig.atmosphere, renderConfig.skyLUT);
            instance->skyboxManager->ConstructFrameGraph(*(instance->frameGraph), instance->blackboard, instance->currentResourceNodeID);
            instance->lightManager->ConstructFrameGraph(*(instance->frameGraph), instance->blackboard, instance->currentResourceNodeID);

            int customPassID = 0;
            for (int i = 0; i < passes.size(); i++)
            {
                PassType pass = passes[i];
                // RenderPassBase *customPass;
                switch (pass)
                {
                case CUSTOM_RENDERER:
                {
                    std::unique_ptr<RenderPassBase> &customPass = customPasses[customPassID++];
                    customPass->Init(i, *(instance->frameGraph), instance->blackboard, instance->currentResourceNodeID);
                    instance->subPasses.push_back(std::move(customPass));
                    break;
                }
                case GBUFFER_PASS:
                {
                    std::unique_ptr<GBufferPass> gbufferPass = std::make_unique<GBufferPass>(ctx, instance->globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_NO_LIGHT],
                                                                                         &instance->hostCameraInfo);
                    gbufferPass->Init(i, *(instance->frameGraph), instance->blackboard, instance->currentResourceNodeID);
                    instance->subPassMap[GBUFFER_PASS] = instance->subPasses.size();
                    instance->subPasses.push_back(std::move(gbufferPass));
                    break;
                }
                case SHADOW_PASS:
                {
                    std::unique_ptr<ShadowPass> shadowPass = std::make_unique<ShadowPass>(
                        ctx, instance->globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_NO_LIGHT],
                        instance->lightManager->GetShadowManager());
                    shadowPass->Init(i, *(instance->frameGraph), instance->blackboard, instance->currentResourceNodeID);
                    instance->subPassMap[SHADOW_PASS] = instance->subPasses.size();
                    instance->subPasses.push_back(std::move(shadowPass));
                    break;
                }
                case SSAO_PASS:
                {
                    const nlohmann::json &ssaoConfigJSON = renderConfig.sourceJSON.value("ssao", nlohmann::json::object());
                    std::unique_ptr<SSAOPass> ssaoPass = std::make_unique<SSAOPass>(ctx, instance->globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_NO_LIGHT], ssaoConfigJSON);
                    ssaoPass->Init(i, *(instance->frameGraph), instance->blackboard, instance->currentResourceNodeID);
                    instance->subPassMap[SSAO_PASS] = instance->subPasses.size();
                    instance->subPasses.push_back(std::move(ssaoPass));
                    break;
                }
                case DEFERRED_LIGHTING_PASS:
                {
                    std::unique_ptr<DeferredLightingPass> lightingPass = std::make_unique<DeferredLightingPass>(ctx, instance->globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_LIGHT],
                                                                                                                instance->lightManager.get(), instance->skyboxManager.get(),
                                                                                                                instance->hdrColorManager.get(), instance->lightManager->GetShadowManager());
                    auto ssaoPassIt = instance->subPassMap.find(SSAO_PASS);
                    if (ssaoPassIt != instance->subPassMap.end())
                    {
                        SSAOPass *ssaoPass = static_cast<SSAOPass *>(instance->subPasses[ssaoPassIt->second].get());
                        lightingPass->SetSSAOInput(ssaoPass->GetOutputSampler(),
                                                   [ssaoPass](uint32_t currentFrame)
                                                   { return ssaoPass->GetOutputImageView(currentFrame); });
                    }
                    lightingPass->Init(i, *(instance->frameGraph), instance->blackboard, instance->currentResourceNodeID);
                    instance->subPassMap[DEFERRED_LIGHTING_PASS] = instance->subPasses.size();
                    instance->subPasses.push_back(std::move(lightingPass));
                    break;
                }
                case SKYBOX_RENDERER:
                {
                    std::unique_ptr<SkyboxRenderer> skyboxRenderer = std::make_unique<SkyboxRenderer>(ctx, instance->globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_NO_LIGHT], instance->skyboxManager.get(), instance->hdrColorManager.get());
                    skyboxRenderer->Init(i, *(instance->frameGraph), instance->blackboard, instance->currentResourceNodeID);
                    instance->subPassMap[SKYBOX_RENDERER] = instance->subPasses.size();
                    instance->subPasses.push_back(std::move(skyboxRenderer));
                    break;
                }
                case ATMOSPHERE_PASS:
                {
                    std::unique_ptr<AtmospherePass> atmospherePass = std::make_unique<AtmospherePass>(
                        ctx, instance->globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_NO_LIGHT],
                        instance->skyboxManager.get(), instance->hdrColorManager.get());
                    atmospherePass->Init(i, *(instance->frameGraph), instance->blackboard, instance->currentResourceNodeID);
                    instance->subPassMap[ATMOSPHERE_PASS] = instance->subPasses.size();
                    instance->subPasses.push_back(std::move(atmospherePass));
                    break;
                }
                case TRANSPARENT_PASS:
                {
                    auto transparentPass = std::make_unique<TransparentPass>(
                        ctx, instance->globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_LIGHT],
                        instance->lightManager.get(), instance->skyboxManager.get(),
                        instance->lightManager->GetShadowManager(), instance->hdrColorManager.get(),
                        &instance->hostCameraInfo);
                    transparentPass->Init(i, *(instance->frameGraph), instance->blackboard, instance->currentResourceNodeID);
                    instance->subPassMap[TRANSPARENT_PASS] = instance->subPasses.size();
                    instance->subPasses.push_back(std::move(transparentPass));
                    break;
                }
                case WIREFRAME_COLLISION_PASS:
                {
                    auto wireframePass = std::make_unique<vke_editor::WireframeCollisionPass>(ctx, instance->globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_NO_LIGHT]);
                    wireframePass->Init(i, *(instance->frameGraph), instance->blackboard, instance->currentResourceNodeID);
                    instance->subPassMap[WIREFRAME_COLLISION_PASS] = instance->subPasses.size();
                    instance->subPasses.push_back(std::move(wireframePass));
                    break;
                }
                case BLOOM_PASS:
                {
                    const nlohmann::json &bloomConfigJSON = renderConfig.sourceJSON.value("bloom", nlohmann::json::object());
                    std::unique_ptr<BloomPass> bloomPass = std::make_unique<BloomPass>(ctx, instance->globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_NO_LIGHT], instance->hdrColorManager.get(), bloomConfigJSON);
                    bloomPass->Init(i, *(instance->frameGraph), instance->blackboard, instance->currentResourceNodeID);
                    instance->subPassMap[BLOOM_PASS] = instance->subPasses.size();
                    instance->subPasses.push_back(std::move(bloomPass));
                    break;
                }
                case TONE_MAPPING_PASS:
                {
                    const nlohmann::json &toneMappingConfigJSON = renderConfig.sourceJSON.value("toneMapping", nlohmann::json::object());
                    std::unique_ptr<ToneMappingPass> toneMappingPass = std::make_unique<ToneMappingPass>(ctx, instance->globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_NO_LIGHT], instance->hdrColorManager.get(), toneMappingConfigJSON);
                    toneMappingPass->Init(i, *(instance->frameGraph), instance->blackboard, instance->currentResourceNodeID);
                    instance->subPassMap[TONE_MAPPING_PASS] = instance->subPasses.size();
                    instance->subPasses.push_back(std::move(toneMappingPass));
                    break;
                }
                case LAYERED_2D_RENDERER:
                {
                    auto layered2DRenderer = std::make_unique<Layered2DRenderer>(
                        ctx, instance->globalDescriptorSets[GLOBAL_DESCRIPTOR_SET_NO_LIGHT],
                        &instance->quadManager);
                    layered2DRenderer->Init(i, *(instance->frameGraph), instance->blackboard, instance->currentResourceNodeID);
                    instance->subPassMap[LAYERED_2D_RENDERER] = instance->subPasses.size();
                    instance->subPasses.push_back(std::move(layered2DRenderer));
                    break;
                }

                default:
                    break;
                }
            }
            instance->frameGraph->Compile();
            return instance;
        }

        static void Shutdown()
        {
        }

        static void Reset();

        static void WaitIdle()
        {
            VKE_LOG_INFO("WAIT_IDLE0")
            vkDeviceWaitIdle(globalLogicalDevice);
            VKE_LOG_INFO("WAIT_IDLE1")
            CPUCommandQueue *cpuQueue = (CPUCommandQueue *)RenderEnvironment::GetInstance()->commandQueues[CPU_QUEUE].get();
            cpuQueue->WaitIdle();
            VKE_LOG_INFO("WAIT_IDLE2")
        }

        static void Dispose()
        {
            instance->cleanup();
            delete instance;
        }

        static vke_ds::id32_t RegisterCamera(std::function<void()> callback)
        {
            vke_ds::id32_t ret = instance->cameraIDAllocator.Alloc();
            instance->cameras[ret] = callback;
            if (instance->cameras.size() == 1 || ret == instance->currentCamera)
            {
                instance->currentCamera = ret;
                callback();
            }
            return ret;
        }

        static void RemoveCamera(vke_ds::id32_t id)
        {
            instance->cameras.erase(id);
            if (instance->currentCamera == id)
            {
                if (!instance->cameras.empty())
                    SetCurrentCamera(instance->cameras.begin()->first);
                else
                    instance->currentCamera = std::numeric_limits<vke_ds::id32_t>::max();
            }
        }

        static void SetCurrentCamera(vke_ds::id32_t id)
        {
            if (id != instance->currentCamera)
            {
                auto it = instance->cameras.find(id);
                if (it != instance->cameras.end())
                {
                    instance->currentCamera = id;
                    it->second();
                }
            }
        }

        static void UpdateCameraInfo(CameraInfo cameraInfo)
        {
            instance->hostCameraInfo = cameraInfo;
            instance->cameraInfoUpdateCnt = 2;
        }

        static void AddRenderUpdateCallback(vke_ds::id64_t id, std::function<void(uint32_t)> callback)
        {
            instance->renderUpdateCallbacks[id] = callback;
        }

        static void RemoveRenderUpdateCallback(vke_ds::id64_t id)
        {
            instance->renderUpdateCallbacks.erase(id);
        }

        static GBufferPass *GetGBufferPass()
        {
            return static_cast<GBufferPass *>(instance->subPasses[instance->subPassMap[GBUFFER_PASS]].get());
        }

        static ShadowPass *GetShadowPass()
        {
            auto it = instance->subPassMap.find(SHADOW_PASS);
            if (it == instance->subPassMap.end())
                return nullptr;
            return static_cast<ShadowPass *>(instance->subPasses[it->second].get());
        }

        static TransparentPass *GetTransparentPass()
        {
            auto it = instance->subPassMap.find(TRANSPARENT_PASS);
            if (it == instance->subPassMap.end())
                return nullptr;
            return static_cast<TransparentPass *>(instance->subPasses[it->second].get());
        }

        static Layered2DRenderer *GetLayered2DRenderer()
        {
            auto it = instance->subPassMap.find(LAYERED_2D_RENDERER);
            if (it == instance->subPassMap.end())
                return nullptr;
            return static_cast<Layered2DRenderer *>(instance->subPasses[it->second].get());
        }

        static vke_editor::WireframeCollisionPass *GetWireframeCollisionPass()
        {
            auto it = instance->subPassMap.find(WIREFRAME_COLLISION_PASS);
            if (it == instance->subPassMap.end())
                return nullptr;
            return static_cast<vke_editor::WireframeCollisionPass *>(instance->subPasses[it->second].get());
        }

        static QuadManager *GetQuadManager()
        {
            return &instance->quadManager;
        }

        static HDRColorManager *GetHDRColorManager()
        {
            return instance->hdrColorManager.get();
        }

        static const CameraInfo &GetCameraInfo()
        {
            return instance->hostCameraInfo;
        }

        static void OnWindowResize(void *listener, RenderContext *ctx)
        {
            instance->recreate(ctx);
            glm::vec2 extent(ctx->width, ctx->height);
            for (auto &subpass : instance->subPasses)
                subpass->OnWindowResize(*instance->frameGraph, ctx);
            instance->frameGraph->MarkNeedRecompile();
            instance->resizeEventHub.DispatchEvent(&extent);
        }

        void Update();

    private:
        vke_ds::id32_t colorAttachmentResourceID;
        vke_ds::id32_t depthAttachmentResourceID;
        DescriptorSetInfo globalDescriptorSetInfos[2]; // 0 no light, 1 light
        VkDescriptorSet globalDescriptorSets[2][MAX_FRAMES_IN_FLIGHT];
        std::vector<std::unique_ptr<RenderPassBase>> subPasses;
        std::map<PassType, int> subPassMap;
        std::unique_ptr<FrameGraph> frameGraph;
        std::unique_ptr<SkyboxManager> skyboxManager;
        std::unique_ptr<HDRColorManager> hdrColorManager;
        QuadManager quadManager;
        std::map<std::string, vke_ds::id32_t> blackboard;
        ResourceNodeIDMap currentResourceNodeID;

        uint32_t cameraInfoUpdateCnt;
        CameraInfo hostCameraInfo;
        std::vector<vke_render::HostCoherentBuffer> camInfoBuffers;
        vke_ds::NaiveIDAllocator<vke_ds::id32_t> cameraIDAllocator;
        std::unordered_map<vke_ds::id32_t, std::function<void()>> cameras;

        std::unordered_map<vke_ds::id64_t, std::function<void(uint32_t)>> renderUpdateCallbacks;

        void initDescriptorSet();
        void constructFrameGraph(std::map<std::string, vke_ds::id32_t> &blackboard,
                                 ResourceNodeIDMap &currentResourceNodeID);
        void cleanup();
        void recreate(RenderContext *ctx);
        void render();
    };
}

#endif
