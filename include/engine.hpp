#ifndef ENGINE_H
#define ENGINE_H

#include <game_config.hpp>
#include <render/render.hpp>
#include <physics/physics.hpp>
#include <audio/audio_manager.hpp>
#include <scene.hpp>
#include <event.hpp>
#include <input.hpp>
#include <engine_state.hpp>
#include <time.hpp>
#include <script.hpp>
#include <ui/ui_manager.hpp>

namespace vke_common
{
    class Engine
    {
    private:
        static Engine *instance;
        Engine() : fixedUpdateAccumulator(0.0f) {}
        ~Engine() {}
        Engine(const Engine &);
        Engine &operator=(const Engine);

        float fixedUpdateAccumulator;

    public:
        static Engine *GetInstance()
        {
            VKE_FATAL_IF(instance == nullptr, "Engine not initialized!")
            return instance;
        }

        static Engine *Init(GLFWwindow *window,
                            const GameConfig &gameConfig,
                            vke_render::RenderContext *ctx,
                            std::vector<vke_render::PassType> &passes,
                            std::vector<std::unique_ptr<vke_render::RenderPassBase>> &customPasses)
        {
            instance = new Engine();
            EventSystem::Init();
            TimeManager::Init();
            InputManager::Init(window);
            EngineStateManager::Init();
            vke_audio::AudioManager::Init();
            vke_render::RenderEnvironment::Init(window, gameConfig.enableVulkanValidationLayers);
            vke_render::VulkanResourceManager::Init();
            if (auto result = AssetManager::Init(std::make_unique<AssetDBJSON>(Paths::Project(PathFromUTF8(gameConfig.assetLUTPath)), CUSTOM_ASSET_ID_ST)); !result)
                VKE_FATAL("{}", result.error())
            vke_physics::PhysicsManager::Init(gameConfig.physicsConfig);
            if (ctx == nullptr)
                ctx = &(vke_render::RenderEnvironment::GetInstance()->rootRenderContext);
            vke_render::Renderer::Init(ctx, passes, customPasses, gameConfig.renderConfig);
            ScriptManager::Init();
            SceneManager::Init();
            vke_ui::UIManager::Init(SceneManager::GetInstance()->registry);
            return instance;
        }

        static void Shutdown()
        {
            vke_render::Renderer::Shutdown();
        }

        // Call between frames. Resume explicitly after loading the next scene.
        static void Reset()
        {
            SceneManager::Reset();
            instance->fixedUpdateAccumulator = 0.0f;
            InputManager::Reset();
            TimeManager::Reset();
        }

        static void WaitIdle()
        {
            vke_render::Renderer::WaitIdle();
        }

        static void Dispose()
        {
            vke_ui::UIManager::Dispose();
            SceneManager::Dispose();
            ScriptManager::Dispose();
            vke_render::Renderer::Dispose();
            vke_physics::PhysicsManager::Dispose();
            AssetManager::Dispose();
            vke_render::VulkanResourceManager::Dispose();
            vke_render::RenderEnvironment::Dispose();
            vke_audio::AudioManager::Dispose();
            EngineStateManager::Dispose();
            InputManager::Dispose();
            TimeManager::Dispose();
            EventSystem::Dispose();
            delete instance;
        }

        static void OnWindowResize(GLFWwindow *window, int width, int height)
        {
            EventSystem::DispatchEvent(EVENT_WINDOW_RESIZE, nullptr);
        }

        bool Update();

        static void UpdateSimulation(float deltaTime, float &fixedUpdateAccumulator);
        static void UpdatePresentation(const vke_ui::UIPointerInput *pointer = nullptr);
        static void FixedUpdate(float deltaTime);

        void MainLoop();
    };
};

#endif
