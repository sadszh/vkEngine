#include <editor/editor.hpp>
#include <reflect/value_editor.hpp>
#include <algorithm>
#include <functional>

namespace vke_editor
{
    namespace
    {
        using namespace vke_common;
        // Commit after drawing so a resized buffer cannot invalidate views that
        // are still being traversed by this frame's widgets.
        using PendingEdit = std::function<ValueViewResult<void>()>;

        template <typename T>
        void DrawScalar(const ValueView &value, T scalar, ImGuiDataType dataType,
                        ValueEditor &editor, PendingEdit &edit)
        {
            if (!ImGui::InputScalar("##value", dataType, &scalar)) return;
            edit = [&editor, value, scalar] { return editor.SetScalar(value, scalar); };
        }

        int ResizeString(ImGuiInputTextCallbackData *data)
        {
            auto &text = *static_cast<std::string *>(data->UserData);
            text.resize(data->BufTextLen);
            data->Buf = text.data();
            return 0;
        }

        void DrawValue(const char *label, const ValueView &value, ValueEditor &editor,
                       PendingEdit &edit, bool canEdit)
        {
            const auto &type = value.Type();
            ImGui::PushID(label);
            const bool compound = type->Kind() == TypeKind::Struct || type->Kind() == TypeKind::Array ||
                                  type->Kind() == TypeKind::Vector;
            if (compound)
            {
                if (ImGui::TreeNodeEx("value", ImGuiTreeNodeFlags_DefaultOpen, "%s", label))
                {
                    if (const auto *structure = type->GetIf<StructTypeInfo>())
                    {
                        for (std::size_t i = 0; i < structure->fields.size(); ++i)
                            DrawValue(structure->fields[i].name.c_str(), *value.Field(i), editor, edit, canEdit);
                    }
                    else
                    {
                        const auto *array = type->GetIf<ArrayTypeInfo>();
                        const uint32_t count = array ? *value.Count() : type->GetIf<VectorTypeInfo>()->componentCount;
                        if (array)
                        {
                            ImGui::Text("%u elements", count);
                            const auto maximum = array->elementType->Kind() == TypeKind::Byte
                                ? ValueView::MaxByteArrayElementCount : ValueView::MaxArrayElementCount;
                            ImGui::BeginDisabled(!canEdit || count >= maximum);
                            if (ImGui::SmallButton("Add element"))
                            {
                                edit = [&editor, value] { return editor.AppendElement(value); };
                            }
                            ImGui::EndDisabled();
                        }
                        // Page large arrays, including nested arrays with variable row heights.
                        constexpr uint32_t pageSize = 64;
                        int page = ImGui::GetStateStorage()->GetInt(ImGui::GetID("page"), 0);
                        const int lastPage = count == 0 ? 0 : static_cast<int>((count - 1) / pageSize);
                        page = std::clamp(page, 0, lastPage);
                        if (lastPage > 0)
                            ImGui::SliderInt("Page", &page, 0, lastPage);
                        ImGui::GetStateStorage()->SetInt(ImGui::GetID("page"), page);
                        const uint32_t end = std::min(count, (static_cast<uint32_t>(page) + 1) * pageSize);
                        for (uint32_t i = static_cast<uint32_t>(page) * pageSize; i < end; ++i)
                        {
                            ImGui::PushID(static_cast<int>(i));
                            bool remove = false;
                            if (array)
                            {
                                ImGui::BeginDisabled(!canEdit);
                                remove = ImGui::SmallButton("Remove");
                                ImGui::EndDisabled();
                                ImGui::SameLine();
                            }
                            DrawValue(std::to_string(i).c_str(), *(array ? value.Element(i) : value.Component(i)),
                                      editor, edit, canEdit);
                            ImGui::PopID();
                            if (remove)
                            {
                                edit = [&editor, value, i] { return editor.RemoveElement(value, i); };
                            }
                        }
                    }
                    ImGui::TreePop();
                }
            }
            else
            {
                ImGui::BeginDisabled(!canEdit);
                // A fixed widget ID keeps field names containing ImGui's ## syntax harmless.
                ImGui::TextUnformatted(label);
                switch (type->Kind())
                {
                case TypeKind::Byte: DrawScalar(value, *value.AsByte(), ImGuiDataType_U8, editor, edit); break;
                case TypeKind::Int32: DrawScalar(value, *value.AsInt32(), ImGuiDataType_S32, editor, edit); break;
                case TypeKind::Int64: DrawScalar(value, *value.AsInt64(), ImGuiDataType_S64, editor, edit); break;
                case TypeKind::Float32: DrawScalar(value, *value.AsFloat32(), ImGuiDataType_Float, editor, edit); break;
                case TypeKind::Float64: DrawScalar(value, *value.AsFloat64(), ImGuiDataType_Double, editor, edit); break;
                case TypeKind::String:
                {
                    auto text = std::string(*value.AsString());
                    // InputText cannot represent embedded NULs; don't silently truncate them.
                    if (text.find('\0') != std::string::npos)
                        ImGui::TextDisabled("String contains NUL bytes (read only)");
                    else if (ImGui::InputText("##value", text.data(), text.capacity() + 1,
                                             ImGuiInputTextFlags_CallbackResize, ResizeString, &text))
                    {
                        edit = [&editor, value, text = std::string(text.c_str())] { return editor.SetString(value, text); };
                    }
                    break;
                }
                default: break;
                }
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
    }

    void Editor::drawScriptComponents()
    {
        auto scripts = vke_common::ScriptManager::GetScriptList(selectedEntity);
        if (!scripts)
        {
            ImGui::TextWrapped("Cannot list scripts: %s", scripts.error().c_str());
            return;
        }
        const bool canEdit = EditorStateManager::GetState() == EditorState::Edit &&
                             !sceneManager->IsPendingDestroy(selectedEntity);
        ImGui::PushID(static_cast<int>(entt::to_integral(selectedEntity)));
        for (const auto &scriptClassName : *scripts)
        {
            const char *className = scriptClassName.c_str();
            ImGui::PushID(className);
            if (!ImGui::TreeNodeEx("value", ImGuiTreeNodeFlags_DefaultOpen, "%s", className))
            {
                ImGui::PopID();
                continue;
            }

            auto data = vke_common::ScriptManager::GetScriptData(selectedEntity, scriptClassName);
            if (!data)
            {
                ImGui::TextWrapped("Cannot export %s: %s", className, data.error().c_str());
            }
            else
            {
                const auto type = vke_common::ScriptManager::GetInstance()->FindTypeInfo(className);
                auto editor = vke_common::ValueEditor::Parse(type, *data);
                if (!editor)
                {
                    ImGui::TextWrapped("Cannot read %s: %s", className,
                                       vke_common::ToString(editor.error().code).data());
                }
                else if (const auto *structure = type->GetIf<vke_common::StructTypeInfo>())
                {
                    // Only expanded scripts have a binary draft; C# owns the state.
                    PendingEdit edit;
                    for (std::size_t i = 0; i < structure->fields.size(); ++i)
                        DrawValue(structure->fields[i].name.c_str(), *editor->Root().Field(i), *editor, edit, canEdit);
                    if (edit && canEdit)
                    {
                        if (auto result = edit(); !result)
                        {
                            VKE_LOG_ERROR("Cannot edit {}: {}", className, vke_common::ToString(result.error().code));
                        }
                        else if (auto applied = vke_common::ScriptManager::SetScriptData(selectedEntity, scriptClassName, *data); !applied)
                        {
                            VKE_LOG_ERROR("Cannot edit {}: {}", className, applied.error());
                        }
                    }
                }
            }
            ImGui::TreePop();
            ImGui::PopID();
        }
        ImGui::PopID();
    }
}
