#ifndef UI_TEXT_COMPONENT_H
#define UI_TEXT_COMPONENT_H

#include <ui/ui_renderable.hpp>
#include <ui/text_layout.hpp>
#include <font.hpp>
#include <asset/asset_manager.hpp>
#include <asset/asset_ref.hpp>
#include <render/material.hpp>
#include <string>
#include <string_view>

namespace vke_ui
{
    enum class TextHorizontalAlignment : int32_t { Left, Center, Right };
    enum class TextVerticalAlignment : int32_t { Top, Middle, Bottom };

    struct TextLayoutOptions
    {
        float fontSize = 48;
        TextHorizontalAlignment horizontalAlignment = TextHorizontalAlignment::Left;
        TextVerticalAlignment verticalAlignment = TextVerticalAlignment::Top;
        bool wordWrap = false;

        bool operator==(const TextLayoutOptions &) const = default;
        vke_common::Result<void> Validate() const
        {
            if (!std::isfinite(fontSize) || fontSize <= 0)
                return std::unexpected("fontSize must be finite and positive");
            if (horizontalAlignment < TextHorizontalAlignment::Left || horizontalAlignment > TextHorizontalAlignment::Right ||
                verticalAlignment < TextVerticalAlignment::Top || verticalAlignment > TextVerticalAlignment::Bottom)
                return std::unexpected("invalid text alignment");
            return {};
        }
    };

    struct UITextData final : UINodeData
    {
        std::string text;
        TextLayoutOptions layout;
        glm::vec4 color{1.0f};
        vke_common::AssetRef<vke_render::Material> material;
        vke_common::AssetRef<vke_common::Font> font{vke_common::BUILTIN_FONT_ARIAL_ID};

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            if (auto valid = Object(json).Strings({"text", "horizontalAlignment", "verticalAlignment"})
                .Numbers({"fontSize"}).Booleans({"wordWrap"}).Unsigneds({"material", "font"}).Vectors({"color"}, 4).Result(); !valid)
                return valid;
            if (json.contains("fontSize") && json["fontSize"].get<float>() <= 0)
                return std::unexpected("fontSize must be positive");
            if (json.contains("horizontalAlignment") && json["horizontalAlignment"] != "left" &&
                json["horizontalAlignment"] != "center" && json["horizontalAlignment"] != "right")
                return std::unexpected("invalid horizontalAlignment");
            if (json.contains("verticalAlignment") && json["verticalAlignment"] != "top" &&
                json["verticalAlignment"] != "middle" && json["verticalAlignment"] != "bottom")
                return std::unexpected("invalid verticalAlignment");
            return validateCommonJSON(json);
        }

        UITextData() = default;
        explicit UITextData(const nlohmann::json &json)
            : UINodeData(json), text(json.value("text", std::string())),
              material(json.value("material", vke_common::AssetHandle{0})),
              font(json.value("font", vke_common::BUILTIN_FONT_ARIAL_ID))
        {
            layout.fontSize = json.value("fontSize", 48.0f);
            layout.wordWrap = json.value("wordWrap", false);
            const auto horizontal = json.value("horizontalAlignment", std::string("left"));
            layout.horizontalAlignment = horizontal == "left" ? TextHorizontalAlignment::Left :
                horizontal == "center" ? TextHorizontalAlignment::Center : TextHorizontalAlignment::Right;
            const auto vertical = json.value("verticalAlignment", std::string("top"));
            layout.verticalAlignment = vertical == "top" ? TextVerticalAlignment::Top :
                vertical == "middle" ? TextVerticalAlignment::Middle : TextVerticalAlignment::Bottom;
            if (json.contains("color"))
            {
                const auto &value = json["color"];
                color = glm::vec4(value[0].get<float>(), value[1].get<float>(),
                                  value[2].get<float>(), value[3].get<float>());
            }
        }

        UIType GetType() const override { return UIType::Text; }
        std::unique_ptr<UINodeData> Clone() const override { return std::make_unique<UITextData>(*this); }

        vke_common::Result<void> Validate() const override
        {
            if (auto valid = layout.Validate(); !valid) return valid;
            return UINodeData::Validate();
        }

        vke_common::Result<void> ValidateAssets() const override
        {
            using vke_common::AssetManager;
            if (material.Handle() != 0 && !material.Get())
                if (auto result = AssetManager::ValidateMaterial(material.Handle()); !result)
                    return std::unexpected("material: " + result.error());
            if (!font.Get())
                if (auto result = AssetManager::ValidateFont(font.Handle()); !result)
                    return result;
            return UINodeData::ValidateAssets();
        }

        vke_common::Result<void> LoadAssets() override
        {
            using vke_common::AssetManager;
            if (material.Handle() != 0 && !material.Get())
            {
                auto loaded = AssetManager::LoadMaterial(material.Handle());
                if (!loaded) return std::unexpected(loaded.error());
                material.Resolve(std::move(*loaded));
            }
            if (!font.Get())
            {
                auto loaded = AssetManager::LoadFont(font.Handle());
                if (!loaded) return std::unexpected(loaded.error());
                font.Resolve(std::move(*loaded));
            }
            return UINodeData::LoadAssets();
        }

        nlohmann::json ToJSON() const override
        {
            auto json = UINodeData::ToJSON();
            json["type"] = "uiText";
            json["text"] = text;
            json["font"] = font.Handle();
            json["fontSize"] = layout.fontSize;
            json["wordWrap"] = layout.wordWrap;
            constexpr const char *horizontal[] = {"left", "center", "right"};
            constexpr const char *vertical[] = {"top", "middle", "bottom"};
            json["horizontalAlignment"] = horizontal[static_cast<int>(layout.horizontalAlignment)];
            json["verticalAlignment"] = vertical[static_cast<int>(layout.verticalAlignment)];
            json["color"] = {color.r, color.g, color.b, color.a};
            json["material"] = material.Handle();
            return json;
        }
    };

    class UIText final : public UIRenderableComponent
    {
    public:
        explicit UIText(const UITextData &data);

        UIType GetType() const override { return UIType::Text; }
        std::unique_ptr<UINodeData> ToData() const override
        {
            auto data = std::make_unique<UITextData>();
            fillData(*data);
            data->font.SetAsset(font);
            data->text = text;
            data->layout = options;
            data->color = GetColor();
            data->material.SetAsset(material);
            return data;
        }

        void SetText(std::string_view newText);
        vke_common::Result<void> SetFont(vke_common::AssetHandle handle);
        vke_common::AssetHandle GetFont() const { return font->handle; }
        vke_common::Result<void> SetLayoutOptions(const TextLayoutOptions &value);
        const TextLayoutOptions &GetLayoutOptions() const { return options; }
        glm::vec2 Measure() const;
        vke_common::Result<glm::vec2> Measure(float maxWidth) const;
        glm::vec2 GetTextSize() const { return {layout.width, layout.height}; }

        void SetColor(const glm::vec4 &newColor)
        {
            setQuadColor(newColor);
        }

        const std::string &GetText() const { return text; }
        glm::vec4 GetColor() const
        {
            return quadIDs.empty() ? glm::vec4(1.0f)
                                   : vke_render::Renderer::GetQuadManager()->Get(quadIDs.front()).color;
        }

    private:
        struct Character
        {
            uint32_t codepoint;
            float advance;
            vke_common::Glyph glyph;
        };
        std::string text;
        std::shared_ptr<vke_common::Font> font;
        TextLayoutOptions options;
        std::vector<Character> characters;
        TextLayout layout;
        void prepareCharacters();
        void updateLayout();
        bool rebuildGeometry(glm::vec4 color);
        void onSizeChanged() override;
        glm::vec2 measureContent(std::optional<float> width) const override;
    };
}

#endif
