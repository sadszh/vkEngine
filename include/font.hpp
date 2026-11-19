#ifndef FONT_H
#define FONT_H

#include <font_sdf.hpp>

// Compatibility wrapper; the SDF implementation is kept in a focused header.
#if 0

#include <common.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <render/texture.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vke_common
{
    struct Glyph
    {
        uint32_t codepoint;
        uint32_t glyphIndex;
        int width;
        int height;
        int bearingX;
        int bearingY;
        int advanceX;
        int atlasX;
        int atlasY;
        float u0;
        float v0;
        float u1;
        float v1;

        Glyph()
            : codepoint(0), glyphIndex(0), width(0), height(0), bearingX(0), bearingY(0),
              advanceX(0), atlasX(0), atlasY(0), u0(0.0f), v0(0.0f), u1(0.0f), v1(0.0f) {}
    };

    class Font
    {
    private:
        struct RasterizedGlyph
        {
            Glyph glyph;
            std::vector<uint8_t> bitmap;
        };

    public:
        AssetHandle handle;
        FT_Face face;
        std::string familyName;
        std::string styleName;
        uint32_t glyphCount;
        uint32_t pixelSize;
        uint32_t firstCodepoint;
        uint32_t requestedCharacterCount;
        uint32_t loadedCharacterCount;
        uint32_t atlasWidth;
        uint32_t atlasHeight;
        int ascender;
        int descender;
        int lineHeight;
        std::shared_ptr<vke_render::Texture2D> atlasTexture;
        std::unordered_map<uint32_t, Glyph> glyphs;

        Font()
            : handle(0), face(nullptr), glyphCount(0), pixelSize(0), firstCodepoint(0), requestedCharacterCount(0), loadedCharacterCount(0),
              atlasWidth(0), atlasHeight(0), ascender(0), descender(0), lineHeight(0), atlasTexture(nullptr) {}
        Font(AssetHandle hdl)
            : handle(hdl), face(nullptr), glyphCount(0), pixelSize(0), firstCodepoint(0), requestedCharacterCount(0), loadedCharacterCount(0),
              atlasWidth(0), atlasHeight(0), ascender(0), descender(0), lineHeight(0), atlasTexture(nullptr) {}

        Font(const Font &) = delete;
        Font &operator=(const Font &) = delete;

        Font(Font &&ano) noexcept
            : handle(ano.handle), face(ano.face), familyName(std::move(ano.familyName)),
              styleName(std::move(ano.styleName)), glyphCount(ano.glyphCount), pixelSize(ano.pixelSize),
              firstCodepoint(ano.firstCodepoint), requestedCharacterCount(ano.requestedCharacterCount), loadedCharacterCount(ano.loadedCharacterCount),
              atlasWidth(ano.atlasWidth), atlasHeight(ano.atlasHeight), ascender(ano.ascender),
              descender(ano.descender), lineHeight(ano.lineHeight), atlasTexture(std::move(ano.atlasTexture)),
              glyphs(std::move(ano.glyphs))
        {
            ano.face = nullptr;
            ano.glyphCount = 0;
            ano.pixelSize = 0;
            ano.firstCodepoint = 0;
            ano.requestedCharacterCount = 0;
            ano.loadedCharacterCount = 0;
            ano.atlasWidth = 0;
            ano.atlasHeight = 0;
            ano.ascender = 0;
            ano.descender = 0;
            ano.lineHeight = 0;
        }

        Font &operator=(Font &&ano) noexcept
        {
            if (this == &ano)
                return *this;

            if (face != nullptr)
                FT_Done_Face(face);

            handle = ano.handle;
            face = ano.face;
            familyName = std::move(ano.familyName);
            styleName = std::move(ano.styleName);
            glyphCount = ano.glyphCount;
            pixelSize = ano.pixelSize;
            firstCodepoint = ano.firstCodepoint;
            requestedCharacterCount = ano.requestedCharacterCount;
            loadedCharacterCount = ano.loadedCharacterCount;
            atlasWidth = ano.atlasWidth;
            atlasHeight = ano.atlasHeight;
            ascender = ano.ascender;
            descender = ano.descender;
            lineHeight = ano.lineHeight;
            atlasTexture = std::move(ano.atlasTexture);
            glyphs = std::move(ano.glyphs);

            ano.face = nullptr;
            ano.glyphCount = 0;
            ano.pixelSize = 0;
            ano.firstCodepoint = 0;
            ano.requestedCharacterCount = 0;
            ano.loadedCharacterCount = 0;
            ano.atlasWidth = 0;
            ano.atlasHeight = 0;
            ano.ascender = 0;
            ano.descender = 0;
            ano.lineHeight = 0;
            return *this;
        }

        ~Font()
        {
            if (face != nullptr)
                FT_Done_Face(face);
        }

        const Glyph *GetGlyph(uint32_t codepoint) const
        {
            auto it = glyphs.find(codepoint);
            if (it == glyphs.end())
                return nullptr;
            return &(it->second);
        }

        void BuildAtlas(uint32_t characterCount, uint32_t startCodepoint = 32, int padding = 1)
        {
            VKE_FATAL_IF(face == nullptr, "Cannot build font atlas without a valid FreeType face!")

            firstCodepoint = startCodepoint;
            requestedCharacterCount = characterCount;
            glyphs.clear();
            atlasTexture.reset();
            atlasWidth = 0;
            atlasHeight = 0;
            loadedCharacterCount = 0;

            if (characterCount == 0)
                return;

            std::vector<RasterizedGlyph> rasterizedGlyphs;
            rasterizedGlyphs.reserve(characterCount);

            uint64_t totalArea = 0;
            uint32_t maxGlyphWidth = 0;
            uint32_t maxGlyphHeight = 0;

            for (uint32_t offset = 0; offset < characterCount; ++offset)
            {
                const uint32_t codepoint = startCodepoint + offset;
                FT_Error error = FT_Load_Char(face, codepoint, FT_LOAD_RENDER);
                if (error != FT_Err_Ok)
                    continue;

                FT_GlyphSlot slot = face->glyph;
                FT_Bitmap &bitmap = slot->bitmap;

                RasterizedGlyph entry;
                entry.glyph.codepoint = codepoint;
                entry.glyph.glyphIndex = FT_Get_Char_Index(face, codepoint);
                entry.glyph.width = static_cast<int>(bitmap.width);
                entry.glyph.height = static_cast<int>(bitmap.rows);
                entry.glyph.bearingX = slot->bitmap_left;
                entry.glyph.bearingY = slot->bitmap_top;
                entry.glyph.advanceX = static_cast<int>(slot->advance.x >> 6);

                if (bitmap.width > 0 && bitmap.rows > 0)
                {
                    entry.bitmap.resize(static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.rows));
                    for (uint32_t row = 0; row < bitmap.rows; ++row)
                    {
                        std::copy_n(bitmap.buffer + row * bitmap.pitch, bitmap.width,
                                    entry.bitmap.data() + row * bitmap.width);
                    }
                }

                totalArea += static_cast<uint64_t>(std::max(entry.glyph.width, 1) + padding) *
                             static_cast<uint64_t>(std::max(entry.glyph.height, 1) + padding);
                maxGlyphWidth = std::max(maxGlyphWidth, static_cast<uint32_t>(std::max(entry.glyph.width, 1)));
                maxGlyphHeight = std::max(maxGlyphHeight, static_cast<uint32_t>(std::max(entry.glyph.height, 1)));
                rasterizedGlyphs.push_back(std::move(entry));
            }

            loadedCharacterCount = static_cast<uint32_t>(rasterizedGlyphs.size());
            if (rasterizedGlyphs.empty())
                return;

            const uint32_t minSide = std::max(maxGlyphWidth + static_cast<uint32_t>(padding * 2),
                                              maxGlyphHeight + static_cast<uint32_t>(padding * 2));
            uint32_t atlasSide = nextPow2(std::max(minSide, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(totalArea))))));
            while (!tryPackGlyphs(rasterizedGlyphs, atlasSide, atlasSide, padding))
                atlasSide <<= 1;

            atlasWidth = atlasSide;
            atlasHeight = atlasSide;

            std::vector<uint8_t> atlasPixels(static_cast<size_t>(atlasSide) * static_cast<size_t>(atlasSide) * 4, 0);
            for (auto &glyph : rasterizedGlyphs)
            {
                blitGlyphBitmap(glyph, atlasPixels, atlasSide);
                glyph.glyph.u0 = static_cast<float>(glyph.glyph.atlasX) / static_cast<float>(atlasSide);
                glyph.glyph.v0 = static_cast<float>(glyph.glyph.atlasY) / static_cast<float>(atlasSide);
                glyph.glyph.u1 = static_cast<float>(glyph.glyph.atlasX + glyph.glyph.width) / static_cast<float>(atlasSide);
                glyph.glyph.v1 = static_cast<float>(glyph.glyph.atlasY + glyph.glyph.height) / static_cast<float>(atlasSide);
                glyphs[glyph.glyph.codepoint] = glyph.glyph;
            }

            atlasTexture = std::make_shared<vke_render::Texture2D>(
                handle,
                atlasPixels.data(),
                static_cast<int>(atlasSide),
                static_cast<int>(atlasSide),
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_FILTER_LINEAR,
                VK_FILTER_LINEAR,
                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                false);
        }

    private:
        static uint32_t nextPow2(uint32_t value)
        {
            uint32_t ret = 1;
            while (ret < value)
                ret <<= 1;
            return ret;
        }

        static bool tryPackGlyphs(std::vector<RasterizedGlyph> &glyphs, uint32_t atlasWidth, uint32_t atlasHeight, int padding)
        {
            int penX = padding;
            int penY = padding;
            int rowHeight = 0;
            for (auto &glyph : glyphs)
            {
                const int width = std::max(glyph.glyph.width, 1);
                const int height = std::max(glyph.glyph.height, 1);

                if (penX + width + padding > static_cast<int>(atlasWidth))
                {
                    penX = padding;
                    penY += rowHeight + padding;
                    rowHeight = 0;
                }

                if (penY + height + padding > static_cast<int>(atlasHeight))
                    return false;

                glyph.glyph.atlasX = penX;
                glyph.glyph.atlasY = penY;
                penX += width + padding;
                rowHeight = std::max(rowHeight, height);
            }
            return true;
        }

        static void blitGlyphBitmap(const RasterizedGlyph &glyph, std::vector<uint8_t> &atlasPixels, uint32_t atlasSide)
        {
            for (int row = 0; row < glyph.glyph.height; ++row)
            {
                for (int col = 0; col < glyph.glyph.width; ++col)
                {
                    const uint8_t value = glyph.bitmap[static_cast<size_t>(row) * static_cast<size_t>(glyph.glyph.width) + static_cast<size_t>(col)];
                    const size_t atlasIndex = (static_cast<size_t>(glyph.glyph.atlasY + row) * static_cast<size_t>(atlasSide) +
                                               static_cast<size_t>(glyph.glyph.atlasX + col)) *
                                              4;
                    atlasPixels[atlasIndex] = 255;
                    atlasPixels[atlasIndex + 1] = 255;
                    atlasPixels[atlasIndex + 2] = 255;
                    atlasPixels[atlasIndex + 3] = value;
                }
            }
        }
    };
}

#endif
#endif
