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
            std::error_code pathError;
            const std::filesystem::path workingDirectory =
                std::filesystem::weakly_canonical(EditorConfig::GetInstance()->workingDirectory, pathError);
            if (pathError)
            {
                error = "Unable to resolve the project working directory.";
                return false;
            }

            std::filesystem::path sourcePath(input);
            if (sourcePath.is_relative())
                sourcePath = workingDirectory / sourcePath;
            sourcePath = std::filesystem::weakly_canonical(sourcePath, pathError);
            if (pathError)
            {
                error = "The selected path could not be resolved.";
                return false;
            }
            if (!std::filesystem::is_regular_file(sourcePath, pathError) || pathError)
            {
                error = "The selected path is not a readable file.";
                return false;
            }

            relativePath = sourcePath.lexically_relative(workingDirectory);
            if (relativePath.empty() || relativePath.is_absolute() ||
                (!relativePath.empty() && *relativePath.begin() == ".."))
            {
                error = "Assets must be located inside the project working directory.";
                return false;
            }
            return true;
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
                    const std::string stem = std::filesystem::path(filePath).stem().string();
                    std::strncpy(importState.name, stem.c_str(), sizeof(importState.name) - 1);
                    importState.name[sizeof(importState.name) - 1] = '\0';
                }
            }
            ImGuiFileDialog::Instance()->Close();
        }
    }

    void Editor::openAssetImport(vke_common::AssetType type)
    {
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
            ImGui::InputText("Name", importState.name, sizeof(importState.name));
            ImGui::InputText("Path", importState.path, sizeof(importState.path));
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
            {
                IGFD::FileDialogConfig dialogConfig;
                dialogConfig.path = EditorConfig::GetInstance()->workingDirectory.string();
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
                    dialogConfig.path = EditorConfig::GetInstance()->workingDirectory.string();
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

            if (!importState.error.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", importState.error.c_str());

            ImGui::Separator();
            const bool canImport = CanImportAsset();
            if (!canImport)
                ImGui::BeginDisabled();
            if (ImGui::Button("Import", ImVec2(120.0f, 0.0f)))
            {
                std::filesystem::path relativePath;
                std::filesystem::path relativeFragmentPath;
                bool pathsValid = GetRelativeAssetPath(importState.path, relativePath, importState.error);
                if (pathsValid && importState.type == vke_common::ASSET_VF_SHADER)
                    pathsValid = GetRelativeAssetPath(
                        importState.fragmentPath, relativeFragmentPath, importState.error);

                if (pathsValid)
                {
                    vke_common::AssetDBBase *assetDB =
                        vke_common::AssetManager::GetInstance()->GetAssetDB();
                    vke_common::AssetHandle newAsset = 0;

                    switch (importState.type)
                    {
                    case vke_common::ASSET_MESH:
                    {
                        vke_common::MeshAsset asset;
                        asset.name = importState.name;
                        asset.path = relativePath;
                        newAsset = assetDB->CreateMeshAsset(asset);
                        break;
                    }
                    case vke_common::ASSET_VF_SHADER:
                    {
                        vke_common::VFShaderAsset asset;
                        asset.name = importState.name;
                        asset.path = relativePath;
                        asset.fragPath = relativeFragmentPath;
                        newAsset = assetDB->CreateVFShaderAsset(asset);
                        break;
                    }
                    case vke_common::ASSET_COMPUTE_SHADER:
                    {
                        vke_common::ComputeShaderAsset asset;
                        asset.name = importState.name;
                        asset.path = relativePath;
                        newAsset = assetDB->CreateComputeShaderAsset(asset);
                        break;
                    }
                    case vke_common::ASSET_SKELETON:
                    {
                        vke_common::SkeletonAsset asset;
                        asset.name = importState.name;
                        asset.path = relativePath;
                        newAsset = assetDB->CreateSkeletonAsset(asset);
                        break;
                    }
                    case vke_common::ASSET_AUDIO_CLIP:
                    {
                        vke_common::AudioClipAsset asset;
                        asset.name = importState.name;
                        asset.path = relativePath;
                        newAsset = assetDB->CreateAudioClipAsset(asset);
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
                            auto image = vke_common::LoadSVG(EditorConfig::GetFullPath(asset.path), asset.rasterWidth, asset.rasterHeight);
                            if (!image)
                            {
                                importState.error = image.error();
                                break;
                            }
                        }
                        newAsset = assetDB->CreateTextureAsset(asset);
                        break;
                    }
                    case vke_common::ASSET_ANIMATION:
                    {
                        vke_common::AnimationAsset asset;
                        asset.name = importState.name;
                        asset.path = relativePath;
                        asset.hasRootMotion = importState.animationHasRootMotion;
                        newAsset = assetDB->CreateAnimationAsset(asset);
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
                        newAsset = assetDB->CreateFontAsset(asset);
                        break;
                    }
                    default:
                        break;
                    }

                    if (newAsset != 0)
                    {
                        if (!assetDB->SyncAll())
                        {
                            importState.error = "The asset database could not persist this asset.";
                        }
                        else
                        {
                            assetDirectoryTree = {};
                            importState.dialogActive = false;
                        }
                    }
                    else if (importState.error.empty())
                    {
                        importState.error = "The asset database could not create this asset.";
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

        HandleBrowseResult("ImportBrowse", importState.path, sizeof(importState.path));
        if (importState.type == vke_common::ASSET_VF_SHADER)
            HandleBrowseResult(
                "ImportBrowseFrag", importState.fragmentPath, sizeof(importState.fragmentPath));
    }
}
