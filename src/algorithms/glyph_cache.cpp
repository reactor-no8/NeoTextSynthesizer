#include "glyph_cache.hpp"
#include <mutex>

const CachedGlyph *GlyphCache::find(size_t fontIdx, uint32_t glyphIndex) const
{
    uint64_t key = makeKey(fontIdx, glyphIndex);
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = cache_.find(key);
    if (it == cache_.end())
        return nullptr;
    return &it->second;
}

const CachedGlyph *GlyphCache::insert(size_t fontIdx, uint32_t glyphIndex, CachedGlyph &&g)
{
    uint64_t key = makeKey(fontIdx, glyphIndex);
    std::unique_lock<std::shared_mutex> lock(mutex_);
    // Another thread may have inserted while we were rasterising; check again.
    auto [it, inserted] = cache_.try_emplace(key, std::move(g));
    return &it->second;
}