#include <game_config.hpp>
#include <engine.hpp>
#include <ui/ui_manager.hpp>

namespace vke_common
{
    GameConfig *GameConfig::instance = nullptr;
    Engine *Engine::instance = nullptr;

    bool Engine::Update()
    {
        const EngineState state = vke_common::EngineStateManager::GetState();
        if (state == EngineState::Terminated)
        {
            Shutdown();
            return false;
        }

        vke_common::TimeManager::Update();
        vke_common::SceneManager::GetInstance()->ProcessDestroyRequests();

        if (state != EngineState::Paused)
            UpdateSimulation(vke_common::TimeManager::GetDeltaTime(), fixedUpdateAccumulator);
        UpdatePresentation();
        vke_common::InputManager::EndFrame();
        return true;
    }

    void Engine::UpdateSimulation(float deltaTime, float &fixedUpdateAccumulator)
    {
        vke_common::SceneManager::GetInstance()->Update(deltaTime);
        fixedUpdateAccumulator += deltaTime;
        const float fixedStepTime = vke_physics::PhysicsManager::GetConfig().stepTime;
        while (fixedUpdateAccumulator >= fixedStepTime)
        {
            FixedUpdate(fixedStepTime);
            fixedUpdateAccumulator -= fixedStepTime;
        }
        vke_common::SceneManager::GetInstance()->LateUpdate(deltaTime);
        if (vke_common::SceneManager::IsRunning())
            vke_audio::AudioManager::Update(deltaTime);
    }

    void Engine::UpdatePresentation(const vke_ui::UIPointerInput *pointer)
    {
        auto *renderer = vke_render::Renderer::GetInstance();
        const glm::vec2 viewportSize(renderer->context->width, renderer->context->height);
        auto *uiManager = vke_ui::UIManager::GetInstance();
        uiManager->Update(viewportSize);
        const auto input = pointer ? *pointer : vke_ui::UIManager::BuildUIPointerInput(
            vke_render::RenderEnvironment::GetInstance()->window, viewportSize);
        uiManager->ProcessPointer(viewportSize, input);
        renderer->Update();
    }

    void Engine::FixedUpdate(float deltaTime)
    {
        vke_common::SceneManager::GetInstance()->FixedUpdate(deltaTime);
        if (vke_common::SceneManager::IsRunning())
            vke_physics::PhysicsManager::FixedUpdate(deltaTime);
    }

    void Engine::MainLoop()
    {
        while (!glfwWindowShouldClose(vke_render::RenderEnvironment::GetInstance()->window))
        {
            glfwPollEvents();
            Update();
        }
        vke_render::RenderEnvironment::WaitIdle();
    }
}
