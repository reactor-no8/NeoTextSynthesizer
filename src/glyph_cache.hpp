#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <shared_mutex>

// Pre-rasterised glyph bitmap stored in the shared cache.
struct CachedGlyph
{
    int bitmapLeft = 0;
    int bitmapTop = 0;
    int advanceX = 0;
    int rows = 0;
    int width = 0;
    int pitch = 0;
    std::vector<uint8_t> buffer; // grayscale bitmap copied out of FreeType
};

// Thread-safe glyph bitmap cache shared by all Renderer instances.
class GlyphCache
{
public:
    // Look up a cached glyph.  Returns nullptr on miss.
    const CachedGlyph *find(size_t fontIdx, uint32_t codepoint) const;

    // Insert a glyph into the cache and return a stable pointer to it.
    // If another thread raced and inserted the same key first, the existing
    // entry is returned and `g` is discarded.
    const CachedGlyph *insert(size_t fontIdx, uint32_t codepoint, CachedGlyph &&g);

private:
    static uint64_t makeKey(size_t fontIdx, uint32_t codepoint)
    {
        return (static_cast<uint64_t>(fontIdx) << 32) | codepoint;
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<uint64_t, CachedGlyph> cache_;
};