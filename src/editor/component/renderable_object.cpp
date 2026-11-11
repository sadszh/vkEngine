#include <editor/editor.hpp>
#include <component/renderable_object.hpp>

namespace vke_editor
{
    static std::string AssetDisplayName(vke_common::AssetHandle handle, const char *name)
    {
        if (handle == 0)
            return "0  <none>";
        return std::to_string(handle) + "  " + (name ? name : "<missing>");
    }

    void Editor::drawRenderableObjectComponent()
    {
        if (selectedEntity == entt::null ||
            !sceneManager->registry.all_of<vke_component::RenderableObject>(selectedEntity))
            return;

        if (!ImGui::TreeNodeEx("RenderableObject", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        vke_component::RenderableObject &renderable =
            sceneManager->registry.get<vke_component::RenderableObject>(selectedEntity);
        const vke_common::AssetHandle materialHandle =
            renderable.material == nullptr ? 0 : renderable.material->handle;
        const vke_common::AssetHandle meshHandle =
            renderable.renderUnit == nullptr || renderable.renderUnit->mesh == nullptr
                ? 0
                : renderable.renderUnit->mesh->handle;

        {
            auto *mat = ShowAssetReadResult(vke_common::AssetManager::GetMaterialAsset(materialHandle));
            const std::string selectedMaterial = AssetDisplayName(materialHandle, mat ? mat->name.c_str() : nullptr);
            if (ImGui::BeginCombo("Material", selectedMaterial.c_str()))
            {
                ShowAssetIterationResult(vke_common::AssetManager::IterateMaterialAsset([&](const vke_common::MaterialAsset &asset)
                                                                                        {
                    const vke_common::AssetHandle assetHandle = asset.id;
                    const bool selected = assetHandle == materialHandle;
                    const std::string materialLabel = std::to_string(assetHandle) + "  " + asset.name;
                    if (ImGui::Selectable(materialLabel.c_str(), selected))
                    {
                        auto material = vke_common::AssetManager::LoadMaterial(assetHandle);
                        if (material)
                            renderable.SetMaterial(*material);
                        else
                            VKE_LOG_ERROR("Cannot set material: {}", material.error())
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus(); }));
                ImGui::EndCombo();
            }
        }

        bool castsShadow = renderable.castsShadow;
        if (ImGui::Checkbox("Cast Shadow", &castsShadow))
            renderable.SetCastShadow(castsShadow);

        {
            auto *meshAsset = ShowAssetReadResult(vke_common::AssetManager::GetMeshAsset(meshHandle));
            const std::string selectedMesh = AssetDisplayName(meshHandle, meshAsset ? meshAsset->name.c_str() : nullptr);
            if (ImGui::BeginCombo("Mesh", selectedMesh.c_str()))
            {
                ShowAssetIterationResult(vke_common::AssetManager::IterateMeshAsset([&](const vke_common::MeshAsset &asset)
                                                                                    {
                    const vke_common::AssetHandle assetHandle = asset.id;
                    const bool selected = assetHandle == meshHandle;
                    const std::string meshLabel = std::to_string(assetHandle) + "  " + asset.name;
                    if (ImGui::Selectable(meshLabel.c_str(), selected))
                    {
                        auto loaded = vke_common::AssetManager::LoadMesh(assetHandle);
                        if (loaded)
                        {
                            std::shared_ptr<const vke_render::Mesh> mesh = std::move(*loaded);
                            renderable.SetMesh(mesh);
                        }
                        else
                            VKE_LOG_ERROR("Cannot set mesh: {}", loaded.error())
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus(); }));
                ImGui::EndCombo();
            }
        }

        ImGui::TreePop();
    }
}
