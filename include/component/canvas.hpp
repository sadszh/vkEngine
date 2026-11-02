#pragma once

#include <ui/text.hpp>
#include <ui/button.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <climits>
#include <unordered_map>

namespace vke_component
{
    struct CanvasData
    {
        glm::vec2 referenceResolution{1920.0f, 1080.0f};
        int32_t sortOrder = 0;
        std::vector<std::unique_ptr<vke_ui::UINodeData>> children;

        CanvasData() = default;
        CanvasData(const CanvasData &other)
            : referenceResolution(other.referenceResolution), sortOrder(other.sortOrder)
        {
            children.reserve(other.children.size());
            for (const auto &child : other.children) children.push_back(child ? child->Clone() : nullptr);
        }
        CanvasData &operator=(const CanvasData &other)
        {
            if (this != &other)
            {
                CanvasData copy(other);
                *this = std::move(copy);
            }
            return *this;
        }
        CanvasData(CanvasData &&) noexcept = default;
        CanvasData &operator=(CanvasData &&) noexcept = default;

        explicit CanvasData(const nlohmann::json &json)
            : sortOrder(json.value("sortOrder", int32_t{0}))
        {
            if (json.contains("referenceResolution"))
                referenceResolution = {json["referenceResolution"][0].get<float>(), json["referenceResolution"][1].get<float>()};
            if (json.contains("children"))
                for (const auto &child : json["children"]) children.push_back(vke_ui::UINodeData::FromJSON(child));
        }

        static vke_common::SceneResult<void> ValidateResolution(const glm::vec2 &value)
        {
            if (value.x <= 0 || value.y <= 0)
                return std::unexpected("reference resolution must be positive");
            return {};
        }

        static vke_common::SceneResult<void> ValidateJSON(const nlohmann::json &json)
        {
            if (auto valid = vke_common::json_validation::Object(json).Vectors({"referenceResolution"}, 2).Strings({"mode"}).Result(); !valid)
                return valid;
            if (json.contains("mode") && json["mode"] != "screenSpace")
                return std::unexpected("only screenSpace Canvas is supported");
            if (json.contains("sortOrder") && (!json["sortOrder"].is_number_integer() ||
                json["sortOrder"].get<double>() < INT32_MIN || json["sortOrder"].get<double>() > INT32_MAX))
                return std::unexpected("sortOrder must be a 32-bit integer");
            if (json.contains("referenceResolution"))
                if (auto valid = ValidateResolution({json["referenceResolution"][0].get<float>(),
                                                    json["referenceResolution"][1].get<float>()}); !valid) return valid;
            return vke_ui::UINodeData::ValidateChildrenJSON(json);
        }

        vke_common::SceneResult<void> Validate() const
        {
            if (auto valid = ValidateResolution(referenceResolution); !valid) return valid;
            for (const auto &child : children)
            {
                if (!child) return std::unexpected("UI child must not be null");
                if (auto valid = child->Validate(); !valid) return valid;
            }
            return {};
        }

        vke_common::SceneResult<void> ValidateAssets() const
        {
            for (const auto &child : children)
                if (auto valid = child->ValidateAssets(); !valid) return valid;
            return {};
        }

        vke_common::SceneResult<void> LoadAssets()
        {
            for (auto &child : children)
                if (auto loaded = child->LoadAssets(); !loaded) return loaded;
            return {};
        }

        nlohmann::json ToJSON() const
        {
            nlohmann::json json{{"type", "canvas"}, {"mode", "screenSpace"}, {"sortOrder", sortOrder},
                                {"referenceResolution", {referenceResolution.x, referenceResolution.y}},
                                {"children", nlohmann::json::array()}};
            for (const auto &child : children) json["children"].push_back(child->ToJSON());
            return json;
        }
    };

    class Canvas
    {
    public:
        explicit Canvas(const CanvasData &data)
            : referenceResolution(data.referenceResolution), sortOrder(data.sortOrder)
        {
            for (const auto &child : data.children) addPrepared(*child, nullptr);
        }

        Canvas(const Canvas &) = delete;
        Canvas &operator=(const Canvas &) = delete;
        Canvas(Canvas &&) noexcept = default;
        Canvas &operator=(Canvas &&other) noexcept
        {
            if (this != &other)
            {
                Unload();
                referenceResolution = other.referenceResolution;
                layoutViewportSize = other.layoutViewportSize;
                resolvedSize = other.resolvedSize;
                layoutDirty = other.layoutDirty;
                sortOrder = other.sortOrder;
                children = std::move(other.children);
                nodes = std::move(other.nodes);
            }

            return *this;
        }

        vke_ui::UIComponent *Get(vke_ui::UIHandle handle) const
        {
            auto it = nodes.find(handle);
            return it == nodes.end() ? nullptr : it->second;
        }

        vke_ui::UIComponent *Find(std::string_view name, vke_ui::UIHandle parent = 0) const
        {
            auto *node = Get(parent);
            if (parent && !node) return nullptr;
            for (const auto &child : node ? node->children : children)
                if (child->name == name) return child.get();
            return nullptr;
        }

        const auto &GetChildren() const { return children; }
        vke_common::SceneResult<vke_ui::UIComponent *> Add(const vke_ui::UINodeData &data, vke_ui::UIHandle parent = 0)
        {
            auto *parentNode = Get(parent);
            if (parent && !parentNode) return std::unexpected("invalid UI parent");
            if (auto valid = data.Validate(); !valid) return std::unexpected(valid.error());
            if (auto valid = data.ValidateAssets(); !valid) return std::unexpected(valid.error());
            auto prepared = data.Clone();
            if (auto loaded = prepared->LoadAssets(); !loaded) return std::unexpected(loaded.error());
            return addPrepared(*prepared, parentNode);
        }

        vke_common::SceneResult<void> Remove(vke_ui::UIHandle handle)
        {
            auto *node = Get(handle);
            if (!node) return std::unexpected("invalid UI handle");
            auto &list = siblings(*node);
            unloadSubtree(*node);
            std::erase_if(list, [node](const auto &child) { return child.get() == node; });
            layoutDirty = true;
            return {};
        }

        vke_common::SceneResult<void> SetParent(vke_ui::UIHandle handle, vke_ui::UIHandle parent)
        {
            auto *node = Get(handle);
            auto *parentNode = Get(parent);
            if (!node || (parent && !parentNode)) return std::unexpected("invalid UI handle or parent");
            for (auto *ancestor = parentNode; ancestor; ancestor = ancestor->parent)
                if (ancestor == node) return std::unexpected("UI hierarchy cannot contain a cycle");
            if (node->parent == parentNode) return {};
            auto &source = siblings(*node);
            auto it = std::find_if(source.begin(), source.end(), [node](const auto &child) { return child.get() == node; });
            auto owned = std::move(*it);
            source.erase(it);
            node->parent = parentNode;
            (parentNode ? parentNode->children : children).push_back(std::move(owned));
            node->layoutDirty = true;
            return {};
        }

        vke_common::SceneResult<void> SetSiblingIndex(vke_ui::UIHandle handle, size_t index)
        {
            auto *node = Get(handle);
            if (!node) return std::unexpected("invalid UI handle");
            auto &list = siblings(*node);
            if (index >= list.size()) return std::unexpected("sibling index out of range");
            auto it = std::find_if(list.begin(), list.end(), [node](const auto &child) { return child.get() == node; });
            auto owned = std::move(*it);
            list.erase(it);
            list.insert(list.begin() + index, std::move(owned));
            layoutDirty = true;
            return {};
        }

        vke_common::SceneResult<void> SetReferenceResolution(const glm::vec2 &value)
        {
            if (auto valid = CanvasData::ValidateResolution(value); !valid) return valid;
            referenceResolution = value;
            layoutDirty = true;
            return {};
        }

        const glm::vec2 &GetReferenceResolution() const { return referenceResolution; }
        const glm::vec2 &GetResolvedSize() const { return resolvedSize; }
        int32_t sortOrder = 0;

        void Update(const glm::vec2 &viewportSize, uint32_t &drawOrder)
        {
            if (viewportSize.x <= 0 || viewportSize.y <= 0) return;
            const bool changed = layoutDirty || viewportSize != layoutViewportSize ||
                std::any_of(children.begin(), children.end(), [](const auto &child) { return needsLayout(*child); });
            layoutViewportSize = viewportSize;
            const auto ratio = viewportSize / referenceResolution;
            const float scale = glm::min(ratio.x, ratio.y);
            resolvedSize = viewportSize / scale;
            const auto matrix = glm::scale(glm::mat4(1.0f), glm::vec3(scale, scale, 1.0f));
            if (changed)
            {
                for (auto &child : children)
                {
                    measureAxis(*child, 0);
                    resolveWidths(*child, resolvedSize.x, nullptr);
                    measureAxis(*child, 1);
                    const auto &rect = child->rectTransform;
                    auto size = child->measuredSize;
                    if (child->selfLayout.height == vke_ui::SizeMode::Rect)
                        size.y = glm::max((rect.anchorMax.y - rect.anchorMin.y) * resolvedSize.y +
                            rect.offsetMax.y - rect.offsetMin.y, 0.0f);
                    arrangeSubtree(*child, rect.anchorMin * resolvedSize + rect.offsetMin,
                        size, resolvedSize, glm::vec2(0), matrix);
                }
            }
            for (auto &child : children) updateRenderSubtree(*child, drawOrder);
            layoutDirty = false;
        }

        void FillData(CanvasData &data) const
        {
            data = {};
            data.referenceResolution = referenceResolution;
            data.sortOrder = sortOrder;
            for (const auto &child : children) data.children.push_back(child->ToData());
        }

        void Unload()
        {
            for (auto &child : children) unloadSubtree(*child);
            children.clear();
        }

    private:
        inline static vke_ui::UIHandle nextHandle = 1;
        glm::vec2 referenceResolution;
        glm::vec2 layoutViewportSize{0.0f};
        glm::vec2 resolvedSize{0.0f};
        bool layoutDirty = true;
        std::vector<std::unique_ptr<vke_ui::UIComponent>> children;
        std::unordered_map<vke_ui::UIHandle, vke_ui::UIComponent *> nodes;

        vke_ui::UIComponent *addPrepared(const vke_ui::UINodeData &data, vke_ui::UIComponent *parent)
        {
            std::unique_ptr<vke_ui::UIComponent> owned;
            switch (data.GetType())
            {
            case vke_ui::UIType::Text:
                owned = std::make_unique<vke_ui::UIText>(static_cast<const vke_ui::UITextData &>(data));
                break;
            case vke_ui::UIType::Button:
                owned = std::make_unique<vke_ui::UIButton>(static_cast<const vke_ui::UIButtonData &>(data));
                break;
            case vke_ui::UIType::Image:
                owned = std::make_unique<vke_ui::UIImage>(static_cast<const vke_ui::UIImageData &>(data));
                break;
            case vke_ui::UIType::Container:
                owned = std::make_unique<vke_ui::UIContainer>(static_cast<const vke_ui::UIContainerData &>(data));
                break;
            }
            auto *node = owned.get();
            node->handle = nextHandle++;
            node->parent = parent;
            nodes.emplace(node->handle, node);
            (parent ? parent->children : children).push_back(std::move(owned));
            for (const auto &child : data.children) addPrepared(*child, node);
            return node;
        }

        void unloadSubtree(vke_ui::UIComponent &node);

        static bool needsLayout(const vke_ui::UIComponent &node);
        static void measureAxis(vke_ui::UIComponent &node, int axis);
        static void resolveWidths(vke_ui::UIComponent &node, float parentWidth, const vke_ui::UIGroupLayoutOptions *group);
        static void arrangeSubtree(vke_ui::UIComponent &node, const glm::vec2 &minimum, const glm::vec2 &size,
                                   const glm::vec2 &parentSize, const glm::vec2 &parentPivot, const glm::mat4 &parentMatrix);
        static void updateRenderSubtree(vke_ui::UIComponent &node, uint32_t &drawOrder);

        std::vector<std::unique_ptr<vke_ui::UIComponent>> &siblings(vke_ui::UIComponent &node) { return node.parent ? node.parent->children : children; }
    };
}
