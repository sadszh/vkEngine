#include <editor/editor.hpp>
#include <component/canvas.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cfloat>

namespace vke_editor
{
    static int textResizeCallback(ImGuiInputTextCallbackData *data)
    {
        auto &text = *static_cast<std::string *>(data->UserData);
        text.resize(data->BufTextLen);
        data->Buf = text.data();
        return 0;
    }

    static bool inputString(const char *label, std::string &text, bool multiline = false)
    {
        text.reserve(text.size() + 1);
        if (multiline)
            return ImGui::InputTextMultiline(label, text.data(), text.capacity() + 1,
                                             ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4), ImGuiInputTextFlags_CallbackResize, textResizeCallback, &text);
        return ImGui::InputText(label, text.data(), text.capacity() + 1,
                                ImGuiInputTextFlags_CallbackResize, textResizeCallback, &text);
    }

    void Editor::drawCanvasComponent()
    {
        using namespace vke_component;
        using namespace vke_ui;
        auto *canvas = sceneManager->registry.try_get<Canvas>(selectedEntity);
        if (!canvas || !ImGui::TreeNodeEx("Canvas", ImGuiTreeNodeFlags_DefaultOpen))
            return;
        ImGui::TextUnformatted("Screen Space");
        ImGui::TextDisabled("UI coordinates are relative to the viewport.");
        auto resolution = canvas->GetReferenceResolution();
        if (ImGui::InputFloat2("Reference resolution", glm::value_ptr(resolution)))
            if (auto result = canvas->SetReferenceResolution(resolution); !result)
                VKE_LOG_ERROR("Invalid Canvas resolution: {}", result.error());
        ImGui::InputInt("Sort order", &canvas->sortOrder);

        auto addNode = [&](UIHandle parent, UIType type)
        {
            std::unique_ptr<UINodeData> data;
            switch (type)
            {
            case UIType::Text:
            {
                auto textData = std::make_unique<UITextData>();
                textData->name = "Text";
                textData->text = "Text";
                data = std::move(textData);
                break;
            }
            case UIType::Button:
            {
                auto buttonData = std::make_unique<UIButtonData>();
                buttonData->name = "Button";
                auto label = std::make_unique<UITextData>();
                label->name = "Label";
                label->text = "Button";
                label->color = {0.1f, 0.1f, 0.1f, 1};
                label->rectTransform.anchorMax = {1, 1};
                label->rectTransform.offsetMin = {16, 8};
                label->rectTransform.offsetMax = {-16, -8};
                label->layout = {24, TextHorizontalAlignment::Center, TextVerticalAlignment::Middle, true};
                buttonData->children.push_back(std::move(label));
                data = std::move(buttonData);
                break;
            }
            case UIType::Image:
                data = std::make_unique<UIImageData>();
                data->name = "Image";
                break;
            case UIType::Container:
                data = std::make_unique<UIContainerData>();
                data->name = "Container";
                break;
            }
            data->rectTransform.offsetMax = {400, 80};
            if (auto result = canvas->Add(*data, parent); !result)
                VKE_LOG_ERROR("Failed to add UI node: {}", result.error());
        };
        if (ImGui::Button("Add Container"))
            addNode(0, UIType::Container);
        ImGui::SameLine();
        if (ImGui::Button("Add Text"))
            addNode(0, UIType::Text);
        ImGui::SameLine();
        if (ImGui::Button("Add Image"))
            addNode(0, UIType::Image);
        ImGui::SameLine();
        if (ImGui::Button("Add Button"))
            addNode(0, UIType::Button);

        std::function<void()> pendingEdit;
        std::function<void(UIComponent &, size_t, size_t)> drawNode;
        drawNode = [&](UIComponent &node, size_t index, size_t siblingCount)
        {
            const auto handle = node.handle;
            const auto type = node.GetType();
            const char *typeName = nullptr;
            switch (type)
            {
            case UIType::Container:
                typeName = "Container";
                break;
            case UIType::Text:
                typeName = "Text";
                break;
            case UIType::Image:
                typeName = "Image";
                break;
            case UIType::Button:
                typeName = "Button";
                break;
            }
            ImGui::PushID(&node);
            const bool opened = ImGui::TreeNodeEx("node", ImGuiTreeNodeFlags_DefaultOpen, "%s (%s)",
                                                  node.name.c_str(), typeName);
            if (opened)
            {
                inputString("Name", node.name);
                bool hitTestEnabled = node.GetHitTestEnabled();
                if (ImGui::Checkbox("Hit test enabled", &hitTestEnabled))
                    node.SetHitTestEnabled(hitTestEnabled);
                bool interactable = node.GetInteractable();
                if (ImGui::Checkbox("Interactable", &interactable))
                    node.SetInteractable(interactable);
                auto rect = node.rectTransform;
                bool changed = ImGui::InputFloat2("Anchor min", glm::value_ptr(rect.anchorMin));
                changed |= ImGui::InputFloat2("Anchor max", glm::value_ptr(rect.anchorMax));
                changed |= ImGui::InputFloat2("Offset min", glm::value_ptr(rect.offsetMin));
                changed |= ImGui::InputFloat2("Offset max", glm::value_ptr(rect.offsetMax));
                changed |= ImGui::InputFloat2("Pivot", glm::value_ptr(rect.pivot));
                changed |= ImGui::InputFloat2("Scale", glm::value_ptr(rect.scale));
                float degrees = glm::degrees(rect.rotation);
                if (ImGui::InputFloat("Rotation (degrees)", &degrees))
                {
                    rect.rotation = glm::radians(degrees);
                    changed = true;
                }
                if (changed)
                    if (auto result = node.SetRectTransform(rect); !result)
                        VKE_LOG_ERROR("Invalid UI rectangle: {}", result.error());
                const auto &size = node.resolvedSize;
                ImGui::Text("Resolved size: %.1f x %.1f", size.x, size.y);
                if (ImGui::TreeNode("Self layout"))
                {
                    auto options = node.GetSelfLayout();
                    int width = static_cast<int>(options.width), height = static_cast<int>(options.height);
                    bool layoutChanged = ImGui::Combo("Width", &width, "Rect\0Content\0");
                    layoutChanged |= ImGui::Combo("Height", &height, "Rect\0Content\0");
                    layoutChanged |= ImGui::Checkbox("Ignore parent layout", &options.ignoreLayout);
                    options.width = static_cast<SizeMode>(width);
                    options.height = static_cast<SizeMode>(height);
                    if (layoutChanged)
                        if (auto result = node.SetSelfLayout(options); !result)
                            VKE_LOG_ERROR("Invalid UI self layout: {}", result.error());
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode("Group layout"))
                {
                    auto options = node.GetGroupLayout();
                    int direction = static_cast<int>(options.direction);
                    int main = static_cast<int>(options.mainAlignment), cross = static_cast<int>(options.crossAlignment);
                    bool layoutChanged = ImGui::Combo("Direction", &direction, "None\0Horizontal\0Vertical\0");
                    if (direction != 0)
                    {
                        layoutChanged |= ImGui::InputFloat4("Padding (L/T/R/B)", glm::value_ptr(options.padding));
                        layoutChanged |= ImGui::InputFloat("Spacing", &options.spacing);
                        layoutChanged |= ImGui::Combo("Main alignment", &main, "Start\0Center\0End\0");
                        layoutChanged |= ImGui::Combo("Cross alignment", &cross, "Start\0Center\0End\0Stretch\0");
                    }
                    options.direction = static_cast<LayoutDirection>(direction);
                    options.mainAlignment = static_cast<LayoutAlignment>(main);
                    options.crossAlignment = static_cast<LayoutAlignment>(cross);
                    if (layoutChanged)
                        if (auto result = node.SetGroupLayout(options); !result)
                            VKE_LOG_ERROR("Invalid UI group layout: {}", result.error());
                    ImGui::TextDisabled("Layout groups own child positions. Rect sizes use offset max - min.");
                    ImGui::TreePop();
                }
                switch (type)
                {
                case UIType::Text:
                {
                    auto &text = static_cast<UIText &>(node);
                    auto value = text.GetText();
                    if (inputString("Text", value, true))
                        text.SetText(value);
                    auto color = text.GetColor();
                    if (ImGui::ColorEdit4("Color", glm::value_ptr(color)))
                        text.SetColor(color);
                    const auto font = text.GetFont();
                    const auto *fontAsset = ShowAssetReadResult(vke_common::AssetManager::GetFontAsset(font));
                    const std::string fontLabel = std::to_string(font) + "  " + (fontAsset ? fontAsset->name : "<missing>");
                    if (ImGui::BeginCombo("Font", fontLabel.c_str()))
                    {
                        ShowAssetIterationResult(vke_common::AssetManager::IterateFontAsset([&](const vke_common::FontAsset &entry)
                        {
                            const auto choice = std::to_string(entry.id) + "  " + entry.name;
                            if (ImGui::Selectable(choice.c_str(), entry.id == font))
                                if (auto result = text.SetFont(entry.id); !result)
                                    VKE_LOG_ERROR("Cannot set text font: {}", result.error());
                            if (entry.id == font) ImGui::SetItemDefaultFocus();
                        }));
                        ImGui::EndCombo();
                    }
                    auto options = text.GetLayoutOptions();
                    bool layoutChanged = ImGui::InputFloat("Font size", &options.fontSize);
                    int horizontal = static_cast<int>(options.horizontalAlignment);
                    int vertical = static_cast<int>(options.verticalAlignment);
                    layoutChanged |= ImGui::Combo("Horizontal alignment", &horizontal, "Left\0Center\0Right\0");
                    layoutChanged |= ImGui::Combo("Vertical alignment", &vertical, "Top\0Middle\0Bottom\0");
                    layoutChanged |= ImGui::Checkbox("Word wrap", &options.wordWrap);
                    options.horizontalAlignment = static_cast<TextHorizontalAlignment>(horizontal);
                    options.verticalAlignment = static_cast<TextVerticalAlignment>(vertical);
                    if (layoutChanged)
                        if (auto result = text.SetLayoutOptions(options); !result)
                            VKE_LOG_ERROR("Invalid text layout: {}", result.error());
                    const auto naturalSize = text.Measure();
                    const auto textSize = text.GetTextSize();
                    ImGui::Text("Natural size: %.1f x %.1f", naturalSize.x, naturalSize.y);
                    ImGui::Text("Text size: %.1f x %.1f", textSize.x, textSize.y);
                    break;
                }
                case UIType::Image:
                case UIType::Button:
                {
                    auto &image = static_cast<UIImage &>(node);
                    const auto texture = image.GetTexture();
                    const auto *asset = ShowAssetReadResult(vke_common::AssetManager::GetTextureAsset(texture));
                    const std::string label = texture == 0 ? "0  <white>" : std::to_string(texture) + "  " + (asset ? asset->name : "<missing>");
                    if (ImGui::BeginCombo("Texture", label.c_str()))
                    {
                        auto selectTexture = [&](vke_common::AssetHandle handle, const char *name)
                        {
                            if (ImGui::Selectable(name, handle == texture))
                                if (auto result = image.SetTexture(handle); !result)
                                    VKE_LOG_ERROR("Cannot set image texture: {}", result.error());
                            if (handle == texture)
                                ImGui::SetItemDefaultFocus();
                        };
                        selectTexture(0, "0  <white>");
                        ShowAssetIterationResult(vke_common::AssetManager::IterateTextureAsset([&](const vke_common::TextureAsset &entry)
                                                                                               {
                            const auto choice = std::to_string(entry.id) + "  " + entry.name;
                            selectTexture(entry.id, choice.c_str()); }));
                        ImGui::EndCombo();
                    }
                    auto color = image.GetColor();
                    if (ImGui::ColorEdit4("Color", glm::value_ptr(color)))
                        if (auto result = image.SetColor(color); !result)
                            VKE_LOG_ERROR("Invalid image color: {}", result.error());
                    if (type == UIType::Button)
                    {
                        auto &button = static_cast<UIButton &>(node);
                        const char *labels[] = {"Hover color", "Pressed color", "Disabled color"};
                        for (int i = 1; i <= 3; ++i)
                        {
                            auto stateColor = button.GetStateColor(i);
                            if (ImGui::ColorEdit4(labels[i - 1], glm::value_ptr(stateColor)))
                                if (auto result = button.SetStateColor(i, stateColor); !result)
                                    VKE_LOG_ERROR("Cannot set button color: {}", result.error());
                        }
                    }
                    auto uv = image.GetUVRect();
                    if (ImGui::InputFloat4("UV min/max", glm::value_ptr(uv)))
                        if (auto result = image.SetUVRect(uv); !result)
                            VKE_LOG_ERROR("Invalid image UV rectangle: {}", result.error());
                    auto preserveAspect = image.GetPreserveAspect();
                    if (ImGui::Checkbox("Preserve aspect", &preserveAspect))
                        image.SetPreserveAspect(preserveAspect);
                    break;
                }
                case UIType::Container:
                    break;
                }
                auto *parent = node.parent;
                if (ImGui::BeginCombo("Parent", parent ? parent->name.c_str() : "Canvas"))
                {
                    auto chooseParent = [&](UIHandle target)
                    {
                        pendingEdit = [canvas, handle, target]()
                        {
                            if (auto result = canvas->SetParent(handle, target); !result)
                                VKE_LOG_ERROR("Cannot reparent UI node: {}", result.error());
                        };
                    };
                    if (ImGui::Selectable("Canvas", !parent))
                        chooseParent(0);
                    std::function<void(const UIComponent &)> drawChoice = [&](const UIComponent &candidate)
                    {
                        if (&candidate == &node)
                            return;
                        ImGui::PushID(&candidate);
                        if (ImGui::Selectable(candidate.name.c_str(), parent == &candidate))
                            chooseParent(candidate.handle);
                        ImGui::PopID();
                        for (const auto &child : candidate.children)
                            drawChoice(*child);
                    };
                    for (const auto &root : canvas->GetChildren())
                        drawChoice(*root);
                    ImGui::EndCombo();
                }
                ImGui::BeginDisabled(index == 0);
                if (ImGui::Button("Move up"))
                    pendingEdit = [canvas, handle, index]()
                    { canvas->SetSiblingIndex(handle, index - 1); };
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(index + 1 == siblingCount);
                if (ImGui::Button("Move down"))
                    pendingEdit = [canvas, handle, index]()
                    { canvas->SetSiblingIndex(handle, index + 1); };
                ImGui::EndDisabled();
                if (ImGui::Button("Add Container"))
                    addNode(handle, UIType::Container);
                ImGui::SameLine();
                if (ImGui::Button("Add Text"))
                    addNode(handle, UIType::Text);
                ImGui::SameLine();
                if (ImGui::Button("Add Image"))
                    addNode(handle, UIType::Image);
                ImGui::SameLine();
                if (ImGui::Button("Add Button"))
                    addNode(handle, UIType::Button);
                ImGui::SameLine();
                if (ImGui::Button("Delete subtree"))
                    pendingEdit = [canvas, handle]()
                    { canvas->Remove(handle); };
                const auto &children = node.children;
                for (size_t i = 0; i < children.size(); ++i)
                    drawNode(*children[i], i, children.size());
                ImGui::TreePop();
            }
            ImGui::PopID();
        };
        const auto &roots = canvas->GetChildren();
        for (size_t i = 0; i < roots.size(); ++i)
            drawNode(*roots[i], i, roots.size());
        if (pendingEdit)
            pendingEdit();
        ImGui::TreePop();
    }
}
