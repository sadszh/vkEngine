#ifndef EDITOR_RENDERER_H
#define EDITOR_RENDERER_H

#include <render/environment.hpp>
#include <editor/gizmo_renderer.hpp>

namespace vke_editor
{
    class EditorRenderer
    {
    private:
        static EditorRenderer *instance;
        EditorRenderer() = default;
        ~EditorRenderer() {}

    public:
        static EditorRenderer *GetInstance()
        {
            return instance;
        }

        static EditorRenderer *Init(GLFWwindow *window,
                                    vke_render::RenderContext *ctx,
                                    uint32_t sceneViewportWidth,
                                    uint32_t sceneViewportHeight);

        vke_render::RenderContext *GetSceneRenderContext()
        {
            return &sceneRenderContext;
        }

        static void Dispose();

        void ResizeSceneViewport(uint32_t width, uint32_t height);

        VkDescriptorSet GetSceneTexture() const
        {
            return sceneTextureDescriptorSets[currentFrame];
        }

        void BeginFrame();

        void Update(const glm::mat4 &viewProj, std::span<const glm::vec3> lines)
        {
            render(viewProj, lines);
            currentFrame = (currentFrame + 1) % vke_render::MAX_FRAMES_IN_FLIGHT;
        }

        static uint32_t AcquireSceneNextImage(uint32_t currentFrame);
        static void PresentScene(uint32_t currentFrame, uint32_t imageIndex);

    private:
        uint32_t currentFrame;
        GLFWwindow *window;
        vke_render::RenderContext *context;
        vke_render::RenderContext sceneRenderContext;
        vke_common::EventHub<vke_render::RenderContext> sceneResizeEventHub;
        VkImage sceneColorImages[vke_render::MAX_FRAMES_IN_FLIGHT];
        VmaAllocation sceneColorImageVmaAllocations[vke_render::MAX_FRAMES_IN_FLIGHT];
        VkImageView sceneColorImageViews[vke_render::MAX_FRAMES_IN_FLIGHT];
        VkImage sceneDepthImages[vke_render::MAX_FRAMES_IN_FLIGHT];
        VmaAllocation sceneDepthImageVmaAllocations[vke_render::MAX_FRAMES_IN_FLIGHT];
        VkImageView sceneDepthImageViews[vke_render::MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSet sceneTextureDescriptorSets[vke_render::MAX_FRAMES_IN_FLIGHT];
        VkSemaphore sceneImageAvailableSemaphores[vke_render::MAX_FRAMES_IN_FLIGHT];
        VkSemaphore sceneRenderFinishedSemaphores[vke_render::MAX_FRAMES_IN_FLIGHT];
        VkFence sceneInFlightFences[vke_render::MAX_FRAMES_IN_FLIGHT];
        VkCommandPool commandPool;
        VkCommandBuffer commandBuffers[vke_render::MAX_FRAMES_IN_FLIGHT];
        bool sceneTexturesRegistered;
        std::unique_ptr<GizmoRenderer> gizmoRenderer;

        void registerSceneTextures();
        void removeSceneTextures();
        void createSceneRenderContext(uint32_t width, uint32_t height);
        void cleanupSceneRenderContext();
        void render(const glm::mat4 &viewProj, std::span<const glm::vec3> lines);
    };
}

#endif
