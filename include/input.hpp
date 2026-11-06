#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include <array>

#include <render/render_common.hpp>
#include <glm/vec2.hpp>

namespace vke_common
{
    class InputManager
    {
    private:
        static constexpr int KEY_CAPACITY = GLFW_KEY_LAST + 1;
        static constexpr int MOUSE_BUTTON_CAPACITY = GLFW_MOUSE_BUTTON_LAST + 1;

        static InputManager *instance;

        InputManager();
        ~InputManager() {}
        InputManager(const InputManager &);
        InputManager &operator=(const InputManager);

    public:
        static InputManager *GetInstance()
        {
            return instance;
        }

        static InputManager *Init(GLFWwindow *window);

        static void Dispose();

        static void EndFrame();
        static void Reset();

        static void KeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods);

        static void WindowFocusCallback(GLFWwindow *window, int focused);

        static void CursorPosCallback(GLFWwindow *window, double xpos, double ypos);

        static void MouseButtonCallback(GLFWwindow *window, int button, int action, int mods);

        static void ScrollCallback(GLFWwindow *window, double xoffset, double yoffset);

        static bool IsKeyDown(int key);

        static bool IsKeyPressed(int key);

        static bool IsKeyReleased(int key);

        static bool IsMouseButtonDown(int button);

        static bool IsMouseButtonPressed(int button);

        static bool IsMouseButtonReleased(int button);

        static bool IsFocusLostThisFrame() { return RequireInstance().lostFocus; }

        static glm::vec2 GetMousePosition();

        static glm::vec2 GetMouseDelta();

        static glm::vec2 GetScrollDelta();

        static void SetCursorMode(int mode);

        static int GetCursorMode();

    private:
        GLFWwindow *window;
        std::array<uint8_t, KEY_CAPACITY> keyDown;
        std::array<uint8_t, KEY_CAPACITY> keyPressed;
        std::array<uint8_t, KEY_CAPACITY> keyReleased;
        std::array<uint8_t, MOUSE_BUTTON_CAPACITY> mouseButtonDown;
        std::array<uint8_t, MOUSE_BUTTON_CAPACITY> mouseButtonPressed;
        std::array<uint8_t, MOUSE_BUTTON_CAPACITY> mouseButtonReleased;
        glm::vec2 mousePosition;
        glm::vec2 mouseDelta;
        glm::vec2 scrollDelta;
        bool hasMousePosition;
        bool lostFocus = false;

        static InputManager &RequireInstance();

        static bool IsKeyInRange(int key);

        static bool IsMouseButtonInRange(int button);

        void ClearTransientState();
    };
}

#endif
