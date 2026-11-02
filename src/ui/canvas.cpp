#include <component/canvas.hpp>
#include <script.hpp>

namespace vke_component
{
    bool Canvas::needsLayout(const vke_ui::UIComponent &node)
    {
        return node.layoutDirty || std::any_of(node.children.begin(), node.children.end(),
                                               [](const auto &child)
                                               { return needsLayout(*child); });
    }

    void Canvas::measureAxis(vke_ui::UIComponent &node, int axis)
    {
        using namespace vke_ui;
        for (auto &child : node.children)
            measureAxis(*child, axis);
        const auto &options = node.groupLayout;
        const auto mode = axis == 0 ? node.selfLayout.width : node.selfLayout.height;
        float size = glm::max(node.rectTransform.offsetMax[axis] - node.rectTransform.offsetMin[axis], 0.0f);
        if (mode == SizeMode::Content)
        {
            size = node.measureContent(axis == 0 ? std::nullopt : std::optional<float>(node.measuredSize.x))[axis];
            if (options.direction != LayoutDirection::None)
            {
                const bool mainAxis = axis == (options.direction == LayoutDirection::Horizontal ? 0 : 1);
                float childrenSize = 0;
                bool first = true;
                for (const auto &child : node.children)
                {
                    if (child->selfLayout.ignoreLayout)
                        continue;
                    if (mainAxis)
                    {
                        if (!first)
                            childrenSize += options.spacing;
                        childrenSize += child->measuredSize[axis];
                    }
                    else
                        childrenSize = glm::max(childrenSize, child->measuredSize[axis]);
                    first = false;
                }
                size = glm::max(size, childrenSize + options.padding[axis] + options.padding[axis + 2]);
            }
        }
        node.measuredSize[axis] = size;
    }

    void Canvas::resolveWidths(vke_ui::UIComponent &node, float parentWidth, const vke_ui::UIGroupLayoutOptions *group)
    {
        using namespace vke_ui;
        const auto &options = node.selfLayout;
        const auto &rect = node.rectTransform;
        if (group && !options.ignoreLayout)
        {
            if (group->direction == LayoutDirection::Vertical && group->crossAlignment == LayoutAlignment::Stretch)
                node.measuredSize.x = glm::max(parentWidth - group->padding.x - group->padding.z, 0.0f);
        }
        else if (options.width == SizeMode::Rect)
            node.measuredSize.x = glm::max((rect.anchorMax.x - rect.anchorMin.x) * parentWidth +
                                               rect.offsetMax.x - rect.offsetMin.x,
                                           0.0f);
        for (auto &child : node.children)
            resolveWidths(*child, node.measuredSize.x, node.groupLayout.direction == LayoutDirection::None ? nullptr : &node.groupLayout);
    }

    void Canvas::arrangeSubtree(vke_ui::UIComponent &node, const glm::vec2 &minimum, const glm::vec2 &size,
                                const glm::vec2 &parentSize, const glm::vec2 &parentPivot, const glm::mat4 &parentMatrix)
    {
        using namespace vke_ui;
        const auto &rect = node.rectTransform;
        if (node.resolvedSize != size)
        {
            node.resolvedSize = size;
            node.onSizeChanged();
        }
        const auto position = minimum + rect.pivot * size - parentPivot * parentSize;
        node.layoutMatrix = glm::translate(parentMatrix, glm::vec3(position, 0)) *
                            glm::rotate(glm::mat4(1), rect.rotation, glm::vec3(0, 0, 1)) *
                            glm::scale(glm::mat4(1), glm::vec3(rect.scale, 1));
        node.layoutDirty = false;

        const auto &options = node.groupLayout;
        const bool group = options.direction != LayoutDirection::None;
        const int mainAxis = options.direction == LayoutDirection::Horizontal ? 0 : 1;
        const int crossAxis = 1 - mainAxis;
        const auto innerSize = glm::max(size - glm::vec2(options.padding) -
                                            glm::vec2(options.padding.z, options.padding.w),
                                        glm::vec2(0));
        float occupied = 0;
        bool first = true;
        if (group)
            for (const auto &child : node.children)
            {
                if (child->selfLayout.ignoreLayout)
                    continue;
                if (!first)
                    occupied += options.spacing;
                occupied += child->measuredSize[mainAxis];
                first = false;
            }

        float cursor = options.padding[mainAxis] + glm::max(innerSize[mainAxis] - occupied, 0.0f) *
                                                       (static_cast<int>(options.mainAlignment) * 0.5f);
        for (auto &child : node.children)
        {
            const auto &childOptions = child->selfLayout;
            const auto &childRect = child->rectTransform;
            auto childSize = child->measuredSize;
            glm::vec2 childMinimum;
            if (group && !childOptions.ignoreLayout)
            {
                if (options.crossAlignment == LayoutAlignment::Stretch)
                    childSize[crossAxis] = innerSize[crossAxis];
                childMinimum[mainAxis] = cursor;
                childMinimum[crossAxis] = options.padding[crossAxis];
                if (options.crossAlignment != LayoutAlignment::Stretch)
                    childMinimum[crossAxis] += glm::max(innerSize[crossAxis] - childSize[crossAxis], 0.0f) *
                                               (static_cast<int>(options.crossAlignment) * 0.5f);
                cursor += childSize[mainAxis] + options.spacing;
            }
            else
            {
                childMinimum = childRect.anchorMin * size + childRect.offsetMin;
                if (childOptions.height == SizeMode::Rect)
                    childSize.y = glm::max((childRect.anchorMax.y - childRect.anchorMin.y) * size.y +
                                               childRect.offsetMax.y - childRect.offsetMin.y,
                                           0.0f);
            }
            arrangeSubtree(*child, childMinimum, childSize, size, rect.pivot, node.layoutMatrix);
        }
    }

    void Canvas::updateRenderSubtree(vke_ui::UIComponent &node, uint32_t &drawOrder)
    {
        node.updateRenderTransform(glm::translate(node.layoutMatrix, glm::vec3(-node.rectTransform.pivot * node.resolvedSize, 0)),
                                   static_cast<float>(drawOrder++));
        for (auto &child : node.children)
            updateRenderSubtree(*child, drawOrder);
    }

    void Canvas::unloadSubtree(vke_ui::UIComponent &node)
    {
        for (auto &child : node.children)
            unloadSubtree(*child);
        node.unload();
        nodes.erase(node.handle);
        vke_common::ScriptManager::GetUIFunctions().unregisterNodeCallbacks(node.handle);
    }
}
