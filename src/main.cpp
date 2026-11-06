#include <engine.hpp>

GLFWwindow *initWindow(int width, int height)
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    return glfwCreateWindow(width, height, "Vulkan window", nullptr, nullptr);
}

int wmain(int argc, wchar_t **argv)
{
    VKE_FATAL_IF(argc != 2, "arg count mismatch")
    const auto configPath = std::filesystem::absolute(std::filesystem::path(argv[1]));
    auto loadedConfig = vke_common::LoadJSON(configPath);
    VKE_FATAL_IF(!loadedConfig, "{}", loadedConfig.error())
    const auto &configJSON = *loadedConfig;
    auto validConfig = vke_common::GameConfig::ValidateJSON(configJSON);
    VKE_FATAL_IF(!validConfig, "{}: {}", vke_common::PathToUTF8(configPath), validConfig.error())
    vke_common::Paths::Init(configPath,
        vke_common::PathFromUTF8(configJSON.value("projectRoot", ".")),
        vke_common::PathFromUTF8(configJSON.value("engineRoot", "")));
    vke_common::GameConfig::Init(configJSON);
    const vke_common::GameConfig *gameConfig = vke_common::GameConfig::GetInstance();

    std::vector<vke_render::PassType> passes = {
        vke_render::SHADOW_PASS,
        vke_render::GBUFFER_PASS,
        vke_render::SSAO_PASS,
        vke_render::DEFERRED_LIGHTING_PASS,
        vke_render::SKYBOX_RENDERER,
        vke_render::ATMOSPHERE_PASS,
        vke_render::TRANSPARENT_PASS,
        vke_render::BLOOM_PASS,
        vke_render::TONE_MAPPING_PASS,
        vke_render::LAYERED_2D_RENDERER};
    std::vector<std::unique_ptr<vke_render::RenderPassBase>> customPasses;

    GLFWwindow *window = initWindow(gameConfig->windowWidth, gameConfig->windowHeight);
    vke_common::Engine *engine = vke_common::Engine::Init(window, *gameConfig, nullptr, passes, customPasses);

    {
        auto sceneData = vke_common::AssetManager::LoadSceneFile(vke_common::Paths::Project(vke_common::PathFromUTF8(gameConfig->defaultScenePath)));
        VKE_FATAL_IF(!sceneData, "Cannot read scene: {}", sceneData.error())
        auto prepared = vke_common::AssetManager::PrepareSceneData(*sceneData);
        VKE_FATAL_IF(!prepared, "Cannot prepare scene: {}", prepared.error())
        auto result = vke_common::SceneManager::Instantiate(*sceneData);
        VKE_FATAL_IF(!result, "Cannot instantiate scene: {}", result.error())
    }

    glfwSetFramebufferSizeCallback(window, vke_common::Engine::OnWindowResize);
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        if (!engine->Update())
            glfwSetWindowShouldClose(vke_render::RenderEnvironment::GetInstance()->window, GLFW_TRUE);
    }
    vke_common::Engine::WaitIdle();
    vke_common::Engine::Dispose();
    vke_common::GameConfig::Dispose();

    return 0;
}
