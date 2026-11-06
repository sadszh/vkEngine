#pragma once

#include <ui/ui_renderable.hpp>
#include <asset/asset_manager.hpp>
#include <asset/asset_ref.hpp>

namespace vke_ui
{
    struct UIImageData : UINodeData
    {
        vke_common::AssetRef<vke_render::Texture2D> texture;
        glm::vec4 color{1.0f};
        glm::vec4 uvRect{0, 0, 1, 1};
        bool preserveAspect = false;

        UIImageData() = default;
        explicit UIImageData(const nlohmann::json &json)
            : UINodeData(json), texture(json.value("texture", vke_common::AssetHandle{0})),
              preserveAspect(json.value("preserveAspect", false))
        {
            if (json.contains("color"))
                for (int i = 0; i < 4; ++i) color[i] = json["color"][i].get<float>();
            if (json.contains("uvRect"))
                for (int i = 0; i < 4; ++i) uvRect[i] = json["uvRect"][i].get<float>();
        }

        static vke_common::Result<void> ValidateUVRect(const glm::vec4 &value)
        {
            for (int i = 0; i < 4; ++i)
                if (value[i] < 0 || value[i] > 1)
                    return std::unexpected("image UV coordinates must be in [0, 1]");
            if (value.x > value.z || value.y > value.w)
                return std::unexpected("image UV minimum must not exceed maximum");
            return {};
        }

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            if (auto valid = Object(json).Unsigneds({"texture"}).Vectors({"color", "uvRect"}, 4)
                    .Booleans({"preserveAspect"}).Result(); !valid) return valid;
            if (json.contains("uvRect"))
            {
                const auto &uv = json["uvRect"];
                if (auto valid = ValidateUVRect({uv[0].get<float>(), uv[1].get<float>(),
                                                uv[2].get<float>(), uv[3].get<float>()}); !valid) return valid;
            }
            return validateCommonJSON(json);
        }

        UIType GetType() const override { return UIType::Image; }
        std::unique_ptr<UINodeData> Clone() const override { return std::make_unique<UIImageData>(*this); }

        vke_common::Result<void> Validate() const override
        {
            if (auto valid = ValidateUVRect(uvRect); !valid) return valid;
            return UINodeData::Validate();
        }

        vke_common::Result<void> ValidateAssets() const override
        {
            if (texture.Handle() != 0 && !texture.Get())
                if (auto valid = vke_common::AssetManager::ValidateTexture2D(texture.Handle()); !valid)
                    return std::unexpected("image texture: " + valid.error());
            return UINodeData::ValidateAssets();
        }

        vke_common::Result<void> LoadAssets() override
        {
            if (texture.Handle() != 0 && !texture.Get())
            {
                texture.Resolve(vke_common::AssetManager::LoadTexture2D(texture.Handle()));
                if (!texture.Get())
                    return std::unexpected("image texture " + std::to_string(texture.Handle()) + ": loading failed");
            }
            return UINodeData::LoadAssets();
        }

        nlohmann::json ToJSON() const override
        {
            auto json = UINodeData::ToJSON();
            json["type"] = "uiImage";
            json["texture"] = texture.Handle();
            json["color"] = {color.r, color.g, color.b, color.a};
            json["uvRect"] = {uvRect.x, uvRect.y, uvRect.z, uvRect.w};
            json["preserveAspect"] = preserveAspect;
            return json;
        }
    };

    class UIImage : public UIRenderableComponent
    {
    public:
        explicit UIImage(const UIImageData &data)
            : UIRenderableComponent(data, vke_render::Renderer::GetLayered2DRenderer()->GetImageMaterial()),
              texture(data.texture), preserveAspect(data.preserveAspect)
        {
            auto registered = vke_render::Renderer::GetLayered2DRenderer()->RegisterImageTexture(texture.Get());
            if (!registered)
                VKE_LOG_ERROR("Cannot render UIImage {}: {}", name, registered.error());
            vke_render::QuadInstanceGPU quad{};
            quad.color = data.color;
            quad.uvRect = data.uvRect;
            quad.atlasInfo.x = registered.value_or(UINT32_MAX);
            updateGeometry(quad);
            if (quadIDs.empty() && registered)
                vke_render::Renderer::GetLayered2DRenderer()->UnregisterImageTexture(*registered);
        }

        UIType GetType() const override { return UIType::Image; }
        std::unique_ptr<UINodeData> ToData() const override
        {
            auto data = std::make_unique<UIImageData>();
            fillImageData(*data);
            return data;
        }

        vke_common::Result<void> SetTexture(vke_common::AssetHandle handle)
        {
            using vke_common::AssetManager;
            if (handle != 0)
                if (auto valid = AssetManager::ValidateTexture2D(handle); !valid) return valid;
            auto loaded = handle ? AssetManager::LoadTexture2D(handle) : nullptr;
            if (handle && !loaded) return std::unexpected("image texture loading failed");
            if (quadIDs.empty()) return std::unexpected("image quad is not allocated");
            auto *renderer = vke_render::Renderer::GetLayered2DRenderer();
            auto registered = renderer->RegisterImageTexture(loaded);
            if (!registered) return std::unexpected(registered.error());
            auto quad = getQuad();
            if (quad.atlasInfo.x != UINT32_MAX)
                renderer->UnregisterImageTexture(quad.atlasInfo.x);
            texture.SetAsset(std::move(loaded));
            invalidateLayout();
            quad.atlasInfo.x = *registered;
            updateGeometry(quad);
            return {};
        }

        virtual vke_common::Result<void> SetColor(const glm::vec4 &value)
        {
            if (quadIDs.empty()) return std::unexpected("image quad is not allocated");
            setQuadColor(value);
            return {};
        }

        vke_common::Result<void> SetUVRect(const glm::vec4 &value)
        {
            if (auto valid = UIImageData::ValidateUVRect(value); !valid) return valid;
            if (quadIDs.empty()) return std::unexpected("image quad is not allocated");
            auto quad = getQuad();
            quad.uvRect = value;
            invalidateLayout();
            updateGeometry(quad);
            return {};
        }

        void SetPreserveAspect(bool value)
        {
            preserveAspect = value;
            onSizeChanged();
        }

        vke_common::AssetHandle GetTexture() const { return texture.Handle(); }
        virtual glm::vec4 GetColor() const { return getQuad().color; }
        glm::vec4 GetUVRect() const { return getQuad().uvRect; }
        bool GetPreserveAspect() const { return preserveAspect; }

    protected:
        void fillImageData(UIImageData &data) const
        {
            fillData(data);
            data.texture = texture;
            data.color = GetColor();
            data.uvRect = GetUVRect();
            data.preserveAspect = preserveAspect;
        }

    private:
        vke_common::AssetRef<vke_render::Texture2D> texture;
        bool preserveAspect;

        glm::vec2 measureContent(std::optional<float> width) const override
        {
            const auto &asset = texture.Get();
            if (!asset) return glm::vec2(0);
            const auto uv = GetUVRect();
            return glm::vec2(asset->width, asset->height) * glm::vec2(uv.z - uv.x, uv.w - uv.y);
        }

        void unload() override
        {
            const auto index = getQuad().atlasInfo.x;
            if (index != UINT32_MAX)
                vke_render::Renderer::GetLayered2DRenderer()->UnregisterImageTexture(index);
            UIRenderableComponent::unload();
        }

        vke_render::QuadInstanceGPU getQuad() const
        {
            if (!quadIDs.empty())
                return vke_render::Renderer::GetQuadManager()->Get(quadIDs.front());
            vke_render::QuadInstanceGPU quad{};
            quad.color = glm::vec4(1.0f);
            quad.uvRect = {0, 0, 1, 1};
            quad.atlasInfo.x = UINT32_MAX;
            return quad;
        }

        void onSizeChanged() override
        {
            if (!quadIDs.empty()) updateGeometry(getQuad());
        }

        void updateGeometry(vke_render::QuadInstanceGPU quad)
        {
            glm::vec2 size = quad.atlasInfo.x == UINT32_MAX ? glm::vec2(0) : resolvedSize;
            if (preserveAspect)
            {
                const auto &asset = texture.Get();
                const glm::vec2 pixels = glm::vec2(asset ? asset->width : 1, asset ? asset->height : 1) *
                    glm::vec2(quad.uvRect.z - quad.uvRect.x, quad.uvRect.w - quad.uvRect.y);
                size = pixels.x > 0 && pixels.y > 0
                    ? pixels * glm::min(size.x / pixels.x, size.y / pixels.y) : glm::vec2(0);
            }
            const auto offset = (resolvedSize - size) * 0.5f;
            quad.quadRect = {offset.x, offset.y, offset.x + size.x, offset.y + size.y};
            if (!quadIDs.empty())
                vke_render::Renderer::GetQuadManager()->Update(quadIDs.front(), quad);
            else if (!setGeometry({quad}))
                VKE_LOG_ERROR("Cannot allocate UIImage quad for {}", name);
        }
    };
}
