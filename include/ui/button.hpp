#pragma once

#include <ui/image.hpp>
#include <array>

namespace vke_ui
{
    struct UIButtonData final : UIImageData
    {
        glm::vec4 hoverColor{0.85f, 0.85f, 0.85f, 1};
        glm::vec4 pressedColor{0.65f, 0.65f, 0.65f, 1};
        glm::vec4 disabledColor{0.4f, 0.4f, 0.4f, 1};

        UIButtonData() { hitTestEnabled = true; }
        explicit UIButtonData(const nlohmann::json &json) : UIImageData(json)
        {
            hitTestEnabled = json.value("hitTestEnabled", true);
            const char *names[] = {"hoverColor", "pressedColor", "disabledColor"};
            glm::vec4 *colors[] = {&hoverColor, &pressedColor, &disabledColor};
            for (int i = 0; i < 3; ++i)
                if (json.contains(names[i]))
                    for (int j = 0; j < 4; ++j) (*colors[i])[j] = json[names[i]][j].get<float>();
        }
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            if (auto valid = vke_common::json_validation::Object(json)
                .Vectors({"hoverColor", "pressedColor", "disabledColor"}, 4).Result(); !valid) return valid;
            return UIImageData::ValidateJSON(json);
        }
        UIType GetType() const override { return UIType::Button; }
        std::unique_ptr<UINodeData> Clone() const override { return std::make_unique<UIButtonData>(*this); }
        nlohmann::json ToJSON() const override
        {
            auto json = UIImageData::ToJSON();
            json["type"] = "uiButton";
            json["hoverColor"] = {hoverColor.r, hoverColor.g, hoverColor.b, hoverColor.a};
            json["pressedColor"] = {pressedColor.r, pressedColor.g, pressedColor.b, pressedColor.a};
            json["disabledColor"] = {disabledColor.r, disabledColor.g, disabledColor.b, disabledColor.a};
            return json;
        }
    };

    class UIButton final : public UIImage
    {
    public:
        explicit UIButton(const UIButtonData &data) : UIImage(data),
            colors{data.color, data.hoverColor, data.pressedColor, data.disabledColor} { onPointerStateChanged(); }
        UIType GetType() const override { return UIType::Button; }
        std::unique_ptr<UINodeData> ToData() const override
        {
            auto data = std::make_unique<UIButtonData>();
            fillImageData(*data);
            data->hoverColor = colors[1];
            data->pressedColor = colors[2];
            data->disabledColor = colors[3];
            return data;
        }
        glm::vec4 GetColor() const override { return colors[0]; }
        vke_common::Result<void> SetColor(const glm::vec4 &value) override { return SetStateColor(0, value); }
        const glm::vec4 &GetStateColor(size_t state) const { return colors[state]; }
        vke_common::Result<void> SetStateColor(size_t state, const glm::vec4 &value)
        {
            if (quadIDs.empty()) return std::unexpected("button quad is not allocated");
            colors[state] = value;
            onPointerStateChanged();
            return {};
        }

    private:
        std::array<glm::vec4, 4> colors;
        void onPointerStateChanged() override
        {
            setQuadColor(colors[!GetInteractable() ? 3 : IsPressed() && IsHovered() ? 2 : IsHovered() ? 1 : 0]);
        }
    };
}
