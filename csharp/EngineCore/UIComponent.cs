using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace vkEngine.EngineCore
{
    public enum LayoutDirection { None, Horizontal, Vertical }
    public enum SizeMode { Rect, Content }
    public enum LayoutAlignment { Start, Center, End, Stretch }

    [StructLayout(LayoutKind.Sequential)]
    public struct UISelfLayoutOptions
    {
        public SizeMode Width, Height;
        private int ignoreLayout;
        public bool IgnoreLayout { readonly get => ignoreLayout != 0; set => ignoreLayout = value ? 1 : 0; }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct UIGroupLayoutOptions
    {
        public LayoutDirection Direction;
        public NVec4 Padding; // Left, top, right, bottom.
        public float Spacing;
        public LayoutAlignment MainAlignment, CrossAlignment;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct RectTransform
    {
        public NVec2 AnchorMin, AnchorMax, OffsetMin, OffsetMax, Pivot, Scale;
        public float Rotation;

        public RectTransform()
        {
            AnchorMin = AnchorMax = OffsetMin = OffsetMax = NVec2.Zero;
            Pivot = new NVec2(0.5f, 0.5f);
            Scale = new NVec2(1, 1);
            Rotation = 0;
        }
    }

    public abstract unsafe class UIComponent
    {
        internal UIComponent(Canvas canvas, UInt64 handle) { Canvas = canvas; Handle = handle; }
        public Canvas Canvas { get; }
        public UInt64 Handle { get; }
        public bool IsValid => NativeFunctionRegistry.Functions.GetUIType(Canvas.Entity, Handle) != 0;
        public IReadOnlyList<UIComponent> Children => Canvas.GetChildren(this);

        private int interaction
        {
            get
            {
                int value = 0;
                RequireSuccess(NativeFunctionRegistry.Functions.GetUIInteraction(Canvas.Entity, Handle, &value));
                return value;
            }
            set { RequireSuccess(NativeFunctionRegistry.Functions.SetUIInteraction(Canvas.Entity, Handle, value)); }
        }
        public bool HitTestEnabled { get => (interaction & 1) != 0; set => interaction = (interaction & 2) | (value ? 1 : 0); }
        public bool Interactable { get => (interaction & 2) != 0; set => interaction = (interaction & 1) | (value ? 2 : 0); }
        public bool IsHovered => (interaction & 4) != 0;
        public bool IsPressed => (interaction & 8) != 0;

        public event Action<UIPointerEvent> PointerEnter
        {
            add => UIManager.Add(this, UIPointerEventType.Enter, value);
            remove => UIManager.Remove(Handle, UIPointerEventType.Enter, value);
        }
        public event Action<UIPointerEvent> PointerExit
        {
            add => UIManager.Add(this, UIPointerEventType.Exit, value);
            remove => UIManager.Remove(Handle, UIPointerEventType.Exit, value);
        }
        public event Action<UIPointerEvent> PointerDown
        {
            add => UIManager.Add(this, UIPointerEventType.Down, value);
            remove => UIManager.Remove(Handle, UIPointerEventType.Down, value);
        }
        public event Action<UIPointerEvent> PointerUp
        {
            add => UIManager.Add(this, UIPointerEventType.Up, value);
            remove => UIManager.Remove(Handle, UIPointerEventType.Up, value);
        }
        public event Action<UIPointerEvent> Click
        {
            add => UIManager.Add(this, UIPointerEventType.Click, value);
            remove => UIManager.Remove(Handle, UIPointerEventType.Click, value);
        }
        public event Action<UIPointerEvent> PointerCancel
        {
            add => UIManager.Add(this, UIPointerEventType.Cancel, value);
            remove => UIManager.Remove(Handle, UIPointerEventType.Cancel, value);
        }

        protected static void RequireSuccess(int result)
        {
            if (result == 0) throw new InvalidOperationException("UI node is invalid or the requested operation is not valid.");
        }

        public RectTransform RectTransform
        {
            get
            {
                RectTransform value = default;
                RequireSuccess(NativeFunctionRegistry.Functions.GetUIRectTransform(Canvas.Entity, Handle, &value));
                return value;
            }
            set { RequireSuccess(NativeFunctionRegistry.Functions.SetUIRectTransform(Canvas.Entity, Handle, &value)); }
        }

        public NVec2 ResolvedSize
        {
            get
            {
                NVec2 value = default;
                RequireSuccess(NativeFunctionRegistry.Functions.GetUIResolvedSize(Canvas.Entity, Handle, &value));
                return value;
            }
        }

        public UISelfLayoutOptions SelfLayout
        {
            get
            {
                UISelfLayoutOptions value = default;
                RequireSuccess(NativeFunctionRegistry.Functions.GetUISelfLayout(Canvas.Entity, Handle, &value));
                return value;
            }
            set { RequireSuccess(NativeFunctionRegistry.Functions.SetUISelfLayout(Canvas.Entity, Handle, &value)); }
        }

        public UIGroupLayoutOptions GroupLayout
        {
            get
            {
                UIGroupLayoutOptions value = default;
                RequireSuccess(NativeFunctionRegistry.Functions.GetUIGroupLayout(Canvas.Entity, Handle, &value));
                return value;
            }
            set { RequireSuccess(NativeFunctionRegistry.Functions.SetUIGroupLayout(Canvas.Entity, Handle, &value)); }
        }

        public void SetParent(UIComponent? parent) =>
            RequireSuccess(NativeFunctionRegistry.Functions.SetUIParent(Canvas.Entity, Handle, Canvas.ParentHandle(parent)));
        public void SetSiblingIndex(int index)
        {
            if (index < 0) throw new ArgumentOutOfRangeException(nameof(index));
            RequireSuccess(NativeFunctionRegistry.Functions.SetUISiblingIndex(Canvas.Entity, Handle, (uint)index));
        }
    }

    public sealed class UIContainer : UIComponent
    {
        internal UIContainer(Canvas canvas, UInt64 handle) : base(canvas, handle) {}
    }

    public abstract class UIRenderableComponent : UIComponent
    {
        internal UIRenderableComponent(Canvas canvas, UInt64 handle) : base(canvas, handle) {}
    }
}
