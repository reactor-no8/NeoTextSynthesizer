#include "fonts/line_shaper.hpp"

namespace
{
hb_direction_t toHbDirection(TextDirection direction)
{
    return direction == TextDirection::Vertical ? HB_DIRECTION_TTB : HB_DIRECTION_LTR;
}
}

ShapingResult SingleLineShaper::shapeText(FT_Face face,
                                          hb_font_t *hbFont,
                                          const std::string &text,
                                          const ShapingOptions &options)
{
    ShapingResult result;
    result.text = text;
    result.direction = options.direction;

    if (!face || !hbFont || text.empty())
    {
        return result;
    }

    hb_buffer_t *buffer = hb_buffer_create();
    if (!buffer)
    {
        return result;
    }

    hb_buffer_add_utf8(buffer, text.c_str(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buffer);
    hb_buffer_set_direction(buffer, toHbDirection(options.direction));

    if (options.script != HB_SCRIPT_INVALID)
    {
        hb_buffer_set_script(buffer, options.script);
    }

    if (!options.language.empty())
    {
        hb_buffer_set_language(buffer, hb_language_from_string(options.language.c_str(), static_cast<int>(options.language.size())));
    }

    hb_feature_t features[2];
    unsigned int featureCount = 0;
    if (options.direction == TextDirection::Vertical && options.enableVerticalFeatures)
    {
        if (hb_feature_from_string("vert", -1, &features[featureCount]))
        {
            ++featureCount;
        }
        if (hb_feature_from_string("vrt2", -1, &features[featureCount]))
        {
            ++featureCount;
        }
    }

    hb_shape(hbFont, buffer, featureCount > 0 ? features : nullptr, featureCount);

    unsigned int glyphCount = 0;
    hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &glyphCount);
    hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &glyphCount);

    if (infos && positions && glyphCount > 0)
    {
        bool hasRenderableGlyph = false;
        result.glyphs.reserve(glyphCount);
        for (unsigned int i = 0; i < glyphCount; ++i)
        {
            ShapedGlyph g;
            g.glyphIndex = infos[i].codepoint;
            g.cluster = infos[i].cluster;
            g.xAdvance = positions[i].x_advance;
            g.yAdvance = positions[i].y_advance;
            g.xOffset = positions[i].x_offset;
            g.yOffset = positions[i].y_offset;
            hasRenderableGlyph = hasRenderableGlyph || (g.glyphIndex != 0);
            result.glyphs.push_back(g);
        }
        result.success = hasRenderableGlyph;
    }

    hb_buffer_destroy(buffer);
    return result;
}