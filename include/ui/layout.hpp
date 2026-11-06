#pragma once

#include <glm/vec4.hpp>
#include <json_validation.hpp>

namespace vke_ui
{
    enum class LayoutDirection : int32_t { None, Horizontal, Vertical };
    enum class SizeMode : int32_t { Rect, Content };
    enum class LayoutAlignment : int32_t { Start, Center, End, Stretch };

    struct UISelfLayoutOptions
    {
        SizeMode width = SizeMode::Rect;
        SizeMode height = SizeMode::Rect;
        bool ignoreLayout = false;

        vke_common::Result<void> Validate() const
        {
            if (width < SizeMode::Rect || width > SizeMode::Content ||
                height < SizeMode::Rect || height > SizeMode::Content)
                return std::unexpected("invalid UI layout size mode");
            return {};
        }

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            if (auto valid = vke_common::json_validation::Object(json)
                .Strings({"width", "height"}).Booleans({"ignoreLayout"}).Result(); !valid)
                return valid;
            for (const char *key : {"width", "height"})
                if (json.contains(key) && json[key] != "rect" && json[key] != "content")
                    return std::unexpected("invalid UI layout size mode");
            return {};
        }

        UISelfLayoutOptions() = default;
        explicit UISelfLayoutOptions(const nlohmann::json &json)
        {
            width = json.value("width", std::string("rect")) == "content" ? SizeMode::Content : SizeMode::Rect;
            height = json.value("height", std::string("rect")) == "content" ? SizeMode::Content : SizeMode::Rect;
            ignoreLayout = json.value("ignoreLayout", false);
        }

        nlohmann::json ToJSON() const
        {
            constexpr const char *sizes[] = {"rect", "content"};
            return {{"width", sizes[static_cast<int>(width)]}, {"height", sizes[static_cast<int>(height)]},
                    {"ignoreLayout", ignoreLayout}};
        }
    };

    struct UIGroupLayoutOptions
    {
        LayoutDirection direction = LayoutDirection::None;
        glm::vec4 padding{0}; // Left, top, right, bottom.
        float spacing = 0;
        LayoutAlignment mainAlignment = LayoutAlignment::Start;
        LayoutAlignment crossAlignment = LayoutAlignment::Start;

        vke_common::Result<void> Validate() const
        {
            if (direction < LayoutDirection::None || direction > LayoutDirection::Vertical ||
                mainAlignment < LayoutAlignment::Start || mainAlignment > LayoutAlignment::End ||
                crossAlignment < LayoutAlignment::Start || crossAlignment > LayoutAlignment::Stretch)
                return std::unexpected("invalid UI layout mode or alignment");
            if (!std::isfinite(spacing) || spacing < 0)
                return std::unexpected("layout spacing must be finite and nonnegative");
            for (int i = 0; i < 4; ++i)
                if (!std::isfinite(padding[i]) || padding[i] < 0)
                    return std::unexpected("layout padding must be finite and nonnegative");
            return {};
        }

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            if (auto valid = vke_common::json_validation::Object(json)
                .Strings({"direction", "mainAlignment", "crossAlignment"})
                .Vectors({"padding"}, 4).Numbers({"spacing"}).Result(); !valid)
                return valid;
            auto oneOf = [&](const char *key, std::initializer_list<const char *> values)
            {
                return !json.contains(key) || std::any_of(values.begin(), values.end(),
                    [&](const char *value) { return json[key] == value; });
            };
            if (!oneOf("direction", {"none", "horizontal", "vertical"}) ||
                !oneOf("mainAlignment", {"start", "center", "end"}) ||
                !oneOf("crossAlignment", {"start", "center", "end", "stretch"}))
                return std::unexpected("invalid UI layout mode or alignment");
            return UIGroupLayoutOptions(json).Validate();
        }

        UIGroupLayoutOptions() = default;
        explicit UIGroupLayoutOptions(const nlohmann::json &json)
        {
            const auto value = json.value("direction", std::string("none"));
            direction = value == "none" ? LayoutDirection::None :
                value == "horizontal" ? LayoutDirection::Horizontal : LayoutDirection::Vertical;
            if (json.contains("padding"))
                for (int i = 0; i < 4; ++i) padding[i] = json["padding"][i].get<float>();
            spacing = json.value("spacing", 0.0f);
            auto alignment = [&](const char *key)
            {
                const auto name = json.value(key, std::string("start"));
                return name == "start" ? LayoutAlignment::Start : name == "center" ? LayoutAlignment::Center :
                    name == "end" ? LayoutAlignment::End : LayoutAlignment::Stretch;
            };
            mainAlignment = alignment("mainAlignment");
            crossAlignment = alignment("crossAlignment");
        }

        nlohmann::json ToJSON() const
        {
            constexpr const char *directions[] = {"none", "horizontal", "vertical"};
            constexpr const char *alignments[] = {"start", "center", "end", "stretch"};
            return {{"direction", directions[static_cast<int>(direction)]},
                    {"padding", {padding.x, padding.y, padding.z, padding.w}}, {"spacing", spacing},
                    {"mainAlignment", alignments[static_cast<int>(mainAlignment)]},
                    {"crossAlignment", alignments[static_cast<int>(crossAlignment)]}};
        }
    };
}
