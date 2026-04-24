#pragma once

#include <memory>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>
#include <nlohmann/json.hpp>

#include "algorithms/glyph_cache.hpp"
#include "algorithms/paged_bitmap.hpp"
#include "backgrounds/background_resources.hpp"
#include "fonts/font_resource.hpp"
#include "fonts/line_shaper.hpp"
#include "fonts/font_selector.hpp"
#include "fonts/library.hpp"

using json = nlohmann::json;

struct ThreadFont
{
    FT_Face ftFace = nullptr;
    hb_font_t *hbFont = nullptr;
    const SharedFontMeta *meta = nullptr;
};


class SingleLineRenderer
{
public:
    SingleLineRenderer(const json &config,
                     GlyphCache &glyphCache,
                     FontSelector &fontSelector);
    ~SingleLineRenderer();

    SingleLineRenderer(const SingleLineRenderer &) = delete;
    SingleLineRenderer &operator=(const SingleLineRenderer &) = delete;
    
    // Renders text using the specified font, returning a mask with alpha channel only
    cv::Mat renderTightText(std::string &text,
                          size_t fontIndex,
                          TextDirection direction);

    // Get a cached glyph for rendering by its index
    const CachedGlyph *getGlyphByIndex(const FontResource &fontRes, uint32_t glyphIndex);

private:
    json config_;
    int fontSize_ = 55;
    GlyphCache &glyphCache_;
    FontSelector &fontSelector_;
    FontLibrary fontLibrary_;
};