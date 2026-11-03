#include <editor/editor.hpp>

GLFWwindow *initWindow(int width, int height)
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    return glfwCreateWindow(width, height, "Vulkan editor", nullptr, nullptr);
}

std::vector<vke_render::PassType> defaultPasses{
    vke_render::SHADOW_PASS,
    vke_render::GBUFFER_PASS,
    vke_render::SSAO_PASS,
    vke_render::DEFERRED_LIGHTING_PASS,
    vke_render::SKYBOX_RENDERER,
    vke_render::ATMOSPHERE_PASS,
    vke_render::TRANSPARENT_PASS,
    vke_render::BLOOM_PASS,
    vke_render::TONE_MAPPING_PASS,
    vke_render::LAYERED_2D_RENDERER,
};

std::vector<std::unique_ptr<vke_render::RenderPassBase>> customPasses;

int main(int argc, char **argv)
{
    vke_common::Logger::Init(vke_common::LoggerOutput::EngineUI);
    VKE_FATAL_IF(argc != 2, "Usage: editor.exe <path/to/editorconfig.json>")

    const std::string configPath(argv[1]);
    const nlohmann::json &configJSON = vke_common::LoadJSON(configPath);
    vke_editor::EditorConfig *editorConfig = vke_editor::EditorConfig::Init(configJSON);

    GLFWwindow *window = initWindow(editorConfig->windowWidth, editorConfig->windowHeight);
    vke_editor::Editor *editor = vke_editor::Editor::Init(
        window, *editorConfig,
        editorConfig->windowWidth, editorConfig->windowHeight,
        defaultPasses, customPasses);

    const std::string &scenePath = editorConfig->gameConfig->defaultScenePath;
    {
        auto sceneData = vke_common::AssetManager::LoadSceneFile(scenePath);
        VKE_FATAL_IF(!sceneData, "Cannot read scene: {}", sceneData.error())
        auto prepared = vke_common::AssetManager::PrepareSceneData(*sceneData);
        VKE_FATAL_IF(!prepared, "Cannot prepare scene: {}", prepared.error())
        auto result = vke_common::SceneManager::Instantiate(*sceneData);
        VKE_FATAL_IF(!result, "Cannot instantiate scene: {}", result.error())
    }

    vke_common::InputManager::SetCursorMode(GLFW_CURSOR_NORMAL);

    glfwSetFramebufferSizeCallback(window, vke_editor::Editor::OnWindowResize);
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        if (!editor->Update())
            glfwSetWindowShouldClose(vke_render::RenderEnvironment::GetInstance()->window, GLFW_TRUE);
    }
    vke_editor::Editor::WaitIdle();
    vke_editor::Editor::Dispose();
    vke_editor::EditorConfig::Dispose();
    vke_common::Logger::Dispose();

    return 0;
}
