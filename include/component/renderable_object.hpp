#ifndef RDOBJECT_H
#define RDOBJECT_H

#include <json_validation.hpp>
#include <utility>
#include <render/render.hpp>
#include <render/buffer.hpp>
#include <asset/asset_manager.hpp>
#include <asset/asset_ref.hpp>
#include <component/transform.hpp>

namespace vke_component
{
    struct RenderableObjectData
    {
        vke_common::AssetRef<vke_render::Material> material;
        vke_common::AssetRef<const vke_render::Mesh> mesh;
        std::vector<glm::ivec4> textureIndices;
        bool castsShadow = true;

        // Call before constructing from JSON; asset checks happen in ValidateAssets.
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            auto result = Object(json).Require({"material", "mesh"}).Unsigneds({"material", "mesh"}).Booleans({"castsShadow"}).Result();
            if (!result)
                return result;
            if (json.contains("textureIndices"))
            {
                if (!json["textureIndices"].is_array())
                    return std::unexpected("invalid textureIndices");
                for (const auto &row : json["textureIndices"])
                {
                    if (!row.is_array() || row.size() != 3)
                        return std::unexpected("invalid textureIndices row");
                    for (const auto &index : row)
                        if (!index.is_number_integer() || index.get<double>() < INT32_MIN || index.get<double>() > INT32_MAX)
                            return std::unexpected("invalid texture index");
                }
            }
            return {};
        }

        RenderableObjectData() = default;

        // Built-in resources for default creation; ordinary construction keeps asset references empty.
        static RenderableObjectData Default()
        {
            RenderableObjectData data;
            data.material.SetHandle(vke_common::BUILTIN_MATERIAL_DEFAULT_ID);
            data.mesh.SetHandle(vke_common::BUILTIN_MESH_SPHERE_ID);
            return data;
        }

        RenderableObjectData(const nlohmann::json &json)
            : material(json["material"].get<vke_common::AssetHandle>()),
              mesh(json["mesh"].get<vke_common::AssetHandle>()),
              castsShadow(json.value("castsShadow", true))
        {
            if (json.contains("textureIndices"))
                for (const auto &index : json["textureIndices"])
                    textureIndices.emplace_back(index[0].get<int>(), index[1].get<int>(), index[2].get<int>(), 0);
        }
        vke_common::Result<void> ValidateAssets() const
        {
            using vke_common::AssetManager;
            if (!material.Get())
                if (auto result = AssetManager::ValidateMaterial(material.Handle()); !result)
                    return std::unexpected("material: " + result.error());
            if (!mesh.Get())
                if (auto result = AssetManager::ValidateMesh(mesh.Handle()); !result)
                    return std::unexpected("mesh: " + result.error());
            return {};
        }

        // Requires successful ValidateAssets() before loading.
        vke_common::Result<void> LoadAssets()
        {
            using vke_common::AssetManager;
            if (!material.Get())
            {
                auto loaded = AssetManager::LoadMaterial(material.Handle());
                if (!loaded) return std::unexpected(loaded.error());
                material.Resolve(std::move(*loaded));
            }
            if (!mesh.Get())
            {
                auto loaded = AssetManager::LoadMesh(mesh.Handle());
                if (!loaded) return std::unexpected(loaded.error());
                mesh.Resolve(std::move(*loaded));
            }
            return {};
        }

        nlohmann::json ToJSON() const
        {
            nlohmann::json indices = nlohmann::json::array();
            for (const glm::ivec4 &index : textureIndices)
                indices.push_back({index.x, index.y, index.z});
            return {{"type", "renderableObject"}, {"material", material.Handle()}, {"mesh", mesh.Handle()}, {"textureIndices", indices}, {"castsShadow", castsShadow}};
        }
    };

    class RenderableObject
    {
    public:
        std::shared_ptr<vke_render::Material> material;
        std::vector<glm::ivec4> textureIndices;
        std::unique_ptr<vke_render::RenderUnit> renderUnit;
        std::unique_ptr<vke_render::RenderUnit> shadowRenderUnit;
        bool castsShadow;

        RenderableObject(const RenderableObject &) = delete;
        RenderableObject &operator=(const RenderableObject &) = delete;

        RenderableObject(RenderableObject &&other) noexcept
            : material(std::move(other.material)),
              textureIndices(std::move(other.textureIndices)),
              renderUnit(std::move(other.renderUnit)),
              shadowRenderUnit(std::move(other.shadowRenderUnit)),
              castsShadow(other.castsShadow),
              renderID(other.renderID),
              shadowRenderID(std::exchange(other.shadowRenderID, 0)) {}

        RenderableObject &operator=(RenderableObject &&other) noexcept
        {
            if (this != &other)
            {
                if (renderUnit)
                {
                    vke_render::Renderer::WaitIdle();
                    Unload();
                }
                material = std::move(other.material);
                textureIndices = std::move(other.textureIndices);
                renderUnit = std::move(other.renderUnit);
                shadowRenderUnit = std::move(other.shadowRenderUnit);
                castsShadow = other.castsShadow;
                renderID = other.renderID;
                shadowRenderID = std::exchange(other.shadowRenderID, 0);
            }
            return *this;
        }

        RenderableObject(const vke_common::Transform &transform, const RenderableObjectData &componentData)
            : material(componentData.material.Get()),
              textureIndices(componentData.textureIndices), castsShadow(componentData.castsShadow),
              shadowRenderID(0)
        {
            auto mesh = componentData.mesh.Get();
            renderUnit = textureIndices.size() == 0 ? std::make_unique<vke_render::RenderUnit>(mesh, transform.model)
                                                    : std::make_unique<vke_render::RenderUnit>(mesh,
                                                                                               transform.model,
                                                                                               std::vector<vke_render::PushConstantInfo>{vke_render::PushConstantInfo(static_cast<uint32_t>(sizeof(glm::ivec4)),
                                                                                                                                                                      textureIndices.data(), false, static_cast<uint32_t>(sizeof(glm::mat4)))},
                                                                                               0);
            shadowRenderUnit = std::make_unique<vke_render::RenderUnit>(mesh, transform.model);
            registerRenderUnits();
        }

        ~RenderableObject() {}

        void OnTransformed(const vke_common::Transform &transform)
        {
            renderUnit->modelMatrix = transform.model;
            shadowRenderUnit->modelMatrix = transform.model;
        }

        void FillData(RenderableObjectData &data) const
        {
            data.material.SetAsset(material);
            data.mesh.SetAsset(renderUnit->mesh);
            data.textureIndices = textureIndices;
            data.castsShadow = castsShadow;
        }

        void Unload()
        {
            unloadFromEngine(material, castsShadow);
        }

        void SetMaterial(std::shared_ptr<vke_render::Material> &mat)
        {
            if (mat == nullptr || mat == material)
                return;

            unloadFromEngine(material, castsShadow);
            material = mat;
            registerRenderUnits();
        }

        void SetCastShadow(bool castShadow)
        {
            if (castShadow == castsShadow)
                return;

            castsShadow = castShadow;
            if (material->renderMode == vke_render::MaterialRenderMode::BLEND_MODE)
                return;

            vke_render::ShadowPass *shadowPass = vke_render::Renderer::GetInstance()->GetShadowPass();
            if (shadowPass == nullptr)
                return;

            if (castsShadow && shadowRenderID == 0)
                shadowRenderID = shadowPass->AddUnit(shadowRenderUnit.get());
            else if (!castsShadow && shadowRenderID != 0)
            {
                shadowPass->RemoveUnit(shadowRenderID);
                shadowRenderID = 0;
            }
        }

        void SetMesh(std::shared_ptr<const vke_render::Mesh> &mesh)
        {
            if (renderUnit != nullptr)
                renderUnit->mesh = mesh;
            if (shadowRenderUnit != nullptr)
                shadowRenderUnit->mesh = mesh;
        }

    private:
        vke_ds::id64_t renderID;
        vke_ds::id64_t shadowRenderID;

        void registerRenderUnits()
        {
            vke_render::Renderer *renderer = vke_render::Renderer::GetInstance();
            if (material->renderMode == vke_render::MaterialRenderMode::BLEND_MODE)
                renderID = renderer->GetTransparentPass()->AddUnit(material, renderUnit.get());
            else
                renderID = renderer->GetGBufferPass()->AddUnit(material, renderUnit.get());
            if (castsShadow && material->renderMode != vke_render::MaterialRenderMode::BLEND_MODE)
            {
                vke_render::ShadowPass *shadowPass = renderer->GetShadowPass();
                if (shadowPass != nullptr)
                    shadowRenderID = shadowPass->AddUnit(shadowRenderUnit.get());
            }
        }

        void unloadFromEngine(std::shared_ptr<vke_render::Material> &mat, bool castShadow)
        {
            vke_render::Renderer *renderer = vke_render::Renderer::GetInstance();
            if (mat->renderMode == vke_render::MaterialRenderMode::BLEND_MODE)
                renderer->GetTransparentPass()->RemoveUnit(renderID);
            else
                renderer->GetGBufferPass()->RemoveUnit(mat.get(), renderID);

            if (castShadow && mat->renderMode != vke_render::MaterialRenderMode::BLEND_MODE && shadowRenderID != 0)
            {
                vke_render::ShadowPass *shadowPass = renderer->GetShadowPass();
                if (shadowPass != nullptr)
                    shadowPass->RemoveUnit(shadowRenderID);
                shadowRenderID = 0;
            }
        }
    };
}

#endif
