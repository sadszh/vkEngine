#ifndef FONT_SDF_H
#define FONT_SDF_H

#include <common.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <render/texture.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vke_common
{
    enum class GlyphAtlasType : uint32_t
    {
        STATIC_ATLAS = 0,
        DYNAMIC_ATLAS = 1
    };

    struct Glyph
    {
        uint32_t codepoint = 0;
        uint32_t glyphIndex = 0;
        int width = 0;
        int height = 0;
        int bearingX = 0;
        int bearingY = 0;
        int advanceX = 0;
        int atlasX = 0;
        int atlasY = 0;
        GlyphAtlasType atlasType = GlyphAtlasType::STATIC_ATLAS;
        uint32_t slotIndex = 0;
        uint64_t lastUsed = 0;
    };

    class Font
    {
    private:
        struct RasterizedGlyph
        {
            Glyph glyph;
            std::vector<uint8_t> bitmap;
        };

        static constexpr uint32_t INVALID_CODEPOINT = std::numeric_limits<uint32_t>::max();

    public:
        static constexpr uint32_t ATLAS_SIZE = 2048;
        static constexpr uint32_t ATLAS_SLOT_SIZE = 64;
        static constexpr uint32_t ATLAS_SLOT_COUNT = 1024;
        static constexpr int SDF_SPREAD = 8;

        AssetHandle handle = 0;
        std::vector<char> fontData;
        FT_Face face = nullptr;
        std::string familyName;
        std::string styleName;
        uint32_t glyphCount = 0;
        uint32_t pixelSize = 0;
        uint32_t firstCodepoint = 0;
        uint32_t requestedCharacterCount = 0;
        uint32_t loadedCharacterCount = 0;
        uint32_t atlasWidth = ATLAS_SIZE;
        uint32_t atlasHeight = ATLAS_SIZE;
        int ascender = 0;
        int descender = 0;
        int lineHeight = 0;
        std::shared_ptr<vke_render::Texture2D> atlasTexture;
        std::unordered_map<uint32_t, Glyph> glyphs;

        Font()
            : dynamicSlotCodepoints(ATLAS_SLOT_COUNT, INVALID_CODEPOINT),
              staticAtlasPixels(static_cast<size_t>(ATLAS_SIZE) * ATLAS_SIZE, 0),
              dynamicAtlasPixels(static_cast<size_t>(ATLAS_SIZE) * ATLAS_SIZE, 0) {}

        explicit Font(AssetHandle hdl) : Font() { handle = hdl; }

        Font(const Font &) = delete;
        Font &operator=(const Font &) = delete;
        Font(Font &&) = delete;
        Font &operator=(Font &&) = delete;

        ~Font()
        {
            if (face != nullptr)
                FT_Done_Face(face);
        }

        static std::vector<uint32_t> DecodeUTF8(std::string_view text)
        {
            std::vector<uint32_t> result;
            result.reserve(text.size());
            size_t i = 0;
            while (i < text.size())
            {
                const uint8_t first = static_cast<uint8_t>(text[i]);
                uint32_t codepoint = 0;
                size_t length = 0;
                if (first < 0x80)
                {
                    codepoint = first;
                    length = 1;
                }
                else if ((first & 0xE0) == 0xC0)
                {
                    codepoint = first & 0x1F;
                    length = 2;
                }
                else if ((first & 0xF0) == 0xE0)
                {
                    codepoint = first & 0x0F;
                    length = 3;
                }
                else if ((first & 0xF8) == 0xF0)
                {
                    codepoint = first & 0x07;
                    length = 4;
                }
                else
                {
                    result.push_back(0xFFFD);
                    ++i;
                    continue;
                }

                if (i + length > text.size())
                {
                    result.push_back(0xFFFD);
                    break;
                }

                bool valid = true;
                for (size_t j = 1; j < length; ++j)
                {
                    const uint8_t next = static_cast<uint8_t>(text[i + j]);
                    if ((next & 0xC0) != 0x80)
                    {
                        valid = false;
                        break;
                    }
                    codepoint = (codepoint << 6) | (next & 0x3F);
                }

                const uint32_t minimum = length == 1 ? 0 : (length == 2 ? 0x80 : (length == 3 ? 0x800 : 0x10000));
                if (!valid || codepoint < minimum || codepoint > 0x10FFFF ||
                    (codepoint >= 0xD800 && codepoint <= 0xDFFF))
                {
                    result.push_back(0xFFFD);
                    ++i;
                    continue;
                }

                result.push_back(codepoint);
                i += length;
            }
            return result;
        }

        Result<void> BuildStaticAtlas(std::string_view configuredCharacters, uint32_t characterCount, uint32_t startCodepoint)
        {
            VKE_FATAL_IF(face == nullptr, "Cannot build font atlas without a valid FreeType face!")

            firstCodepoint = startCodepoint;
            requestedCharacterCount = characterCount;
            loadedCharacterCount = 0;
            glyphs.clear();
            dynamicGlyphs.clear();
            std::fill(staticAtlasPixels.begin(), staticAtlasPixels.end(), 0);
            std::fill(dynamicAtlasPixels.begin(), dynamicAtlasPixels.end(), 0);
            std::fill(dynamicSlotCodepoints.begin(), dynamicSlotCodepoints.end(), INVALID_CODEPOINT);
            dynamicSlotCount = 0;
            dynamicAtlasUpdateCnt = 0;
            useCounter = 0;

            std::vector<uint32_t> codepoints;
            if (!configuredCharacters.empty())
                codepoints = DecodeUTF8(configuredCharacters);
            else
            {
                codepoints.reserve(characterCount);
                for (uint32_t offset = 0; offset < characterCount; ++offset)
                    codepoints.push_back(startCodepoint + offset);
            }

            std::unordered_set<uint32_t> uniqueCodepoints;
            uint32_t slot = 0;
            for (uint32_t codepoint : codepoints)
            {
                if (slot >= ATLAS_SLOT_COUNT)
                    break;
                if (!uniqueCodepoints.insert(codepoint).second)
                    continue;

                RasterizedGlyph rasterized;
                auto result = rasterizeGlyph(codepoint, GlyphAtlasType::STATIC_ATLAS, slot, rasterized);
                if (!result) return std::unexpected(result.error());
                if (!*result) continue;
                blitGlyph(rasterized, staticAtlasPixels);
                glyphs[codepoint] = rasterized.glyph;
                ++slot;
            }

            loadedCharacterCount = static_cast<uint32_t>(glyphs.size());
            atlasTexture = std::make_shared<vke_render::Texture2D>(
                handle, staticAtlasPixels.data(), ATLAS_SIZE, ATLAS_SIZE,
                VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, false);
            return {};
        }

        Glyph *GetOrCreateGlyph(uint32_t codepoint)
        {
            auto staticIt = glyphs.find(codepoint);
            if (staticIt != glyphs.end())
                return &staticIt->second;

            auto dynamicIt = dynamicGlyphs.find(codepoint);
            if (dynamicIt != dynamicGlyphs.end())
            {
                dynamicIt->second.lastUsed = ++useCounter;
                return &dynamicIt->second;
            }

            uint32_t slot = 0;
            uint32_t evictedCodepoint = INVALID_CODEPOINT;
            if (dynamicSlotCount < ATLAS_SLOT_COUNT)
                slot = dynamicSlotCount;
            else
            {
                auto lru = std::min_element(dynamicGlyphs.begin(), dynamicGlyphs.end(),
                                            [](const auto &a, const auto &b)
                                            { return a.second.lastUsed < b.second.lastUsed; });
                if (lru == dynamicGlyphs.end())
                    return nullptr;
                slot = lru->second.slotIndex;
                evictedCodepoint = lru->first;
            }

            RasterizedGlyph rasterized;
            auto result = rasterizeGlyph(codepoint, GlyphAtlasType::DYNAMIC_ATLAS, slot, rasterized);
            if (!result)
            {
                VKE_LOG_ERROR("font {}: {}", handle, result.error())
                return nullptr;
            }
            if (!*result) return nullptr;

            if (evictedCodepoint != INVALID_CODEPOINT)
                dynamicGlyphs.erase(evictedCodepoint);
            else
                ++dynamicSlotCount;
            rasterized.glyph.lastUsed = ++useCounter;
            clearSlot(dynamicAtlasPixels, slot);
            blitGlyph(rasterized, dynamicAtlasPixels);
            dynamicSlotCodepoints[slot] = codepoint;
            auto [it, inserted] = dynamicGlyphs.emplace(codepoint, rasterized.glyph);
            dynamicAtlasUpdateCnt = vke_render::MAX_FRAMES_IN_FLIGHT;
            return &it->second;
        }

        const std::vector<uint8_t> &GetDynamicAtlasPixels() const { return dynamicAtlasPixels; }
        uint32_t GetDynamicAtlasUpdateCnt() const { return dynamicAtlasUpdateCnt; }
        void ConsumeDynamicAtlasUpdate()
        {
            if (dynamicAtlasUpdateCnt > 0)
                --dynamicAtlasUpdateCnt;
        }

    private:
        std::unordered_map<uint32_t, Glyph> dynamicGlyphs;
        std::vector<uint32_t> dynamicSlotCodepoints;
        std::vector<uint8_t> staticAtlasPixels;
        std::vector<uint8_t> dynamicAtlasPixels;
        uint32_t dynamicSlotCount = 0;
        uint32_t dynamicAtlasUpdateCnt = 0;
        uint64_t useCounter = 0;

        Result<bool> rasterizeGlyph(uint32_t codepoint, GlyphAtlasType atlasType, uint32_t slotIndex, RasterizedGlyph &out)
        {
            const uint32_t glyphIndex = FT_Get_Char_Index(face, codepoint);
            if (glyphIndex == 0 && codepoint != 0)
                return false;

            FT_Error error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT);
            if (error) return std::unexpected("glyph " + std::to_string(codepoint) + ": FT_Load_Glyph failed: " + std::to_string(error));

            const int advanceX = static_cast<int>(face->glyph->advance.x >> 6);
            if (face->glyph->format == FT_GLYPH_FORMAT_OUTLINE && face->glyph->outline.n_contours == 0)
            {
                out.glyph.codepoint = codepoint;
                out.glyph.glyphIndex = glyphIndex;
                out.glyph.advanceX = advanceX;
                out.glyph.atlasType = atlasType;
                out.glyph.slotIndex = slotIndex;
                return true;
            }

            // Bitmap-to-SDF is more robust for complex and intersecting outlines.
            error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
            if (error == FT_Err_Ok)
                error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF);
            if (error) return std::unexpected("glyph " + std::to_string(codepoint) + ": FT_Render_Glyph failed: " + std::to_string(error));

            FT_GlyphSlot ftGlyph = face->glyph;
            const FT_Bitmap &bitmap = ftGlyph->bitmap;
            if (bitmap.width > ATLAS_SLOT_SIZE || bitmap.rows > ATLAS_SLOT_SIZE)
            {
                VKE_LOG_WARN("SDF glyph U+{:04X} is {}x{} and does not fit a {}x{} atlas slot",
                             codepoint, bitmap.width, bitmap.rows, ATLAS_SLOT_SIZE, ATLAS_SLOT_SIZE)
                return false;
            }

            const uint32_t slotsPerRow = ATLAS_SIZE / ATLAS_SLOT_SIZE;
            const uint32_t slotX = (slotIndex % slotsPerRow) * ATLAS_SLOT_SIZE;
            const uint32_t slotY = (slotIndex / slotsPerRow) * ATLAS_SLOT_SIZE;
            const int offsetX = static_cast<int>((ATLAS_SLOT_SIZE - bitmap.width) / 2);
            const int offsetY = static_cast<int>((ATLAS_SLOT_SIZE - bitmap.rows) / 2);

            out.glyph.codepoint = codepoint;
            out.glyph.glyphIndex = glyphIndex;
            out.glyph.width = static_cast<int>(bitmap.width);
            out.glyph.height = static_cast<int>(bitmap.rows);
            out.glyph.bearingX = ftGlyph->bitmap_left;
            out.glyph.bearingY = ftGlyph->bitmap_top;
            out.glyph.advanceX = advanceX;
            out.glyph.atlasX = static_cast<int>(slotX) + offsetX;
            out.glyph.atlasY = static_cast<int>(slotY) + offsetY;
            out.glyph.atlasType = atlasType;
            out.glyph.slotIndex = slotIndex;

            if (bitmap.width == 0 || bitmap.rows == 0)
                return true;

            out.bitmap.resize(static_cast<size_t>(bitmap.width) * bitmap.rows);
            const int pitch = bitmap.pitch;
            for (uint32_t row = 0; row < bitmap.rows; ++row)
            {
                const uint32_t sourceRow = pitch >= 0 ? row : bitmap.rows - 1 - row;
                const uint8_t *source = bitmap.buffer + static_cast<ptrdiff_t>(sourceRow) * std::abs(pitch);
                std::copy_n(source, bitmap.width, out.bitmap.data() + static_cast<size_t>(row) * bitmap.width);
            }
            return true;
        }

        static void clearSlot(std::vector<uint8_t> &atlas, uint32_t slotIndex)
        {
            const uint32_t slotsPerRow = ATLAS_SIZE / ATLAS_SLOT_SIZE;
            const uint32_t slotX = (slotIndex % slotsPerRow) * ATLAS_SLOT_SIZE;
            const uint32_t slotY = (slotIndex / slotsPerRow) * ATLAS_SLOT_SIZE;
            for (uint32_t row = 0; row < ATLAS_SLOT_SIZE; ++row)
            {
                std::fill_n(atlas.data() + static_cast<size_t>(slotY + row) * ATLAS_SIZE + slotX,
                            ATLAS_SLOT_SIZE, uint8_t(0));
            }
        }

        static void blitGlyph(const RasterizedGlyph &glyph, std::vector<uint8_t> &atlas)
        {
            for (int row = 0; row < glyph.glyph.height; ++row)
            {
                std::copy_n(glyph.bitmap.data() + static_cast<size_t>(row) * glyph.glyph.width,
                            glyph.glyph.width,
                            atlas.data() + static_cast<size_t>(glyph.glyph.atlasY + row) * ATLAS_SIZE + glyph.glyph.atlasX);
            }
        }
    };
}

#endif
