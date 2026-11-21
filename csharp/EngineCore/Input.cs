namespace vkEngine.EngineCore
{
    public enum KeyCode
    {
        Space = 32,
        Apostrophe = 39,
        Comma = 44,
        Minus = 45,
        Period = 46,
        Slash = 47,
        D0 = 48,
        D1 = 49,
        D2 = 50,
        D3 = 51,
        D4 = 52,
        D5 = 53,
        D6 = 54,
        D7 = 55,
        D8 = 56,
        D9 = 57,
        Semicolon = 59,
        Equal = 61,
        A = 65,
        B = 66,
        C = 67,
        D = 68,
        E = 69,
        F = 70,
        G = 71,
        H = 72,
        I = 73,
        J = 74,
        K = 75,
        L = 76,
        M = 77,
        N = 78,
        O = 79,
        P = 80,
        Q = 81,
        R = 82,
        S = 83,
        T = 84,
        U = 85,
        V = 86,
        W = 87,
        X = 88,
        Y = 89,
        Z = 90,
        LeftBracket = 91,
        Backslash = 92,
        RightBracket = 93,
        GraveAccent = 96,
        Escape = 256,
        Enter = 257,
        Tab = 258,
        Backspace = 259,
        Insert = 260,
        Delete = 261,
        Right = 262,
        Left = 263,
        Down = 264,
        Up = 265,
        PageUp = 266,
        PageDown = 267,
        Home = 268,
        End = 269,
        CapsLock = 280,
        ScrollLock = 281,
        NumLock = 282,
        PrintScreen = 283,
        Pause = 284,
        F1 = 290,
        F2 = 291,
        F3 = 292,
        F4 = 293,
        F5 = 294,
        F6 = 295,
        F7 = 296,
        F8 = 297,
        F9 = 298,
        F10 = 299,
        F11 = 300,
        F12 = 301,
        LeftShift = 340,
        LeftControl = 341,
        LeftAlt = 342,
        LeftSuper = 343,
        RightShift = 344,
        RightControl = 345,
        RightAlt = 346,
        RightSuper = 347
    }

    public enum MouseButton
    {
        Left = 0,
        Right = 1,
        Middle = 2,
        Button4 = 3,
        Button5 = 4,
        Button6 = 5,
        Button7 = 6,
        Button8 = 7
    }

    public enum CursorMode
    {
        Normal = 0x00034001,
        Hidden = 0x00034002,
        Disabled = 0x00034003
    }

    public static unsafe class Input
    {
        private static delegate* unmanaged[Cdecl]<int, int> isKeyDown;
        private static delegate* unmanaged[Cdecl]<int, int> isKeyPressed;
        private static delegate* unmanaged[Cdecl]<int, int> isKeyReleased;
        private static delegate* unmanaged[Cdecl]<int, int> isMouseButtonDown;
        private static delegate* unmanaged[Cdecl]<int, int> isMouseButtonPressed;
        private static delegate* unmanaged[Cdecl]<int, int> isMouseButtonReleased;
        private static delegate* unmanaged[Cdecl]<NVec2*, void> getMousePosition;
        private static delegate* unmanaged[Cdecl]<NVec2*, void> getMouseDelta;
        private static delegate* unmanaged[Cdecl]<NVec2*, void> getMouseScrollDelta;
        private static delegate* unmanaged[Cdecl]<int, void> setCursorMode;
        private static delegate* unmanaged[Cdecl]<int> getCursorMode;

        internal static void RegisterNativeFunctions(NativeFunctions* functions)
        {
            isKeyDown = functions->IsKeyDown;
            isKeyPressed = functions->IsKeyPressed;
            isKeyReleased = functions->IsKeyReleased;
            isMouseButtonDown = functions->IsMouseButtonDown;
            isMouseButtonPressed = functions->IsMouseButtonPressed;
            isMouseButtonReleased = functions->IsMouseButtonReleased;
            getMousePosition = functions->GetMousePosition;
            getMouseDelta = functions->GetMouseDelta;
            getMouseScrollDelta = functions->GetMouseScrollDelta;
            setCursorMode = functions->SetCursorMode;
            getCursorMode = functions->GetCursorMode;
        }

        public static bool IsKeyDown(KeyCode key)
        {
            return isKeyDown((int)key) != 0;
        }

        public static bool IsKeyPressed(KeyCode key)
        {
            return isKeyPressed((int)key) != 0;
        }

        public static bool IsKeyReleased(KeyCode key)
        {
            return isKeyReleased((int)key) != 0;
        }

        public static bool IsMouseButtonDown(MouseButton button)
        {
            return isMouseButtonDown((int)button) != 0;
        }

        public static bool IsMouseButtonPressed(MouseButton button)
        {
            return isMouseButtonPressed((int)button) != 0;
        }

        public static bool IsMouseButtonReleased(MouseButton button)
        {
            return isMouseButtonReleased((int)button) != 0;
        }

        public static NVec2 MousePosition
        {
            get
            {
                NVec2 value = default;
                getMousePosition(&value);
                return value;
            }
        }

        public static bool TryGetMouseRay(out NVec3 origin, out NVec3 direction)
        {
            NVec3 rayOrigin = default, rayDirection = default;
            bool success = NativeFunctionRegistry.Functions.GetMouseRay(&rayOrigin, &rayDirection) != 0;
            origin = rayOrigin;
            direction = rayDirection;
            return success;
        }

        public static NVec2 MouseDelta
        {
            get
            {
                NVec2 value = default;
                getMouseDelta(&value);
                return value;
            }
        }

        public static NVec2 MouseScrollDelta
        {
            get
            {
                NVec2 value = default;
                getMouseScrollDelta(&value);
                return value;
            }
        }

        public static CursorMode CursorMode
        {
            get
            {
                return (CursorMode)getCursorMode();
            }
            set
            {
                setCursorMode((int)value);
            }
        }
    }
}
