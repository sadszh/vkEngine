#include <interop/ui.hpp>
#include <scene.hpp>
#include <ui/text.hpp>
#include <algorithm>
#include <cstring>

namespace vke_interop
{
    static vke_component::Canvas *getCanvas(uint32_t entity)
    {
        auto *scene = vke_common::SceneManager::GetInstance();
        auto id = static_cast<entt::entity>(entity);
        if (!scene->registry.valid(id) || scene->IsPendingDestroy(id)) return nullptr;
        return scene->registry.try_get<vke_component::Canvas>(id);
    }

    static vke_ui::UIComponent *getNode(uint32_t entity, uint64_t handle)
    {
        auto *canvas = getCanvas(entity);
        return canvas ? canvas->Get(handle) : nullptr;
    }

    int32_t VKE_INTEROP_CDECL GetUIType(uint32_t entity, uint64_t handle)
    {
        auto *node = getNode(entity, handle);
        return node ? static_cast<int32_t>(node->GetType()) : 0;
    }

    uint64_t VKE_INTEROP_CDECL FindUIChild(uint32_t entity, uint64_t parent, const char *name, uint32_t length)
    {
        auto *canvas = getCanvas(entity);
        if (!canvas || (!name && length)) return 0;
        auto *node = canvas->Find(std::string_view(name ? name : "", length), parent);
        return node ? node->handle : 0;
    }

    int32_t VKE_INTEROP_CDECL GetUIChildCount(uint32_t entity, uint64_t parent)
    {
        auto *canvas = getCanvas(entity);
        if (!canvas) return -1;
        auto *node = canvas->Get(parent);
        if (parent && !node) return -1;
        return static_cast<int32_t>((node ? node->children : canvas->GetChildren()).size());
    }

    uint64_t VKE_INTEROP_CDECL GetUIChild(uint32_t entity, uint64_t parent, uint32_t index)
    {
        auto *canvas = getCanvas(entity);
        if (!canvas) return 0;
        auto *node = canvas->Get(parent);
        if (parent && !node) return 0;
        const auto &children = node ? node->children : canvas->GetChildren();
        return index < children.size() ? children[index]->handle : 0;
    }

    uint64_t VKE_INTEROP_CDECL CreateUINode(uint32_t entity, uint64_t parent, int32_t type, const char *name, uint32_t length)
    {
        auto *canvas = getCanvas(entity);
        if (!canvas || (!name && length)) return 0;
        std::unique_ptr<vke_ui::UINodeData> data;
        switch (static_cast<vke_ui::UIType>(type))
        {
        case vke_ui::UIType::Text:
            data = std::make_unique<vke_ui::UITextData>();
            break;
        case vke_ui::UIType::Image:
            data = std::make_unique<vke_ui::UIImageData>();
            break;
        case vke_ui::UIType::Button:
            data = std::make_unique<vke_ui::UIButtonData>();
            break;
        case vke_ui::UIType::Container:
            data = std::make_unique<vke_ui::UIContainerData>();
            break;
        default:
            return 0;
        }
        data->name.assign(name ? name : "", length);
        auto result = canvas->Add(*data, parent);
        return result ? (*result)->handle : 0;
    }

    int32_t VKE_INTEROP_CDECL RemoveUINode(uint32_t entity, uint64_t handle)
    {
        auto *canvas = getCanvas(entity);
        return canvas && canvas->Remove(handle).has_value();
    }

    int32_t VKE_INTEROP_CDECL SetUIParent(uint32_t entity, uint64_t handle, uint64_t parent)
    {
        auto *canvas = getCanvas(entity);
        return canvas && canvas->SetParent(handle, parent).has_value();
    }

    int32_t VKE_INTEROP_CDECL SetUISiblingIndex(uint32_t entity, uint64_t handle, uint32_t index)
    {
        auto *canvas = getCanvas(entity);
        return canvas && canvas->SetSiblingIndex(handle, index).has_value();
    }

    int32_t VKE_INTEROP_CDECL GetUIRectTransform(uint32_t entity, uint64_t handle, UIRectTransform *value)
    {
        auto *node = getNode(entity, handle);
        if (!node || !value) return 0;
        const auto &rect = node->rectTransform;
        *value = {{rect.anchorMin.x, rect.anchorMin.y}, {rect.anchorMax.x, rect.anchorMax.y},
                  {rect.offsetMin.x, rect.offsetMin.y}, {rect.offsetMax.x, rect.offsetMax.y},
                  {rect.pivot.x, rect.pivot.y}, {rect.scale.x, rect.scale.y}, rect.rotation};
        return 1;
    }

    int32_t VKE_INTEROP_CDECL SetUIRectTransform(uint32_t entity, uint64_t handle, const UIRectTransform *value)
    {
        auto *node = getNode(entity, handle);
        if (!node || !value) return 0;
        vke_ui::RectTransform rect;
        rect.anchorMin = {value->anchorMin.x, value->anchorMin.y};
        rect.anchorMax = {value->anchorMax.x, value->anchorMax.y};
        rect.offsetMin = {value->offsetMin.x, value->offsetMin.y};
        rect.offsetMax = {value->offsetMax.x, value->offsetMax.y};
        rect.pivot = {value->pivot.x, value->pivot.y};
        rect.scale = {value->scale.x, value->scale.y};
        rect.rotation = value->rotation;
        return node->SetRectTransform(rect).has_value();
    }

    int32_t VKE_INTEROP_CDECL GetUIResolvedSize(uint32_t entity, uint64_t handle, Vector2<float> *value)
    {
        auto *canvas = getCanvas(entity);
        if (!canvas || !value) return 0;
        auto *node = canvas->Get(handle);
        if (handle && !node) return 0;
        const auto &size = node ? node->resolvedSize : canvas->GetResolvedSize();
        *value = {size.x, size.y};
        return 1;
    }

    int32_t VKE_INTEROP_CDECL GetUISelfLayout(uint32_t entity, uint64_t handle, UISelfLayoutOptions *value)
    {
        auto *node = getNode(entity, handle);
        if (!node || !value) return 0;
        const auto &options = node->GetSelfLayout();
        *value = {static_cast<int32_t>(options.width), static_cast<int32_t>(options.height), options.ignoreLayout ? 1 : 0};
        return 1;
    }

    int32_t VKE_INTEROP_CDECL SetUISelfLayout(uint32_t entity, uint64_t handle, const UISelfLayoutOptions *value)
    {
        auto *node = getNode(entity, handle);
        if (!node || !value) return 0;
        vke_ui::UISelfLayoutOptions options;
        options.width = static_cast<vke_ui::SizeMode>(value->width);
        options.height = static_cast<vke_ui::SizeMode>(value->height);
        options.ignoreLayout = value->ignoreLayout != 0;
        return node->SetSelfLayout(options).has_value();
    }

    int32_t VKE_INTEROP_CDECL GetUIGroupLayout(uint32_t entity, uint64_t handle, UIGroupLayoutOptions *value)
    {
        auto *node = getNode(entity, handle);
        if (!node || !value) return 0;
        const auto &options = node->GetGroupLayout();
        *value = {static_cast<int32_t>(options.direction), {options.padding.x, options.padding.y, options.padding.z, options.padding.w},
            options.spacing, static_cast<int32_t>(options.mainAlignment), static_cast<int32_t>(options.crossAlignment)};
        return 1;
    }

    int32_t VKE_INTEROP_CDECL SetUIGroupLayout(uint32_t entity, uint64_t handle, const UIGroupLayoutOptions *value)
    {
        auto *node = getNode(entity, handle);
        if (!node || !value) return 0;
        vke_ui::UIGroupLayoutOptions options;
        options.direction = static_cast<vke_ui::LayoutDirection>(value->direction);
        options.padding = {value->padding.x, value->padding.y, value->padding.z, value->padding.w};
        options.spacing = value->spacing;
        options.mainAlignment = static_cast<vke_ui::LayoutAlignment>(value->mainAlignment);
        options.crossAlignment = static_cast<vke_ui::LayoutAlignment>(value->crossAlignment);
        return node->SetGroupLayout(options).has_value();
    }

    int32_t VKE_INTEROP_CDECL GetCanvasReferenceResolution(uint32_t entity, Vector2<float> *value)
    {
        auto *canvas = getCanvas(entity);
        if (!canvas || !value) return 0;
        const auto &size = canvas->GetReferenceResolution();
        *value = {size.x, size.y};
        return 1;
    }

    int32_t VKE_INTEROP_CDECL SetCanvasReferenceResolution(uint32_t entity, const Vector2<float> *value)
    {
        auto *canvas = getCanvas(entity);
        return canvas && value && canvas->SetReferenceResolution({value->x, value->y}).has_value();
    }

    int32_t VKE_INTEROP_CDECL GetCanvasSortOrder(uint32_t entity, int32_t *value)
    {
        auto *canvas = getCanvas(entity);
        if (!canvas || !value) return 0;
        *value = canvas->sortOrder;
        return 1;
    }

    int32_t VKE_INTEROP_CDECL SetCanvasSortOrder(uint32_t entity, int32_t value)
    {
        auto *canvas = getCanvas(entity);
        if (!canvas) return 0;
        canvas->sortOrder = value;
        return 1;
    }

    static vke_ui::UIText *getText(uint32_t entity, uint64_t handle)
    {
        auto *node = getNode(entity, handle);
        return node && node->GetType() == vke_ui::UIType::Text ? static_cast<vke_ui::UIText *>(node) : nullptr;
    }

    int32_t VKE_INTEROP_CDECL GetUITextLength(uint32_t entity, uint64_t handle)
    {
        auto *text = getText(entity, handle);
        return text ? static_cast<int32_t>(text->GetText().size()) : -1;
    }

    int32_t VKE_INTEROP_CDECL GetUITextLayoutOptions(uint32_t entity, uint64_t handle, UITextLayoutOptions *value)
    {
        auto *text = getText(entity, handle);
        if (!text || !value) return 0;
        const auto &options = text->GetLayoutOptions();
        *value = {options.fontSize, static_cast<int32_t>(options.horizontalAlignment),
                  static_cast<int32_t>(options.verticalAlignment), options.wordWrap};
        return 1;
    }

    int32_t VKE_INTEROP_CDECL SetUITextLayoutOptions(uint32_t entity, uint64_t handle, const UITextLayoutOptions *value)
    {
        auto *text = getText(entity, handle);
        if (!text || !value || (value->wordWrap != 0 && value->wordWrap != 1)) return 0;
        return text->SetLayoutOptions({value->fontSize,
            static_cast<vke_ui::TextHorizontalAlignment>(value->horizontalAlignment),
            static_cast<vke_ui::TextVerticalAlignment>(value->verticalAlignment), value->wordWrap != 0}).has_value();
    }

    int32_t VKE_INTEROP_CDECL MeasureUIText(uint32_t entity, uint64_t handle, float maxWidth, Vector2<float> *value)
    {
        auto *text = getText(entity, handle);
        if (!text || !value) return 0;
        // -1 is the ABI's unconstrained measurement sentinel.
        auto size = maxWidth == -1 ? vke_common::SceneResult<glm::vec2>(text->Measure()) : text->Measure(maxWidth);
        if (!size) return 0;
        *value = {size->x, size->y};
        return 1;
    }

    int32_t VKE_INTEROP_CDECL GetUITextSize(uint32_t entity, uint64_t handle, Vector2<float> *value)
    {
        auto *text = getText(entity, handle);
        if (!text || !value) return 0;
        const auto size = text->GetTextSize();
        *value = {size.x, size.y};
        return 1;
    }

    int32_t VKE_INTEROP_CDECL GetUITextText(uint32_t entity, uint64_t handle, char *buffer, uint32_t length)
    {
        auto *text = getText(entity, handle);
        if (!text || (!buffer && length)) return -1;
        const auto &value = text->GetText();
        const auto written = std::min(length, static_cast<uint32_t>(value.size()));
        if (written) std::memcpy(buffer, value.data(), written);
        return static_cast<int32_t>(written);
    }

    int32_t VKE_INTEROP_CDECL SetUITextText(uint32_t entity, uint64_t handle, const char *value, uint32_t length)
    {
        auto *text = getText(entity, handle);
        if (!text || (!value && length) || length > INT32_MAX) return 0;
        text->SetText(std::string_view(value ? value : "", length));
        return 1;
    }

    int32_t VKE_INTEROP_CDECL GetUITextColor(uint32_t entity, uint64_t handle, Vector4<float> *color)
    {
        auto *text = getText(entity, handle);
        if (!text || !color) return 0;
        const auto &value = text->GetColor();
        *color = {value.r, value.g, value.b, value.a};
        return 1;
    }

    int32_t VKE_INTEROP_CDECL SetUITextColor(uint32_t entity, uint64_t handle, const Vector4<float> *color)
    {
        auto *text = getText(entity, handle);
        if (!text || !color) return 0;
        text->SetColor({color->x, color->y, color->z, color->w});
        return 1;
    }

    static vke_ui::UIImage *getImage(uint32_t entity, uint64_t handle)
    {
        auto *node = getNode(entity, handle);
        return node && (node->GetType() == vke_ui::UIType::Image || node->GetType() == vke_ui::UIType::Button) ? static_cast<vke_ui::UIImage *>(node) : nullptr;
    }

    int32_t VKE_INTEROP_CDECL GetUIImageTexture(uint32_t entity, uint64_t handle, uint64_t *value)
    {
        auto *image = getImage(entity, handle);
        if (!image || !value) return 0;
        *value = image->GetTexture();
        return 1;
    }

    int32_t VKE_INTEROP_CDECL SetUIImageTexture(uint32_t entity, uint64_t handle, uint64_t value)
    {
        auto *image = getImage(entity, handle);
        return image && image->SetTexture(value).has_value();
    }

    int32_t VKE_INTEROP_CDECL GetUIImageColor(uint32_t entity, uint64_t handle, Vector4<float> *value)
    {
        auto *image = getImage(entity, handle);
        if (!image || !value) return 0;
        const auto &color = image->GetColor();
        *value = {color.r, color.g, color.b, color.a};
        return 1;
    }

    int32_t VKE_INTEROP_CDECL SetUIImageColor(uint32_t entity, uint64_t handle, const Vector4<float> *value)
    {
        auto *image = getImage(entity, handle);
        return image && value && image->SetColor({value->x, value->y, value->z, value->w}).has_value();
    }

    int32_t VKE_INTEROP_CDECL GetUIImageUVRect(uint32_t entity, uint64_t handle, Vector4<float> *value)
    {
        auto *image = getImage(entity, handle);
        if (!image || !value) return 0;
        const auto &uv = image->GetUVRect();
        *value = {uv.x, uv.y, uv.z, uv.w};
        return 1;
    }

    int32_t VKE_INTEROP_CDECL SetUIImageUVRect(uint32_t entity, uint64_t handle, const Vector4<float> *value)
    {
        auto *image = getImage(entity, handle);
        return image && value && image->SetUVRect({value->x, value->y, value->z, value->w}).has_value();
    }

    int32_t VKE_INTEROP_CDECL GetUIImagePreserveAspect(uint32_t entity, uint64_t handle, int32_t *value)
    {
        auto *image = getImage(entity, handle);
        if (!image || !value) return 0;
        *value = image->GetPreserveAspect();
        return 1;
    }

    int32_t VKE_INTEROP_CDECL SetUIImagePreserveAspect(uint32_t entity, uint64_t handle, int32_t value)
    {
        auto *image = getImage(entity, handle);
        if (!image || (value != 0 && value != 1)) return 0;
        image->SetPreserveAspect(value != 0);
        return 1;
    }

    int32_t VKE_INTEROP_CDECL GetUIInteraction(uint32_t entity, uint64_t handle, int32_t *value)
    {
        auto *node = getNode(entity, handle);
        if (!node || !value) return 0;
        *value = (node->GetHitTestEnabled() ? 1 : 0) | (node->GetInteractable() ? 2 : 0) |
                 (node->IsHovered() ? 4 : 0) | (node->IsPressed() ? 8 : 0);
        return 1;
    }

    int32_t VKE_INTEROP_CDECL SetUIInteraction(uint32_t entity, uint64_t handle, int32_t value)
    {
        auto *node = getNode(entity, handle);
        if (!node || value < 0 || value > 3) return 0;
        node->SetHitTestEnabled((value & 1) != 0);
        node->SetInteractable((value & 2) != 0);
        return 1;
    }

    int32_t VKE_INTEROP_CDECL GetUIButtonColor(uint32_t entity, uint64_t handle, int32_t state, Vector4<float> *value)
    {
        auto *node = getNode(entity, handle);
        if (!node || node->GetType() != vke_ui::UIType::Button || state < 0 || state > 3 || !value) return 0;
        const auto color = static_cast<vke_ui::UIButton *>(node)->GetStateColor(state);
        *value = {color.r, color.g, color.b, color.a};
        return 1;
    }

    int32_t VKE_INTEROP_CDECL SetUIButtonColor(uint32_t entity, uint64_t handle, int32_t state, const Vector4<float> *value)
    {
        auto *node = getNode(entity, handle);
        if (!node || node->GetType() != vke_ui::UIType::Button || state < 0 || state > 3 || !value) return 0;
        return static_cast<vke_ui::UIButton *>(node)->SetStateColor(state, {value->x, value->y, value->z, value->w}).has_value();
    }
}
