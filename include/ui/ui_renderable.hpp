#ifndef UI_RENDERABLE_COMPONENT_H
#define UI_RENDERABLE_COMPONENT_H

#include <ui/ui.hpp>
#include <render/render.hpp>
#include <memory>
#include <vector>
#include <utility>

namespace vke_render
{
    class Layered2DRenderUnit;
}

namespace vke_ui
{
    class UIRenderableComponent : public UIComponent
    {
    public:
        explicit UIRenderableComponent(const UINodeData &data,
                                       std::shared_ptr<vke_render::Material> componentMaterial,
                                       const std::shared_ptr<vke_common::Font> &font = nullptr)
            : UIComponent(data), material(std::move(componentMaterial))
        {
            vke_render::Layered2DRenderer *renderer = vke_render::Renderer::GetLayered2DRenderer();

            if (material == nullptr)
                material = renderer->GetDefaultMaterial();

            id = renderer->AllocateUnit(material, glm::mat4(1.0f), quadIDs, font);
            renderUnit = renderer->GetUnit(id);
        }

        ~UIRenderableComponent() override = default;

        UIRenderableComponent(const UIRenderableComponent &) = delete;
        UIRenderableComponent &operator=(const UIRenderableComponent &) = delete;

    protected:
        void setRenderFont(const std::shared_ptr<vke_common::Font> &font)
        {
            vke_render::Renderer::GetLayered2DRenderer()->UpdateUnitFont(id, font);
        }

        void unload() override
        {
            vke_render::Layered2DRenderer *renderer = vke_render::Renderer::GetLayered2DRenderer();
            renderer->DestroyUnit(id);
            vke_render::Renderer::GetQuadManager()->Release(quadIDs);
        }

        void updateRenderTransform(const glm::mat4 &matrix, float order) override
        {
            renderUnit->modelMatrix = matrix;
            renderUnit->modelMatrix[3].z = order;
        }

        bool setGeometry(std::vector<vke_render::QuadInstanceGPU> newQuads)
        {
            if (!setQuads(std::move(newQuads)))
                return false;

            vke_render::Layered2DRenderer *renderer = vke_render::Renderer::GetLayered2DRenderer();
            return renderer->UpdateUnitQuadIDs(id, quadIDs);
        }

        void setQuadColor(const glm::vec4 &color)
        {
            vke_render::QuadManager *quadManager = vke_render::Renderer::GetQuadManager();
            for (vke_render::QuadID quadID : quadIDs)
                quadManager->UpdateColor(quadID, color);
        }

        std::shared_ptr<vke_render::Material> material;
        std::vector<vke_render::QuadID> quadIDs;

    private:
        vke_ds::id32_t id;
        vke_render::Layered2DRenderUnit *renderUnit;

        bool setQuads(std::vector<vke_render::QuadInstanceGPU> newQuads)
        {
            vke_render::QuadManager *quadManager = vke_render::Renderer::GetQuadManager();
            if (!quadManager->CanAllocate(newQuads.size(), quadIDs.size()))
                return false;

            quadManager->Release(quadIDs);
            std::vector<vke_render::QuadID> newQuadIDs;
            quadManager->Allocate(newQuads, newQuadIDs);
            quadIDs = std::move(newQuadIDs);
            return true;
        }
    };
}

#endif
