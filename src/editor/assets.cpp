#include <editor/editor.hpp>
#include <imgui_impl_vulkan.h>
#include <common.hpp>
#include <algorithm>
#include <cstring>

namespace vke_editor
{
    const std::filesystem::path EditorAssetLUTPath = "editor_assets/assets.json";
    static const char *AssetBrowserModeName(AssetBrowserMode mode)
    {
        switch (mode)
        {
        case AssetBrowserMode::ByDirectory:
            return "Directory";
        default:
            return "Type";
        }
    }

    static const char *AssetTypeName(vke_common::AssetType type)
    {
        return vke_common::AssetTypeToName[static_cast<int>(type)].c_str();
    }

    static const char *RenderModeName(vke_render::MaterialRenderMode mode)
    {
        switch (mode)
        {
        case vke_render::MaterialRenderMode::CUTOFF_MODE:
            return "Cutoff";
        case vke_render::MaterialRenderMode::BLEND_MODE:
            return "Blend";
        default:
            return "Opaque";
        }
    }

    static const char *BlendModeName(vke_render::MaterialBlendMode mode)
    {
        switch (mode)
        {
        case vke_render::MaterialBlendMode::PREMULTIPLIED_ALPHA:
            return "Premultiplied Alpha";
        case vke_render::MaterialBlendMode::ADDITIVE:
            return "Additive";
        default:
            return "Alpha";
        }
    }

    static std::string TextureDisplayName(vke_common::AssetHandle handle)
    {
        vke_common::TextureAsset *texture = ShowAssetReadResult(vke_common::AssetManager::GetTextureAsset(handle));
        if (texture == nullptr)
            return std::to_string(handle) + "  <missing>";
        return std::to_string(handle) + "  " + texture->name;
    }

    static bool GetAssetInfo(vke_common::AssetType type, vke_common::AssetHandle handle, AssetTreeEntry &entry)
    {
        switch (type)
        {
        case vke_common::ASSET_TEXTURE:
            if (auto *asset = ShowAssetReadResult(vke_common::AssetManager::GetTextureAsset(handle)))
            {
                entry = {type, handle, asset->name, vke_common::PathToUTF8(asset->path)};
                return true;
            }
            break;
        case vke_common::ASSET_MESH:
            if (auto *asset = ShowAssetReadResult(vke_common::AssetManager::GetMeshAsset(handle)))
            {
                entry = {type, handle, asset->name, vke_common::PathToUTF8(asset->path)};
                return true;
            }
            break;
        case vke_common::ASSET_VF_SHADER:
            if (auto *asset = ShowAssetReadResult(vke_common::AssetManager::GetVFShaderAsset(handle)))
            {
                entry = {type, handle, asset->name, vke_common::PathToUTF8(asset->path)};
                return true;
            }
            break;
        case vke_common::ASSET_COMPUTE_SHADER:
            if (auto *asset = ShowAssetReadResult(vke_common::AssetManager::GetComputeShaderAsset(handle)))
            {
                entry = {type, handle, asset->name, vke_common::PathToUTF8(asset->path)};
                return true;
            }
            break;
        case vke_common::ASSET_MATERIAL:
            if (auto *asset = ShowAssetReadResult(vke_common::AssetManager::GetMaterialAsset(handle)))
            {
                entry = {type, handle, asset->name, vke_common::PathToUTF8(asset->path)};
                return true;
            }
            break;
        case vke_common::ASSET_SKELETON:
            if (auto *asset = ShowAssetReadResult(vke_common::AssetManager::GetSkeletonAsset(handle)))
            {
                entry = {type, handle, asset->name, vke_common::PathToUTF8(asset->path)};
                return true;
            }
            break;
        case vke_common::ASSET_ANIMATION:
            if (auto *asset = ShowAssetReadResult(vke_common::AssetManager::GetAnimationAsset(handle)))
            {
                entry = {type, handle, asset->name, vke_common::PathToUTF8(asset->path)};
                return true;
            }
            break;
        case vke_common::ASSET_SCENE:
            if (auto *asset = ShowAssetReadResult(vke_common::AssetManager::GetSceneAsset(handle)))
            {
                entry = {type, handle, asset->name, vke_common::PathToUTF8(asset->path)};
                return true;
            }
            break;
        case vke_common::ASSET_FONT:
            if (auto *asset = ShowAssetReadResult(vke_common::AssetManager::GetFontAsset(handle)))
            {
                entry = {type, handle, asset->name, vke_common::PathToUTF8(asset->path)};
                return true;
            }
            break;
        case vke_common::ASSET_AUDIO_CLIP:
            if (auto *asset = ShowAssetReadResult(vke_common::AssetManager::GetAudioClipAsset(handle)))
            {
                entry = {type, handle, asset->name, vke_common::PathToUTF8(asset->path)};
                return true;
            }
            break;
        default:
            break;
        }
        return false;
    }

    std::shared_ptr<vke_render::Texture2D> Editor::loadTexturePreview(vke_common::AssetHandle textureAsset)
    {
        auto error = texturePreviewErrors.find(textureAsset);
        if (error == texturePreviewErrors.end())
        {
            auto loaded = vke_common::AssetManager::LoadTexture2D(textureAsset);
            if (loaded) return std::move(*loaded);
            error = texturePreviewErrors.emplace(textureAsset, loaded.error()).first;
        }
        ImGui::TextWrapped("Preview unavailable: %s", error->second.c_str());
        const std::string retryID = "Retry##texture" + std::to_string(textureAsset);
        if (ImGui::Button(retryID.c_str())) texturePreviewErrors.erase(error);
        return nullptr;
    }

    VkDescriptorSet Editor::getTexturePreviewDescriptorSet(const vke_render::Texture2D &texture)
    {
        const auto descriptorIt = texturePreviewDescriptorSets.find(texture.handle);
        if (descriptorIt != texturePreviewDescriptorSets.end())
            return descriptorIt->second;

        VkDescriptorSet descriptorSet = ImGui_ImplVulkan_AddTexture(
            texture.textureImageView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (descriptorSet != VK_NULL_HANDLE)
            texturePreviewDescriptorSets[texture.handle] = descriptorSet;
        return descriptorSet;
    }

    void Editor::drawTexturePreview(vke_common::AssetHandle textureAsset, float maxSize)
    {
        auto texture = loadTexturePreview(textureAsset);
        if (!texture) return;
        VkDescriptorSet descriptorSet = getTexturePreviewDescriptorSet(*texture);
        if (descriptorSet == VK_NULL_HANDLE)
        {
            ImGui::TextDisabled("Preview unavailable");
            return;
        }

        const float width = static_cast<float>(texture->width);
        const float height = static_cast<float>(texture->height);
        const float scale = width <= 0.0f || height <= 0.0f ? 1.0f : std::min(maxSize / width, maxSize / height);
        ImGui::Image(
            ImTextureRef((ImTextureID)descriptorSet),
            ImVec2(width * scale, height * scale));
    }

    void Editor::showAssets()
    {
        ImGui::Begin("Assets");
        vke_common::AssetManager *assetManager = vke_common::AssetManager::GetInstance();
        const bool byType = assetBrowserMode == AssetBrowserMode::ByType;
        if (ImGui::Button(byType ? "Directory View" : "Type View"))
            assetBrowserMode = byType ? AssetBrowserMode::ByDirectory : AssetBrowserMode::ByType;
        ImGui::SameLine();
        ImGui::TextDisabled("Mode: %s", AssetBrowserModeName(assetBrowserMode));
        ImGui::Separator();

        if (assetBrowserMode == AssetBrowserMode::ByDirectory)
            showAssetsByDirectory(assetManager);
        else
            showAssetsByType(assetManager);

        ImGui::End();
    }

    void Editor::showAssetsByType(vke_common::AssetManager *assetManager)
    {
        const auto showAssetGroup = [this](const char *label, vke_common::AssetType type, auto &&iterate)
        {
            if (!ImGui::TreeNode(label))
                return;

            auto result = iterate([&](auto &asset)
                                  {
                AssetTreeEntry entry{type, asset.id, asset.name, vke_common::PathToUTF8(asset.path)};
                drawAssetEntry(entry); });

            ShowAssetIterationResult(result);
            ImGui::TreePop();
        };

        showAssetGroup("Textures", vke_common::ASSET_TEXTURE, [](auto &&op)
                       { return vke_common::AssetManager::IterateTextureAsset(op); });
        showAssetGroup("Materials", vke_common::ASSET_MATERIAL, [](auto &&op)
                       { return vke_common::AssetManager::IterateMaterialAsset(op); });
        showAssetGroup("VF Shaders", vke_common::ASSET_VF_SHADER, [](auto &&op)
                       { return vke_common::AssetManager::IterateVFShaderAsset(op); });
        showAssetGroup("Compute Shaders", vke_common::ASSET_COMPUTE_SHADER, [](auto &&op)
                       { return vke_common::AssetManager::IterateComputeShaderAsset(op); });
        showAssetGroup("Meshes", vke_common::ASSET_MESH, [](auto &&op)
                       { return vke_common::AssetManager::IterateMeshAsset(op); });
        showAssetGroup("Skeletons", vke_common::ASSET_SKELETON, [](auto &&op)
                       { return vke_common::AssetManager::IterateSkeletonAsset(op); });
        showAssetGroup("Scenes", vke_common::ASSET_SCENE, [](auto &&op)
                       { return vke_common::AssetManager::IterateSceneAsset(op); });
        showAssetGroup("Fonts", vke_common::ASSET_FONT, [](auto &&op)
                       { return vke_common::AssetManager::IterateFontAsset(op); });
        showAssetGroup("Animations", vke_common::ASSET_ANIMATION, [](auto &&op)
                       { return vke_common::AssetManager::IterateAnimationAsset(op); });
        showAssetGroup("Audio Clips", vke_common::ASSET_AUDIO_CLIP, [](auto &&op)
                       { return vke_common::AssetManager::IterateAudioClipAsset(op); });
    }

    void Editor::showAssetsByDirectory(vke_common::AssetManager *assetManager)
    {
        if (assetDirectoryTree.name.empty())
            if (auto result = rebuildAssetDirectoryTree(assetManager); !result)
            {
                ShowAssetIterationResult(result);
                return;
            }

        if (assetDirectoryTree.children.empty() && assetDirectoryTree.assets.empty())
        {
            ImGui::TextDisabled("No assets");
            return;
        }
        drawAssetDirectoryNode(assetDirectoryTree);
    }

    void Editor::drawAssetDirectoryNode(const AssetTreeNode &node)
    {
        for (const auto &entry : node.assets)
            drawAssetEntry(entry);

        for (const auto &[name, child] : node.children)
        {
            ImGui::PushID(name.c_str());
            if (ImGui::TreeNodeEx(
                    child.name.c_str(),
                    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick))
            {
                drawAssetDirectoryNode(child);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    void Editor::drawAssetEntry(const AssetTreeEntry &entry)
    {
        ImGui::PushID(static_cast<int>(entry.type));
        ImGui::PushID(static_cast<int>(entry.id));
        const std::string label = "[" + std::string(AssetTypeName(entry.type)) + "] " +
                                  std::to_string(entry.id) + "  " + entry.name;
        const bool selected = selectedAssetType == entry.type && selectedAsset == entry.id;
        if (ImGui::Selectable(label.c_str(), selected))
            selectAsset(entry.type, entry.id);

        if (!entry.path.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", entry.path.c_str());
        ImGui::PopID();
        ImGui::PopID();
    }

    void Editor::selectAsset(vke_common::AssetType assetType, vke_common::AssetHandle asset)
    {
        AssetTreeEntry entry;
        if (!GetAssetInfo(assetType, asset, entry))
            return;

        selectedEntity = entt::null;
        selectedAssetType = assetType;
        selectedAsset = asset;
    }

    void Editor::clearSelectedAsset()
    {
        selectedAssetType = vke_common::ASSET_CNT_FLAG;
        selectedAsset = 0;
    }

    void Editor::disposeTexturePreviewDescriptorSets()
    {
        for (auto &[asset, descriptorSet] : texturePreviewDescriptorSets)
        {
            if (descriptorSet != VK_NULL_HANDLE)
                ImGui_ImplVulkan_RemoveTexture(descriptorSet);
        }
        texturePreviewDescriptorSets.clear();
        texturePreviewErrors.clear();
        if (selectedAssetType == vke_common::ASSET_TEXTURE)
        {
            selectedAssetType = vke_common::ASSET_CNT_FLAG;
            selectedAsset = 0;
        }
    }

    void Editor::showSelectedTextureInspector()
    {
        vke_common::TextureAsset *asset = ShowAssetReadResult(vke_common::AssetManager::GetTextureAsset(selectedAsset));
        if (asset == nullptr)
        {
            clearSelectedAsset();
            ImGui::TextUnformatted("No object selected");
            return;
        }

        auto texture = loadTexturePreview(selectedAsset);
        if (!texture) return;
        VkDescriptorSet descriptorSet = getTexturePreviewDescriptorSet(*texture);
        if (descriptorSet == VK_NULL_HANDLE)
        {
            ImGui::TextUnformatted("Texture preview unavailable");
            return;
        }

        ImGui::Text("Texture: %s", asset->name.c_str());
        ImGui::Text("ID: %llu", static_cast<unsigned long long>(asset->id));
        ImGui::Text("Path: %s", vke_common::PathToUTF8(asset->path).c_str());
        ImGui::Text("Size: %u x %u", texture->width, texture->height);
        ImGui::Separator();

        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float imageWidth = std::min(availableWidth, 512.0f);
        const float aspect = texture->width == 0 ? 1.0f : static_cast<float>(texture->height) / static_cast<float>(texture->width);
        ImGui::Image(
            ImTextureRef((ImTextureID)descriptorSet),
            ImVec2(imageWidth, imageWidth * aspect));
    }

    void Editor::showSelectedMaterialInspector()
    {
        vke_common::MaterialAsset *asset = ShowAssetReadResult(vke_common::AssetManager::GetMaterialAsset(selectedAsset));
        if (asset == nullptr)
        {
            clearSelectedAsset();
            ImGui::TextUnformatted("No object selected");
            return;
        }

        ImGui::Text("Material: %s", asset->name.c_str());
        ImGui::Text("ID: %llu", static_cast<unsigned long long>(asset->id));
        ImGui::Text("Path: %s", vke_common::PathToUTF8(asset->path).c_str());
        ImGui::Text("Shader: %llu", static_cast<unsigned long long>(asset->shader));
        ImGui::Text("Render Mode: %s", RenderModeName(asset->renderMode));
        ImGui::Text("Blend Mode: %s", BlendModeName(asset->blendMode));

        if (ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (asset->textures.empty())
                ImGui::TextDisabled("No textures");
            for (size_t i = 0; i < asset->textures.size(); ++i)
            {
                const vke_common::AssetHandle textureHandle = asset->textures[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("%zu: %s", i, TextureDisplayName(textureHandle).c_str());
                drawTexturePreview(textureHandle, 128.0f);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Texture Bindings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (asset->textureBindingInfos == nullptr || asset->textureBindingInfos->empty())
                ImGui::TextDisabled("No texture bindings");
            else
            {
                for (size_t i = 0; i < asset->textureBindingInfos->size(); ++i)
                {
                    const vke_render::TextureBindingInfo &info = (*asset->textureBindingInfos)[i];
                    ImGui::Text(
                        "%zu: binding=%u offset=%u count=%u",
                        i,
                        info.binding,
                        info.offset,
                        info.cnt);
                }
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Push Constants", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (asset->pushConstantInfos == nullptr || asset->pushConstantInfos->empty())
                ImGui::TextDisabled("No push constants");
            else
            {
                for (size_t i = 0; i < asset->pushConstantInfos->size(); ++i)
                {
                    const vke_render::PushConstantInfo &info = (*asset->pushConstantInfos)[i];
                    ImGui::PushID(static_cast<int>(i));
                    const std::string label = "Constant " + std::to_string(i);
                    if (ImGui::TreeNode(label.c_str()))
                    {
                        ImGui::Text("Offset: %u", info.offset);
                        ImGui::Text("Size: %u bytes", info.size);
                        ImGui::Text("Type: %s", info.isFloat ? "float" : "int");

                        const uint32_t valueCount = info.size / sizeof(uint32_t);
                        if (info.pValues == nullptr)
                        {
                            ImGui::TextDisabled("No values");
                            ImGui::TreePop();
                            ImGui::PopID();
                            continue;
                        }

                        for (uint32_t valueIndex = 0; valueIndex < valueCount; ++valueIndex)
                        {
                            const std::string valueLabel = "[" + std::to_string(valueIndex) + "]";
                            uint32_t *rawValue = static_cast<uint32_t *>(const_cast<void *>(info.pValues)) + valueIndex;
                            if (info.isFloat)
                            {
                                float value;
                                std::memcpy(&value, rawValue, sizeof(value));
                                if (ImGui::InputFloat(valueLabel.c_str(), &value, 0.0f, 0.0f, "%.6f"))
                                    std::memcpy(rawValue, &value, sizeof(value));
                            }
                            else
                            {
                                int32_t value;
                                std::memcpy(&value, rawValue, sizeof(value));
                                if (ImGui::InputInt(valueLabel.c_str(), &value))
                                    std::memcpy(rawValue, &value, sizeof(value));
                            }
                        }

                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::TreePop();
        }
    }

    void Editor::showSelectedAssetInspector()
    {
        AssetTreeEntry entry;
        if (!GetAssetInfo(selectedAssetType, selectedAsset, entry))
        {
            clearSelectedAsset();
            ImGui::TextUnformatted("No object selected");
            return;
        }

        ImGui::Text("Asset: %s", entry.name.c_str());
        ImGui::Text("Type: %s", AssetTypeName(entry.type));
        ImGui::Text("ID: %llu", static_cast<unsigned long long>(entry.id));
        ImGui::Text("Path: %s", entry.path.c_str());

        switch (entry.type)
        {
        case vke_common::ASSET_VF_SHADER:
            if (auto *asset = ShowAssetReadResult(vke_common::AssetManager::GetVFShaderAsset(entry.id)))
                ImGui::Text("Fragment Path: %s", vke_common::PathToUTF8(asset->fragPath).c_str());
            break;
        case vke_common::ASSET_FONT:
            if (auto *asset = ShowAssetReadResult(vke_common::AssetManager::GetFontAsset(entry.id)))
            {
                ImGui::Text("Pixel Size: %u", asset->pixelSize);
                ImGui::Text("First Codepoint: %u", asset->firstCodepoint);
                ImGui::Text("Character Count: %u", asset->characterCount);
            }
            break;
        default:
            break;
        }
    }
}
