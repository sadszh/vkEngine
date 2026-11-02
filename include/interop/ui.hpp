#pragma once

#include <interop/interop.hpp>
#include <interop/math.hpp>
#include <cstdint>

namespace vke_interop
{
    struct CSharpUIFunctions
    {
        void (*dispatchPointerEvent)(uint32_t, uint64_t, int32_t, float, float) = nullptr;
        void (*unregisterNodeCallbacks)(uint64_t) = nullptr;
    };

    struct UIRectTransform
    {
        Vector2<float> anchorMin, anchorMax, offsetMin, offsetMax, pivot, scale;
        float rotation;
    };

    struct UITextLayoutOptions
    {
        float fontSize;
        int32_t horizontalAlignment, verticalAlignment, wordWrap;
    };
    struct UISelfLayoutOptions
    {
        int32_t width, height, ignoreLayout;
    };
    struct UIGroupLayoutOptions
    {
        int32_t direction;
        Vector4<float> padding;
        float spacing;
        int32_t mainAlignment, crossAlignment;
    };
    using GetUISelfLayoutFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, UISelfLayoutOptions *);
    using SetUISelfLayoutFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, const UISelfLayoutOptions *);
    int32_t VKE_INTEROP_CDECL GetUISelfLayout(uint32_t, uint64_t, UISelfLayoutOptions *);
    int32_t VKE_INTEROP_CDECL SetUISelfLayout(uint32_t, uint64_t, const UISelfLayoutOptions *);
    using GetUIGroupLayoutFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, UIGroupLayoutOptions *);
    using SetUIGroupLayoutFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, const UIGroupLayoutOptions *);
    int32_t VKE_INTEROP_CDECL GetUIGroupLayout(uint32_t, uint64_t, UIGroupLayoutOptions *);
    int32_t VKE_INTEROP_CDECL SetUIGroupLayout(uint32_t, uint64_t, const UIGroupLayoutOptions *);
    using GetUITextLayoutOptionsFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, UITextLayoutOptions *);
    using SetUITextLayoutOptionsFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, const UITextLayoutOptions *);
    using MeasureUITextFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, float, Vector2<float> *);
    using GetUITextSizeFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, Vector2<float> *);
    int32_t VKE_INTEROP_CDECL GetUITextLayoutOptions(uint32_t, uint64_t, UITextLayoutOptions *);
    int32_t VKE_INTEROP_CDECL SetUITextLayoutOptions(uint32_t, uint64_t, const UITextLayoutOptions *);
    int32_t VKE_INTEROP_CDECL MeasureUIText(uint32_t, uint64_t, float, Vector2<float> *);
    int32_t VKE_INTEROP_CDECL GetUITextSize(uint32_t, uint64_t, Vector2<float> *);

    using GetUITypeFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t);
    using FindUIChildFn = uint64_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, const char *, uint32_t);
    using GetUIChildCountFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t);
    using GetUIChildFn = uint64_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, uint32_t);
    using CreateUINodeFn = uint64_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, int32_t, const char *, uint32_t);
    using RemoveUINodeFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t);
    using SetUIParentFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, uint64_t);
    using SetUISiblingIndexFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, uint32_t);
    using GetUIRectTransformFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, UIRectTransform *);
    using SetUIRectTransformFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, const UIRectTransform *);
    using GetUIResolvedSizeFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, Vector2<float> *);
    using GetCanvasReferenceResolutionFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, Vector2<float> *);
    using SetCanvasReferenceResolutionFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, const Vector2<float> *);
    using GetCanvasSortOrderFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, int32_t *);
    using SetCanvasSortOrderFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, int32_t);
    using GetUITextLengthFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t);
    using GetUITextTextFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, char *, uint32_t);
    using SetUITextTextFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, const char *, uint32_t);
    using GetUITextColorFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, Vector4<float> *);
    using SetUITextColorFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, const Vector4<float> *);

    using GetUIImageTextureFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, uint64_t *);
    using SetUIImageTextureFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, uint64_t);
    using GetUIImageColorFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, Vector4<float> *);
    using SetUIImageColorFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, const Vector4<float> *);
    using GetUIImageUVRectFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, Vector4<float> *);
    using SetUIImageUVRectFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, const Vector4<float> *);
    using GetUIImagePreserveAspectFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, int32_t *);
    using SetUIImagePreserveAspectFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, int32_t);

    int32_t VKE_INTEROP_CDECL GetUIType(uint32_t entity, uint64_t handle);
    uint64_t VKE_INTEROP_CDECL FindUIChild(uint32_t entity, uint64_t parent, const char *name, uint32_t length);
    int32_t VKE_INTEROP_CDECL GetUIChildCount(uint32_t entity, uint64_t parent);
    uint64_t VKE_INTEROP_CDECL GetUIChild(uint32_t entity, uint64_t parent, uint32_t index);
    uint64_t VKE_INTEROP_CDECL CreateUINode(uint32_t entity, uint64_t parent, int32_t type, const char *name, uint32_t length);
    int32_t VKE_INTEROP_CDECL RemoveUINode(uint32_t entity, uint64_t handle);
    int32_t VKE_INTEROP_CDECL SetUIParent(uint32_t entity, uint64_t handle, uint64_t parent);
    int32_t VKE_INTEROP_CDECL SetUISiblingIndex(uint32_t entity, uint64_t handle, uint32_t index);
    int32_t VKE_INTEROP_CDECL GetUIRectTransform(uint32_t entity, uint64_t handle, UIRectTransform *value);
    int32_t VKE_INTEROP_CDECL SetUIRectTransform(uint32_t entity, uint64_t handle, const UIRectTransform *value);
    int32_t VKE_INTEROP_CDECL GetUIResolvedSize(uint32_t entity, uint64_t handle, Vector2<float> *value);
    int32_t VKE_INTEROP_CDECL GetCanvasReferenceResolution(uint32_t entity, Vector2<float> *value);
    int32_t VKE_INTEROP_CDECL SetCanvasReferenceResolution(uint32_t entity, const Vector2<float> *value);
    int32_t VKE_INTEROP_CDECL GetCanvasSortOrder(uint32_t entity, int32_t *value);
    int32_t VKE_INTEROP_CDECL SetCanvasSortOrder(uint32_t entity, int32_t value);

    int32_t VKE_INTEROP_CDECL GetUITextLength(uint32_t entity, uint64_t handle);
    int32_t VKE_INTEROP_CDECL GetUITextText(uint32_t entity, uint64_t handle, char *buffer, uint32_t bufferLength);
    int32_t VKE_INTEROP_CDECL SetUITextText(uint32_t entity, uint64_t handle, const char *text, uint32_t length);
    int32_t VKE_INTEROP_CDECL GetUITextColor(uint32_t entity, uint64_t handle, Vector4<float> *color);
    int32_t VKE_INTEROP_CDECL SetUITextColor(uint32_t entity, uint64_t handle, const Vector4<float> *color);

    int32_t VKE_INTEROP_CDECL GetUIImageTexture(uint32_t entity, uint64_t handle, uint64_t *value);
    int32_t VKE_INTEROP_CDECL SetUIImageTexture(uint32_t entity, uint64_t handle, uint64_t value);
    int32_t VKE_INTEROP_CDECL GetUIImageColor(uint32_t entity, uint64_t handle, Vector4<float> *value);
    int32_t VKE_INTEROP_CDECL SetUIImageColor(uint32_t entity, uint64_t handle, const Vector4<float> *value);
    int32_t VKE_INTEROP_CDECL GetUIImageUVRect(uint32_t entity, uint64_t handle, Vector4<float> *value);
    int32_t VKE_INTEROP_CDECL SetUIImageUVRect(uint32_t entity, uint64_t handle, const Vector4<float> *value);
    int32_t VKE_INTEROP_CDECL GetUIImagePreserveAspect(uint32_t entity, uint64_t handle, int32_t *value);
    int32_t VKE_INTEROP_CDECL SetUIImagePreserveAspect(uint32_t entity, uint64_t handle, int32_t value);
    using GetUIInteractionFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, int32_t *);
    int32_t VKE_INTEROP_CDECL GetUIInteraction(uint32_t, uint64_t, int32_t *);
    using SetUIInteractionFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, int32_t);
    int32_t VKE_INTEROP_CDECL SetUIInteraction(uint32_t, uint64_t, int32_t);
    using GetUIButtonColorFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, int32_t, Vector4<float> *);
    int32_t VKE_INTEROP_CDECL GetUIButtonColor(uint32_t, uint64_t, int32_t, Vector4<float> *);
    using SetUIButtonColorFn = int32_t(VKE_INTEROP_CDECL *)(uint32_t, uint64_t, int32_t, const Vector4<float> *);
    int32_t VKE_INTEROP_CDECL SetUIButtonColor(uint32_t, uint64_t, int32_t, const Vector4<float> *);
}
