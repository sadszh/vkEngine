#ifndef UI_COMPONENT_H
#define UI_COMPONENT_H

#include <ui/rect_transform.hpp>
#include <ui/layout.hpp>
#include <glm/mat4x4.hpp>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <glm/gtc/matrix_inverse.hpp>
#include <cmath>
#include <optional>

namespace vke_component
{
    class Canvas;
}

namespace vke_ui
{
    using UIHandle = uint64_t;

    enum class UIType : int32_t
    {
        Container = 1,
        Text = 2,
        Image = 3,
        Button = 4
    };

    enum class PointerEvent : int32_t
    {
        Enter,
        Exit,
        Down,
        Up,
        Click,
        Cancel
    };

    struct UIPointerInput
    {
        glm::vec2 position{0}; // Render target pixels, origin at top left.
        bool active = false;
        bool inside = false;
        bool down = false;
        bool pressed = false;
        bool released = false;
    };

    struct UINodeData
    {
        std::string name;
        RectTransform rectTransform;
        UISelfLayoutOptions selfLayout;
        UIGroupLayoutOptions groupLayout;
        bool hitTestEnabled = false;
        bool interactable = true;
        std::vector<std::unique_ptr<UINodeData>> children;

        virtual ~UINodeData() = default;
        virtual UIType GetType() const = 0;
        virtual std::unique_ptr<UINodeData> Clone() const = 0;

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json);
        static std::unique_ptr<UINodeData> FromJSON(const nlohmann::json &json);

        virtual vke_common::Result<void> Validate() const
        {
            if (auto valid = rectTransform.Validate(); !valid)
                return valid;
            if (auto valid = selfLayout.Validate(); !valid)
                return valid;
            if (auto valid = groupLayout.Validate(); !valid)
                return valid;
            for (const auto &child : children)
            {
                if (!child)
                    return std::unexpected("UI child must not be null");
                if (auto valid = child->Validate(); !valid)
                    return valid;
            }
            return {};
        }

        virtual vke_common::Result<void> ValidateAssets() const
        {
            for (const auto &child : children)
                if (auto valid = child->ValidateAssets(); !valid)
                    return valid;
            return {};
        }

        virtual vke_common::Result<void> LoadAssets()
        {
            for (auto &child : children)
                if (auto loaded = child->LoadAssets(); !loaded)
                    return loaded;
            return {};
        }

        virtual nlohmann::json ToJSON() const
        {
            nlohmann::json json{{"name", name}, {"rectTransform", rectTransform.ToJSON()}, {"hitTestEnabled", hitTestEnabled}, {"interactable", interactable}, {"children", nlohmann::json::array()}};
            json["selfLayout"] = selfLayout.ToJSON();
            json["groupLayout"] = groupLayout.ToJSON();
            for (const auto &child : children)
                json["children"].push_back(child->ToJSON());
            return json;
        }

        static vke_common::Result<void> ValidateChildrenJSON(const nlohmann::json &json)
        {
            if (!json.contains("children"))
                return {};
            if (!json["children"].is_array())
                return std::unexpected("children must be an array");
            for (const auto &child : json["children"])
                if (auto valid = ValidateJSON(child); !valid)
                    return valid;
            return {};
        }

    protected:
        static vke_common::Result<void> validateCommonJSON(const nlohmann::json &json)
        {
            if (auto valid = vke_common::json_validation::Object(json).Strings({"name"}).Booleans({"hitTestEnabled", "interactable"}).Result(); !valid)
                return valid;
            if (json.contains("rectTransform"))
                if (auto valid = RectTransform::ValidateJSON(json["rectTransform"]); !valid)
                    return valid;
            if (json.contains("selfLayout"))
                if (auto valid = UISelfLayoutOptions::ValidateJSON(json["selfLayout"]); !valid)
                    return valid;
            if (json.contains("groupLayout"))
                if (auto valid = UIGroupLayoutOptions::ValidateJSON(json["groupLayout"]); !valid)
                    return valid;
            return ValidateChildrenJSON(json);
        }

        UINodeData() = default;
        explicit UINodeData(const nlohmann::json &json) : name(json.value("name", std::string())),
                                                          hitTestEnabled(json.value("hitTestEnabled", false)), interactable(json.value("interactable", true))
        {
            if (json.contains("rectTransform"))
                rectTransform = RectTransform(json["rectTransform"]);
            if (json.contains("selfLayout"))
                selfLayout = UISelfLayoutOptions(json["selfLayout"]);
            if (json.contains("groupLayout"))
                groupLayout = UIGroupLayoutOptions(json["groupLayout"]);
            if (json.contains("children"))
                for (const auto &child : json["children"])
                    children.push_back(FromJSON(child));
        }

        UINodeData(const UINodeData &other) { *this = other; }
        UINodeData &operator=(const UINodeData &other)
        {
            if (this == &other)
                return *this;
            std::vector<std::unique_ptr<UINodeData>> copiedChildren;
            copiedChildren.reserve(other.children.size());
            for (const auto &child : other.children)
                copiedChildren.push_back(child ? child->Clone() : nullptr);
            name = other.name;
            rectTransform = other.rectTransform;
            selfLayout = other.selfLayout;
            groupLayout = other.groupLayout;
            hitTestEnabled = other.hitTestEnabled;
            interactable = other.interactable;
            children = std::move(copiedChildren);
            return *this;
        }
        UINodeData(UINodeData &&) noexcept = default;
        UINodeData &operator=(UINodeData &&) noexcept = default;
    };

    class UIComponent
    {
    public:
        std::string name;
        UIHandle handle = 0;
        UIComponent *parent = nullptr;
        std::vector<std::unique_ptr<UIComponent>> children;
        RectTransform rectTransform;
        glm::vec2 resolvedSize{0.0f};

        virtual ~UIComponent() = default;
        bool GetHitTestEnabled() const { return hitTestEnabled; }
        void SetHitTestEnabled(bool value)
        {
            hitTestEnabled = value;
            if (!value)
                setPointerState(false, false);
        }
        bool GetInteractable() const { return interactable; }
        void SetInteractable(bool value)
        {
            interactable = value;
            if (!value)
                hovered = pressed = false;
            onPointerStateChanged();
        }
        bool IsHovered() const { return hovered; }
        bool IsPressed() const { return pressed; }
        bool ContainsPoint(const glm::vec2 &point) const
        {
            if (resolvedSize.x <= 0 || resolvedSize.y <= 0)
                return false;
            const float determinant = glm::determinant(glm::mat2(layoutMatrix));
            if (determinant == 0 || !std::isfinite(determinant))
                return false;
            const auto local = glm::vec2(glm::inverse(layoutMatrix) * glm::vec4(point, 0, 1)) + rectTransform.pivot * resolvedSize;
            return local.x >= 0 && local.y >= 0 && local.x < resolvedSize.x && local.y < resolvedSize.y;
        }
        UIComponent(const UIComponent &) = delete;
        UIComponent &operator=(const UIComponent &) = delete;

        virtual UIType GetType() const = 0;
        vke_common::Result<void> SetRectTransform(const RectTransform &value)
        {
            if (auto valid = value.Validate(); !valid)
                return valid;
            rectTransform = value;
            layoutDirty = true;
            return {};
        }
        virtual std::unique_ptr<UINodeData> ToData() const = 0;
        const UISelfLayoutOptions &GetSelfLayout() const { return selfLayout; }
        vke_common::Result<void> SetSelfLayout(const UISelfLayoutOptions &value)
        {
            if (auto valid = value.Validate(); !valid) return valid;
            selfLayout = value;
            layoutDirty = true;
            return {};
        }

        const UIGroupLayoutOptions &GetGroupLayout() const { return groupLayout; }
        vke_common::Result<void> SetGroupLayout(const UIGroupLayoutOptions &value)
        {
            if (auto valid = value.Validate(); !valid) return valid;
            groupLayout = value;
            layoutDirty = true;
            return {};
        }

    protected:
        explicit UIComponent(const UINodeData &data) : name(data.name), rectTransform(data.rectTransform),
                                                       hitTestEnabled(data.hitTestEnabled), interactable(data.interactable),
                                                       selfLayout(data.selfLayout), groupLayout(data.groupLayout) {}
        void fillData(UINodeData &data) const
        {
            data.name = name;
            data.rectTransform = rectTransform;
            data.selfLayout = selfLayout;
            data.groupLayout = groupLayout;
            data.hitTestEnabled = hitTestEnabled;
            data.interactable = interactable;
            for (const auto &child : children)
                data.children.push_back(child->ToData());
        }
        virtual void onPointerStateChanged() {}
        virtual void unload() {}
        virtual void onSizeChanged() {}
        virtual void updateRenderTransform(const glm::mat4 &matrix, float order) {}
        virtual glm::vec2 measureContent(std::optional<float> width) const { return glm::vec2(0); }
        void invalidateLayout() { layoutDirty = true; }

    private:
        friend class vke_component::Canvas;
        friend class UIManager;
        void setPointerState(bool over, bool down)
        {
            hovered = over;
            pressed = down;
            onPointerStateChanged();
        }

        bool hitTestEnabled;
        bool interactable;
        bool hovered = false;
        bool pressed = false;
        bool layoutDirty = true;
        UISelfLayoutOptions selfLayout;
        UIGroupLayoutOptions groupLayout;
        glm::vec2 measuredSize{0};
        glm::mat4 layoutMatrix{1.0f};
    };

    struct UIContainerData final : UINodeData
    {
        UIContainerData() = default;
        explicit UIContainerData(const nlohmann::json &json) : UINodeData(json) {}

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            return validateCommonJSON(json);
        }

        UIType GetType() const override { return UIType::Container; }
        std::unique_ptr<UINodeData> Clone() const override { return std::make_unique<UIContainerData>(*this); }

        nlohmann::json ToJSON() const override
        {
            auto json = UINodeData::ToJSON();
            json["type"] = "uiContainer";
            return json;
        }
    };

    class UIContainer final : public UIComponent
    {
    public:
        explicit UIContainer(const UIContainerData &data) : UIComponent(data) {}
        UIType GetType() const override { return UIType::Container; }
        std::unique_ptr<UINodeData> ToData() const override
        {
            auto data = std::make_unique<UIContainerData>();
            fillData(*data);
            return data;
        }
    };
}

#endif
