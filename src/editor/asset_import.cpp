#include <editor/editor.hpp>
#include <editor/editor_config.hpp>
#include <asset/svg.hpp>
#include <imgui/dialog/ImGuiFileDialog.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace vke_editor
{
    namespace
    {
        constexpr size_t assetNameBufferSize = 256;
        constexpr size_t assetPathBufferSize = 4096;
        constexpr size_t fontCharactersBufferSize = 256;
        constexpr uint32_t maximumUnicodeCodepoint = 0x10FFFF;

        struct AssetImportState
        {
            bool dialogActive = false;
            vke_common::AssetHandle pendingAsset = 0;
            vke_common::AssetType type = vke_common::ASSET_CNT_FLAG;
            char name[assetNameBufferSize]{};
            char path[assetPathBufferSize]{};
            char fragmentPath[assetPathBufferSize]{};

            int textureFormat = 0;
            int textureMinFilter = 0;
            int textureMagFilter = 0;
            int textureAddressMode = 0;
            bool textureAnisotropy = true;
            bool textureGenerateMipMap = true;
            int textureRasterWidth = 0;
            int textureRasterHeight = 0;

            bool animationHasRootMotion = false;

            int fontPixelSize = 48;
            int fontCharacterCount = 128;
            int fontFirstCodepoint = 32;
            char fontCharacters[fontCharactersBufferSize]{};

            std::string error;
        };

        AssetImportState importState;

        void DiscardPendingImport()
        {
            if (!importState.pendingAsset) return;
            auto *db = vke_common::AssetManager::GetAssetDB();
            switch (importState.type)
            {
            case vke_common::ASSET_MESH: db->RemoveMeshAsset(importState.pendingAsset); break;
            case vke_common::ASSET_VF_SHADER: db->RemoveVFShaderAsset(importState.pendingAsset); break;
            case vke_common::ASSET_COMPUTE_SHADER: db->RemoveComputeShaderAsset(importState.pendingAsset); break;
            case vke_common::ASSET_SKELETON: db->RemoveSkeletonAsset(importState.pendingAsset); break;
            case vke_common::ASSET_AUDIO_CLIP: db->RemoveAudioClipAsset(importState.pendingAsset); break;
            case vke_common::ASSET_TEXTURE: db->RemoveTextureAsset(importState.pendingAsset); break;
            case vke_common::ASSET_ANIMATION: db->RemoveAnimationAsset(importState.pendingAsset); break;
            case vke_common::ASSET_FONT: db->RemoveFontAsset(importState.pendingAsset); break;
            default: break;
            }
            importState.pendingAsset = 0;
        }

        const char *textureFormatNames[] = {
            "R8G8B8A8_SRGB", "R8G8B8A8_UNORM", "R8G8B8A8_SNORM",
            "B8G8R8A8_SRGB", "B8G8R8A8_UNORM", "R8_UNORM"};
        const VkFormat textureFormatValues[] = {
            VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SNORM,
            VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8_UNORM};
        constexpr int textureFormatCount = static_cast<int>(sizeof(textureFormatNames) / sizeof(textureFormatNames[0]));

        const char *textureFilterNames[] = {"Linear", "Nearest"};
        const VkFilter textureFilterValues[] = {VK_FILTER_LINEAR, VK_FILTER_NEAREST};
        constexpr int textureFilterCount = static_cast<int>(sizeof(textureFilterNames) / sizeof(textureFilterNames[0]));

        const char *textureAddressModeNames[] = {"Repeat", "Clamp To Edge", "Clamp To Border", "Mirrored Repeat"};
        const VkSamplerAddressMode textureAddressModeValues[] = {
            VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT};
        constexpr int textureAddressModeCount = static_cast<int>(sizeof(textureAddressModeNames) / sizeof(textureAddressModeNames[0]));

        const char *GetImportFilter(vke_common::AssetType type)
        {
            switch (type)
            {
            case vke_common::ASSET_MESH:
                return ".mesh";
            case vke_common::ASSET_VF_SHADER:
            case vke_common::ASSET_COMPUTE_SHADER:
                return ".spv";
            case vke_common::ASSET_SKELETON:
            case vke_common::ASSET_ANIMATION:
                return ".ozz";
            case vke_common::ASSET_AUDIO_CLIP:
                return ".wav,.mp3,.ogg,.flac";
            case vke_common::ASSET_TEXTURE:
                return ".png,.jpg,.jpeg,.bmp,.tga,.hdr,.ktx,.svg,.SVG";
            case vke_common::ASSET_FONT:
                return ".ttf,.otf";
            default:
                return "";
            }
        }

        const char *GetAssetTypeLabel(vke_common::AssetType type)
        {
            switch (type)
            {
            case vke_common::ASSET_MESH:
                return "Mesh";
            case vke_common::ASSET_VF_SHADER:
                return "VF Shader";
            case vke_common::ASSET_COMPUTE_SHADER:
                return "Compute Shader";
            case vke_common::ASSET_SKELETON:
                return "Skeleton";
            case vke_common::ASSET_AUDIO_CLIP:
                return "Audio Clip";
            case vke_common::ASSET_TEXTURE:
                return "Texture";
            case vke_common::ASSET_ANIMATION:
                return "Animation";
            case vke_common::ASSET_FONT:
                return "Font";
            default:
                return "Asset";
            }
        }

        bool IsFontConfigurationValid()
        {
            if (importState.fontPixelSize <= 0 || importState.fontCharacterCount <= 0 ||
                importState.fontFirstCodepoint < 0 ||
                importState.fontFirstCodepoint > static_cast<int>(maximumUnicodeCodepoint))
                return false;

            if (importState.fontCharacters[0] != '\0')
                return true;

            const uint64_t lastCodepoint = static_cast<uint64_t>(importState.fontFirstCodepoint) +
                                           static_cast<uint64_t>(importState.fontCharacterCount) - 1;
            return lastCodepoint <= maximumUnicodeCodepoint;
        }

        bool CanImportAsset()
        {
            if (importState.name[0] == '\0' || importState.path[0] == '\0')
                return false;
            if (importState.type == vke_common::ASSET_VF_SHADER && importState.fragmentPath[0] == '\0')
                return false;
            if (importState.type == vke_common::ASSET_TEXTURE && vke_common::IsSVGPath(importState.path))
                return importState.textureRasterWidth >= 0 && importState.textureRasterHeight >= 0 &&
                       importState.textureRasterWidth <= vke_common::MaxSVGDimension &&
                       importState.textureRasterHeight <= vke_common::MaxSVGDimension;
            return importState.type != vke_common::ASSET_FONT || IsFontConfigurationValid();
        }

        bool GetRelativeAssetPath(const char *input, std::filesystem::path &relativePath, std::string &error)
        {
            try
            {
                auto sourcePath = vke_common::PathFromUTF8(input);
                if (sourcePath.is_relative())
                    sourcePath = vke_common::Paths::Project(sourcePath);
                if (!std::filesystem::is_regular_file(sourcePath))
                {
                    error = "The selected path is not a regular file.";
                    return false;
                }
                relativePath = vke_common::Paths::ProjectRelative(sourcePath);
                return true;
            }
            catch (const std::exception &exception)
            {
                error = exception.what();
                return false;
            }
        }

        void HandleBrowseResult(const char *key, char *buffer, size_t bufferSize)
        {
            if (!ImGuiFileDialog::Instance()->Display(key))
                return;

            if (ImGuiFileDialog::Instance()->IsOk())
            {
                const std::string filePath = ImGuiFileDialog::Instance()->GetFilePathName();
                std::strncpy(buffer, filePath.c_str(), bufferSize - 1);
                buffer[bufferSize - 1] = '\0';
                importState.error.clear();

                if (importState.name[0] == '\0')
                {
                    const std::string stem = vke_common::PathToUTF8(vke_common::PathFromUTF8(filePath).stem());
                    std::strncpy(importState.name, stem.c_str(), sizeof(importState.name) - 1);
                    importState.name[sizeof(importState.name) - 1] = '\0';
                }
            }
            ImGuiFileDialog::Instance()->Close();
        }
    }

    void Editor::openAssetImport(vke_common::AssetType type)
    {
        DiscardPendingImport();
        assetDirectoryTree = {};
        importState = {};
        importState.dialogActive = true;
        importState.type = type;
    }

    void Editor::showAssetImportDialog()
    {
        if (!importState.dialogActive)
            return;

        const std::string title = std::string("Import ") + GetAssetTypeLabel(importState.type);
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        const bool windowVisible = ImGui::Begin(
            title.c_str(), &importState.dialogActive,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        if (windowVisible)
        {
            ImGui::BeginDisabled(importState.pendingAsset != 0);
            ImGui::InputText("Name", importState.name, sizeof(importState.name));
            ImGui::InputText("Path", importState.path, sizeof(importState.path));
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
            {
                IGFD::FileDialogConfig dialogConfig;
                dialogConfig.path = vke_common::PathToUTF8(vke_common::Paths::ProjectRoot());
                ImGuiFileDialog::Instance()->OpenDialog(
                    "ImportBrowse", "Select File", GetImportFilter(importState.type), dialogConfig);
            }

            if (importState.type == vke_common::ASSET_VF_SHADER)
            {
                ImGui::InputText("Fragment Path", importState.fragmentPath, sizeof(importState.fragmentPath));
                ImGui::SameLine();
                if (ImGui::Button("Browse Frag..."))
                {
                    IGFD::FileDialogConfig dialogConfig;
                    dialogConfig.path = vke_common::PathToUTF8(vke_common::Paths::ProjectRoot());
                    ImGuiFileDialog::Instance()->OpenDialog(
                        "ImportBrowseFrag", "Select Fragment Shader", ".spv", dialogConfig);
                }
            }

            if (importState.type == vke_common::ASSET_TEXTURE)
            {
                ImGui::SeparatorText("Texture Settings");
                const bool svg = vke_common::IsSVGPath(importState.path);
                if (svg)
                {
                    if (importState.textureFormat > 1)
                        importState.textureFormat = 0;
                    ImGui::InputInt("Raster Width", &importState.textureRasterWidth);
                    ImGui::InputInt("Raster Height", &importState.textureRasterHeight);
                    ImGui::TextDisabled("0 = automatic; maximum 4096 pixels per dimension.");
                }
                ImGui::Combo("Format", &importState.textureFormat, textureFormatNames, svg ? 2 : textureFormatCount);
                ImGui::Combo("Min Filter", &importState.textureMinFilter, textureFilterNames, textureFilterCount);
                ImGui::Combo("Mag Filter", &importState.textureMagFilter, textureFilterNames, textureFilterCount);
                ImGui::Combo("Address Mode", &importState.textureAddressMode,
                             textureAddressModeNames, textureAddressModeCount);
                ImGui::Checkbox("Anisotropy", &importState.textureAnisotropy);
                ImGui::SameLine();
                ImGui::Checkbox("Generate MipMap", &importState.textureGenerateMipMap);
            }

            if (importState.type == vke_common::ASSET_ANIMATION)
            {
                ImGui::SeparatorText("Animation Settings");
                ImGui::Checkbox("Has Root Motion", &importState.animationHasRootMotion);
            }

            if (importState.type == vke_common::ASSET_FONT)
            {
                ImGui::SeparatorText("Font Settings");
                ImGui::InputInt("Pixel Size", &importState.fontPixelSize);
                ImGui::InputInt("Character Count", &importState.fontCharacterCount);
                ImGui::InputInt("First Codepoint", &importState.fontFirstCodepoint);
                ImGui::InputText("Characters", importState.fontCharacters, sizeof(importState.fontCharacters));
                if (!IsFontConfigurationValid())
                    ImGui::TextDisabled("Font values must describe a non-empty valid Unicode range.");
            }

            ImGui::EndDisabled();
            if (importState.pendingAsset)
                ImGui::TextWrapped("Saving failed. Retry saving this asset, or cancel to discard the import.");
            if (!importState.error.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", importState.error.c_str());

            ImGui::Separator();
            const bool canImport = importState.pendingAsset != 0 || CanImportAsset();
            if (!canImport)
                ImGui::BeginDisabled();
            if (ImGui::Button(importState.pendingAsset ? "Retry Save" : "Import", ImVec2(120.0f, 0.0f)))
            {
                auto *assetDB = vke_common::AssetManager::GetAssetDB();
                importState.error.clear();
                if (!importState.pendingAsset)
                {
                    std::filesystem::path relativePath;
                    std::filesystem::path relativeFragmentPath;
                    bool pathsValid = GetRelativeAssetPath(importState.path, relativePath, importState.error);
                    if (pathsValid && importState.type == vke_common::ASSET_VF_SHADER)
                        pathsValid = GetRelativeAssetPath(importState.fragmentPath, relativeFragmentPath, importState.error);
                    if (pathsValid)
                    {
                        switch (importState.type)
                        {
                        case vke_common::ASSET_MESH:
                        {
                            vke_common::MeshAsset asset;
                            asset.name = importState.name;
                            asset.path = relativePath;
                            importState.pendingAsset = assetDB->CreateMeshAsset(asset);
                            break;
                        }
                        case vke_common::ASSET_VF_SHADER:
                        {
                            vke_common::VFShaderAsset asset;
                            asset.name = importState.name;
                            asset.path = relativePath;
                            asset.fragPath = relativeFragmentPath;
                            importState.pendingAsset = assetDB->CreateVFShaderAsset(asset);
                            break;
                        }
                        case vke_common::ASSET_COMPUTE_SHADER:
                        {
                            vke_common::ComputeShaderAsset asset;
                            asset.name = importState.name;
                            asset.path = relativePath;
                            importState.pendingAsset = assetDB->CreateComputeShaderAsset(asset);
                            break;
                        }
                        case vke_common::ASSET_SKELETON:
                        {
                            vke_common::SkeletonAsset asset;
                            asset.name = importState.name;
                            asset.path = relativePath;
                            importState.pendingAsset = assetDB->CreateSkeletonAsset(asset);
                            break;
                        }
                        case vke_common::ASSET_AUDIO_CLIP:
                        {
                            vke_common::AudioClipAsset asset;
                            asset.name = importState.name;
                            asset.path = relativePath;
                            importState.pendingAsset = assetDB->CreateAudioClipAsset(asset);
                            break;
                        }
                        case vke_common::ASSET_TEXTURE:
                        {
                            vke_common::TextureAsset asset;
                            asset.name = importState.name;
                            asset.path = relativePath;
                            asset.format = textureFormatValues[importState.textureFormat];
                            asset.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
                            asset.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                            asset.minFilter = textureFilterValues[importState.textureMinFilter];
                            asset.magFilter = textureFilterValues[importState.textureMagFilter];
                            asset.addressMode = textureAddressModeValues[importState.textureAddressMode];
                            asset.anisotropyEnable = importState.textureAnisotropy;
                            asset.generateMipMap = importState.textureGenerateMipMap;
                            asset.rasterWidth = importState.textureRasterWidth;
                            asset.rasterHeight = importState.textureRasterHeight;
                            if (vke_common::IsSVGPath(asset.path))
                            {
                                auto image = vke_common::LoadSVG(vke_common::Paths::Project(asset.path), asset.rasterWidth, asset.rasterHeight);
                                if (!image)
                                {
                                    importState.error = image.error();
                                    break;
                                }
                            }
                            importState.pendingAsset = assetDB->CreateTextureAsset(asset);
                            break;
                        }
                        case vke_common::ASSET_ANIMATION:
                        {
                            vke_common::AnimationAsset asset;
                            asset.name = importState.name;
                            asset.path = relativePath;
                            asset.hasRootMotion = importState.animationHasRootMotion;
                            importState.pendingAsset = assetDB->CreateAnimationAsset(asset);
                            break;
                        }
                        case vke_common::ASSET_FONT:
                        {
                            vke_common::FontAsset asset;
                            asset.name = importState.name;
                            asset.path = relativePath;
                            asset.pixelSize = static_cast<uint32_t>(importState.fontPixelSize);
                            asset.characterCount = static_cast<uint32_t>(importState.fontCharacterCount);
                            asset.firstCodepoint = static_cast<uint32_t>(importState.fontFirstCodepoint);
                            asset.characters = importState.fontCharacters;
                            importState.pendingAsset = assetDB->CreateFontAsset(asset);
                            break;
                        }
                        default:
                            break;
                        }
                    }
                }
                if (importState.pendingAsset)
                {
                    assetDirectoryTree = {};
                    if (auto result = assetDB->SyncAll(); !result)
                        importState.error = result.error();
                    else
                    {
                        importState.pendingAsset = 0;
                        importState.dialogActive = false;
                    }
                }
            }
            if (!canImport)
                ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
                importState.dialogActive = false;
        }
        ImGui::End();
        if (!importState.dialogActive && importState.pendingAsset)
        {
            DiscardPendingImport();
            assetDirectoryTree = {};
        }

        HandleBrowseResult("ImportBrowse", importState.path, sizeof(importState.path));
        if (importState.type == vke_common::ASSET_VF_SHADER)
            HandleBrowseResult(
                "ImportBrowseFrag", importState.fragmentPath, sizeof(importState.fragmentPath));
    }
}
