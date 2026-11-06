#ifndef RECT_TRANSFORM_H
#define RECT_TRANSFORM_H

#include <glm/vec2.hpp>
#include <json_validation.hpp>

namespace vke_ui
{
    struct RectTransform
    {
        glm::vec2 anchorMin{0.0f};
        glm::vec2 anchorMax{0.0f};
        glm::vec2 offsetMin{0.0f};
        glm::vec2 offsetMax{0.0f};
        glm::vec2 pivot{0.5f};
        glm::vec2 scale{1.0f};
        float rotation = 0.0f;

        vke_common::Result<void> Validate() const
        {
            for (int axis = 0; axis < 2; ++axis)
            {
                if (anchorMin[axis] < 0 || anchorMax[axis] > 1 || anchorMin[axis] > anchorMax[axis])
                    return std::unexpected("anchors must satisfy 0 <= min <= max <= 1");
                if (pivot[axis] < 0 || pivot[axis] > 1)
                    return std::unexpected("pivot must be within [0, 1]");
            }
            return {};
        }

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            auto valid = vke_common::json_validation::Object(json)
                .Vectors({"anchorMin", "anchorMax", "offsetMin", "offsetMax", "pivot", "scale"}, 2).Numbers({"rotation"}).Result();
            if (!valid) return valid;
            return RectTransform(json).Validate();
        }

        RectTransform() = default;
        explicit RectTransform(const nlohmann::json &json)
        {
            auto read = [&](const char *name, glm::vec2 &value)
            {
                if (json.contains(name)) value = {json[name][0].get<float>(), json[name][1].get<float>()};
            };
            read("anchorMin", anchorMin);
            read("anchorMax", anchorMax);
            read("offsetMin", offsetMin);
            read("offsetMax", offsetMax);
            read("pivot", pivot);
            read("scale", scale);
            rotation = json.value("rotation", 0.0f);
        }

        nlohmann::json ToJSON() const
        {
            return {{"anchorMin", {anchorMin.x, anchorMin.y}}, {"anchorMax", {anchorMax.x, anchorMax.y}},
                    {"offsetMin", {offsetMin.x, offsetMin.y}}, {"offsetMax", {offsetMax.x, offsetMax.y}},
                    {"pivot", {pivot.x, pivot.y}}, {"scale", {scale.x, scale.y}}, {"rotation", rotation}};
        }
    };
}

#endif
