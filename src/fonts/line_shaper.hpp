#pragma once

#include <string>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

enum class TextDirection
{
    Horizontal,
    Vertical
};

struct ShapingOptions
{
    TextDirection direction = TextDirection::Horizontal;
    bool enableVerticalFeatures = true;
    hb_script_t script = HB_SCRIPT_INVALID;
    std::string language;
};

struct ShapedGlyph
{
    uint32_t glyphIndex = 0;
    uint32_t cluster = 0;
    int xAdvance = 0;
    int yAdvance = 0;
    int xOffset = 0;
    int yOffset = 0;
};

struct ShapingResult
{
    std::string text;
    TextDirection direction = TextDirection::Horizontal;
    std::vector<ShapedGlyph> glyphs;
    bool success = false;
};

class SingleLineShaper
{
public:
    static ShapingResult shapeText(FT_Face face,
                                   hb_font_t *hbFont,
                                   const std::string &text,
                                   const ShapingOptions &options);
};