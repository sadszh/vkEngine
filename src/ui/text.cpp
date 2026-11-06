#include <ui/text.hpp>

namespace vke_ui
{
    UIText::UIText(const UITextData &data)
        : UIRenderableComponent(data, data.material.Get()), text(data.text),
          font(vke_common::AssetManager::LoadFont(vke_common::BUILTIN_FONT_ARIAL_ID)), options(data.layout)
    {
        prepareCharacters();
        layout = LayoutText(characters, options.fontSize / font->pixelSize, static_cast<float>(font->lineHeight),
                            options.wordWrap ? std::optional<float>(resolvedSize.x) : std::nullopt);
        if (!rebuildGeometry(data.color))
        {
            vke_render::QuadInstanceGPU quad{};
            quad.color = data.color;
            setGeometry({quad});
            VKE_LOG_ERROR("Cannot allocate UIText geometry for {}", name);
        }
    }

    void UIText::prepareCharacters()
    {
        characters.clear();
        characters.reserve(text.size());
        bool carriageReturn = false;
        for (uint32_t codepoint : vke_common::Font::DecodeUTF8(text))
        {
            if (codepoint == '\n' && carriageReturn)
            {
                carriageReturn = false;
                continue;
            }
            carriageReturn = codepoint == '\r';
            if (carriageReturn)
                codepoint = '\n';
            if (codepoint == '\n')
            {
                characters.push_back({codepoint, 0, {}});
                continue;
            }
            auto *glyph = font->GetOrCreateGlyph(codepoint == '\t' ? ' ' : codepoint);
            if (!glyph)
                glyph = font->GetOrCreateGlyph('?');
            const auto value = glyph ? *glyph : vke_common::Glyph{};
            characters.push_back({codepoint, static_cast<float>(value.advanceX) * (codepoint == '\t' ? 4 : 1), value});
        }
    }

    void UIText::SetText(std::string_view newText)
    {
        if (text == newText)
            return;
        text.assign(newText);
        prepareCharacters();
        invalidateLayout();
        updateLayout();
    }

    vke_common::Result<void> UIText::SetLayoutOptions(const TextLayoutOptions &value)
    {
        if (auto valid = value.Validate(); !valid)
            return valid;
        if (options == value)
            return {};
        const bool reflow = options.fontSize != value.fontSize || options.wordWrap != value.wordWrap;
        options = value;
        if (reflow)
        {
            invalidateLayout();
            updateLayout();
        }
        else if (!rebuildGeometry(GetColor()))
            VKE_LOG_ERROR("Cannot allocate UIText geometry for {}", name);
        return {};
    }

    glm::vec2 UIText::Measure() const
    {
        const auto result = LayoutText(characters, options.fontSize / font->pixelSize, static_cast<float>(font->lineHeight));
        return {result.width, result.height};
    }

    vke_common::Result<glm::vec2> UIText::Measure(float maxWidth) const
    {
        if (!std::isfinite(maxWidth) || maxWidth < 0)
            return std::unexpected("measurement width must be finite and nonnegative");
        const auto result = LayoutText(characters, options.fontSize / font->pixelSize,
                                       static_cast<float>(font->lineHeight), maxWidth);
        return glm::vec2(result.width, result.height);
    }

    glm::vec2 UIText::measureContent(std::optional<float> width) const
    {
        const auto result = LayoutText(characters, options.fontSize / font->pixelSize,
            static_cast<float>(font->lineHeight), options.wordWrap ? width : std::nullopt);
        return {result.width, result.height};
    }

    void UIText::updateLayout()
    {
        layout = LayoutText(characters, options.fontSize / font->pixelSize, static_cast<float>(font->lineHeight),
                            options.wordWrap ? std::optional<float>(resolvedSize.x) : std::nullopt);
        if (!rebuildGeometry(GetColor()))
            VKE_LOG_ERROR("Cannot allocate UIText geometry for {}", name);
    }

    void UIText::onSizeChanged()
    {
        if (options.wordWrap && layout.wrapWidth != resolvedSize.x)
            updateLayout();
        else if (options.horizontalAlignment != TextHorizontalAlignment::Left ||
                 options.verticalAlignment != TextVerticalAlignment::Top)
            if (!rebuildGeometry(GetColor()))
                VKE_LOG_ERROR("Cannot allocate UIText geometry for {}", name);
    }

    bool UIText::rebuildGeometry(glm::vec4 color)
    {
        const float scale = options.fontSize / font->pixelSize;
        const float vertical = static_cast<int>(options.verticalAlignment) * 0.5f;
        float baseline = (resolvedSize.y - layout.height) * vertical + font->ascender * scale;
        std::vector<vke_render::QuadInstanceGPU> quads;
        quads.reserve(characters.size());
        for (const auto &line : layout.lines)
        {
            float pen = (resolvedSize.x - line.width) * (static_cast<int>(options.horizontalAlignment) * 0.5f);
            for (size_t i = line.begin; i < line.end; ++i)
            {
                const auto &character = characters[i];
                const auto &glyph = character.glyph;
                if (character.codepoint != '\t' && glyph.width > 0 && glyph.height > 0)
                {
                    const float x = pen + glyph.bearingX * scale;
                    const float y = baseline - glyph.bearingY * scale;
                    vke_render::QuadInstanceGPU quad{};
                    quad.quadRect = {x, y, x + glyph.width * scale, y + glyph.height * scale};
                    quad.uvRect = {glyph.atlasX, glyph.atlasY, glyph.atlasX + glyph.width, glyph.atlasY + glyph.height};
                    quad.color = color;
                    quad.atlasInfo.x = static_cast<uint32_t>(glyph.atlasType);
                    quads.push_back(quad);
                }
                pen += character.advance * scale;
            }
            baseline += font->lineHeight * scale;
        }
        if (quads.empty())
        {
            vke_render::QuadInstanceGPU quad{};
            quad.color = color;
            quads.push_back(quad);
        }
        if (quads.size() != quadIDs.size())
            return setGeometry(std::move(quads));
        auto *manager = vke_render::Renderer::GetQuadManager();
        for (size_t i = 0; i < quads.size(); ++i)
            manager->Update(quadIDs[i], quads[i]);
        return true;
    }
}
