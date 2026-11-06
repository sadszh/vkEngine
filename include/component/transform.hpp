#ifndef TRANSFORM_H
#define TRANSFORM_H

#include <json_validation.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <entt/entity/entity.hpp>
#include <set>

namespace vke_common
{
    struct TransformData
    {
        glm::quat localRotation = glm::quat(glm::vec3(0));
        glm::vec3 localPosition = glm::vec3(0);
        glm::vec3 localScale = glm::vec3(1);

        // Call before constructing from JSON; asset checks happen in LoadAssets where applicable.
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            auto result = Object(json).Require({"pos", "scl", "rot"})
                .Vectors({"pos", "scl"}, 3).Vectors({"rot"}, 4).Result();
            if (!result) return result;
            double length = 0;
            for (const auto &value : json["rot"]) length += value.get<double>() * value.get<double>();
            if (length <= 0) return std::unexpected("invalid transform rotation");
            return {};
        }

        TransformData() = default;
        TransformData(const nlohmann::json &json)
        {
            const auto &pos = json["pos"];
            const auto &scl = json["scl"];
            const auto &rot = json["rot"];
            localPosition = glm::vec3(pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>());
            localScale = glm::vec3(scl[0].get<float>(), scl[1].get<float>(), scl[2].get<float>());
            localRotation = glm::normalize(glm::quat(rot[3].get<float>(), rot[0].get<float>(), rot[1].get<float>(), rot[2].get<float>()));
        }
        nlohmann::json ToJSON() const
        {
            return {
                {"pos", {localPosition[0], localPosition[1], localPosition[2]}},
                {"scl", {localScale[0], localScale[1], localScale[2]}},
                {"rot", {localRotation[0], localRotation[1], localRotation[2], localRotation[3]}}};
        }
    };

    struct Transform
    {
        glm::mat4 model;
        glm::quat localRotation;
        glm::vec3 localPosition;
        glm::vec3 localScale;
        entt::entity parent = entt::null;
        std::set<entt::entity> children;

        Transform(const Transform &) = delete;
        Transform &operator=(const Transform &) = delete;
        Transform(Transform &&) noexcept = default;
        Transform &operator=(Transform &&) noexcept = default;

        Transform(glm::vec3 pos, glm::vec3 scl, glm::quat rot)
            : localRotation(rot), localPosition(pos), localScale(scl)
        {
            calcModelMatrix();
        }

        Transform(const TransformData &componentData)
            : localRotation(componentData.localRotation),
              localPosition(componentData.localPosition),
              localScale(componentData.localScale)
        {
            calcModelMatrix();
        }

        void FillData(TransformData &data) const
        {
            data.localPosition = localPosition;
            data.localScale = localScale;
            data.localRotation = localRotation;
        }

        glm::vec3 GetGlobalPosition() const
        {
            return glm::vec3(model[3]);
        }

        glm::vec3 GetLossyScale() const
        {
            return glm::vec3(
                glm::length(glm::vec3(model[0])),
                glm::length(glm::vec3(model[1])),
                glm::length(glm::vec3(model[2])));
        }

        glm::quat GetGlobalRotation() const
        {
            glm::vec3 scale = GetLossyScale();
            if (scale.x == 0 || scale.y == 0 || scale.z == 0)
                return glm::quat(1, 0, 0, 0);
            glm::mat3 rotMat(
                glm::vec3(model[0]) / scale.x,
                glm::vec3(model[1]) / scale.y,
                glm::vec3(model[2]) / scale.z);
            return glm::quat_cast(rotMat);
        }

        void RemoveParent()
        {
            localPosition = GetGlobalPosition();
            localScale = GetLossyScale();
            localRotation = glm::normalize(GetGlobalRotation());
            calcModelMatrix();
        }

        void SetParent(const Transform &fa)
        {
            glm::mat4 inv(glm::inverse(fa.model));

            localPosition = inv * glm::vec4(GetGlobalPosition(), 1);
            localRotation = glm::normalize(glm::inverse(fa.GetGlobalRotation()) * GetGlobalRotation());
            localScale = GetLossyScale() / fa.GetLossyScale();
            calcModelMatrixWithParent(fa.model);
        }

        void SetParentFixedLocal(const Transform &fa)
        {
            calcModelMatrixWithParent(fa.model);
        }

        void SetLocalPosition(const glm::vec3 &pos)
        {
            localPosition = pos;
            calcModelMatrix();
        }

        void SetLocalPositionWithParent(const glm::mat4 &fa, const glm::vec3 &pos)
        {
            localPosition = pos;
            calcModelMatrixWithParent(fa);
        }

        void SetGlobalPosition(const glm::vec3 &pos)
        {
            localPosition = pos;
            calcModelMatrix();
        }

        void SetGlobalPositionWithParent(const glm::mat4 &fa, const glm::vec3 &pos)
        {
            localPosition = glm::vec3(glm::inverse(fa) * glm::vec4(pos, 1.0f));
            calcModelMatrixWithParent(fa);
        }

        void TranslateLocal(const glm::vec3 &det)
        {
            localPosition += localRotation * det;
            calcModelMatrix();
        }

        void TranslateLocalWithParent(const glm::mat4 &fa, const glm::vec3 &det)
        {
            localPosition += localRotation * det;
            calcModelMatrixWithParent(fa);
        }

        void TranslateGlobal(const glm::vec3 &det)
        {
            localPosition += det;
            calcModelMatrix();
        }

        void TranslateGlobalWithParent(const glm::mat4 &fa, const glm::vec3 &det)
        {
            localPosition = glm::inverse(fa) * glm::vec4(GetGlobalPosition() + det, 1.0f);
            calcModelMatrixWithParent(fa);
        }

        void SetLocalRotation(const glm::quat &rot)
        {
            localRotation = glm::normalize(rot);
            calcModelMatrix();
        }

        void SetLocalRotationWithParent(const Transform &fa, const glm::quat &rot)
        {
            localRotation = glm::normalize(rot);
            calcModelMatrixWithParent(fa.model);
        }

        void SetGlobalRotation(const glm::quat &rot)
        {
            localRotation = glm::normalize(rot);
            calcModelMatrix();
        }

        void SetGlobalRotationWithParent(const Transform &fa, const glm::quat &rot)
        {
            localRotation = glm::normalize(glm::inverse(fa.GetGlobalRotation()) * rot);
            calcModelMatrixWithParent(fa.model);
        }

        void RotateLocal(const float det, const glm::vec3 &axis)
        {
            localRotation = glm::normalize(glm::rotate(localRotation, det, axis));
            calcModelMatrix();
        }

        void RotateLocalWithParent(const Transform &fa, const float det, const glm::vec3 &axis)
        {
            localRotation = glm::normalize(glm::rotate(localRotation, det, axis));
            calcModelMatrixWithParent(fa.model);
        }

        void RotateGlobal(const float det, const glm::vec3 &axis)
        {
            localRotation = glm::normalize(glm::angleAxis(det, glm::normalize(axis)) * localRotation);
            calcModelMatrix();
        }

        void RotateGlobalWithParent(const Transform &fa, const float det, const glm::vec3 &axis)
        {
            glm::quat parentRot = fa.GetGlobalRotation();
            localRotation = glm::normalize(glm::inverse(parentRot) * glm::angleAxis(det, glm::normalize(axis)) * parentRot * localRotation);
            calcModelMatrixWithParent(fa.model);
        }

        void SetLocalScale(const glm::vec3 &scale)
        {
            localScale = scale;
            calcModelMatrix();
        }

        void SetLocalScaleWithParent(const Transform &fa, const glm::vec3 &scale)
        {
            localScale = scale;
            calcModelMatrixWithParent(fa.model);
        }

        void Scale(const glm::vec3 &scale)
        {
            localScale *= scale;
            calcModelMatrix();
        }

        void ScaleWithParent(const Transform &fa, const glm::vec3 &scale)
        {
            localScale *= scale;
            calcModelMatrixWithParent(fa.model);
        }

        void UpdateWithParent(const Transform &fa)
        {
            initWithParent(fa);
        }

    private:
        void calcModelMatrix()
        {
            model = glm::translate(glm::mat4(1.0f), localPosition) *
                    glm::mat4_cast(localRotation) *
                    glm::scale(glm::mat4(1.0f), localScale);
        }

        void calcModelMatrixWithParent(const glm::mat4 &fa)
        {
            model = fa * glm::translate(glm::mat4(1.0f), localPosition) *
                    glm::mat4_cast(localRotation) *
                    glm::scale(glm::mat4(1.0f), localScale);
        }

        void initWithParent(const Transform &fa)
        {
            calcModelMatrixWithParent(fa.model);
        }
    };
}

#endif
