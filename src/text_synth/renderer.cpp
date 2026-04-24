#include "text_synth/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>

#include "algorithms/alpha_blend.hpp"
#include "utils/utf8_helper.hpp"
#include "utils/utils.hpp"

namespace
{
constexpr int kHbScale = 64;

cv::Rect computeAlphaBoundingBox(const cv::Mat &img)
{
    cv::Rect bbox(0, 0, 0, 0);
    for (int r = 0; r < img.rows; ++r)
    {
        for (int c = 0; c < img.cols; ++c)
        {
            if (img.at<cv::Vec4b>(r, c)[3] == 0)
            {
                continue;
            }

            if (bbox.width == 0 && bbox.height == 0)
            {
                bbox = cv::Rect(c, r, 1, 1);
            }
            else
            {
                bbox.x = std::min(bbox.x, c);
                bbox.y = std::min(bbox.y, r);
                bbox.width = std::max(bbox.x + bbox.width, c + 1) - bbox.x;
                bbox.height = std::max(bbox.y + bbox.height, r + 1) - bbox.y;
            }
        }
    }
    return bbox;
}
} // namespace

SingleLineRenderer::SingleLineRenderer(const json &config,
                                  GlyphCache &glyphCache,
                                  FontSelector &fontSelector)
    : config_(config),
      glyphCache_(glyphCache),
      fontSelector_(fontSelector)
{
    fontSize_ = config_.value("font_size", 55);
    if (config_.contains("text_sampler"))
    {
        fontSize_ = config_["text_sampler"].value("font_size", fontSize_);
    }
}

SingleLineRenderer::~SingleLineRenderer()
{
}

const CachedGlyph *SingleLineRenderer::getGlyphByIndex(const FontResource &fontRes, uint32_t glyphIndex)
{
    const size_t fontIdx = fontRes.getDescriptor().index;
    const CachedGlyph *cached = glyphCache_.find(fontIdx, glyphIndex);
    if (cached)
    {
        return cached;
    }
    
    if (glyphIndex == 0)
    {
        return nullptr;
    }

    FT_Face ftFace = fontRes.getFTFace();
    if (!ftFace)
    {
        return nullptr;
    }

    if (FT_Load_Glyph(ftFace, glyphIndex, FT_LOAD_RENDER))
    {
        return nullptr;
    }

    FT_Bitmap &bmp = ftFace->glyph->bitmap;
    CachedGlyph g;
    g.bitmapLeft = ftFace->glyph->bitmap_left;
    g.bitmapTop = ftFace->glyph->bitmap_top;
    g.advanceX = static_cast<int>(ftFace->glyph->advance.x >> 6);
    g.rows = static_cast<int>(bmp.rows);
    g.width = static_cast<int>(bmp.width);
    g.pitch = static_cast<int>(bmp.pitch);
    g.buffer.assign(bmp.buffer, bmp.buffer + static_cast<size_t>(bmp.rows) * std::abs(bmp.pitch));
    return glyphCache_.insert(fontIdx, glyphIndex, std::move(g));
}

cv::Mat SingleLineRenderer::renderTightText(std::string &text,
                                        size_t fontIndex,
                                        TextDirection direction)
{
    try {
        // Get the font resource
        const FontResource &fontRes = fontSelector_.getFont(fontIndex);
        
        auto chars = UTF8Helper::Split(text);
        auto codepoints = UTF8Helper::ToCodepoints(text);
        if (chars.empty() || codepoints.empty())
        {
            return {};
        }
        
        if (chars.empty())
            return {};
            
        // Get font metrics
        int ascender = fontRes.getFTFace()->size->metrics.ascender >> 6;
        int descender = fontRes.getFTFace()->size->metrics.descender >> 6;

        // dimensions depend on text direction
        const int fs = fontSize_;
        const int glyphCount = static_cast<int>(chars.size());

        int canvasW, canvasH;
        double penX, penY;
        if (direction == TextDirection::Vertical)
        {
            // Vertical: characters advance downward along Y
            canvasW = fs * 6;
            canvasH = glyphCount * fs * 4 + fs * 4;
            penX = fs * 3.0; // centered horizontally
            penY = fs * 2.0; // top margin
        }
        else
        {
            // Horizontal: characters advance rightward along X
            canvasW = glyphCount * fs * 4 + fs * 4;
            canvasH = fs * 6;
            penX = fs * 2.0;
            penY = fs * 3.0; // baseline
        }
        
        // Create a transparent canvas for alpha mask
        cv::Mat alphaMask(canvasH, canvasW, CV_8UC4, cv::Scalar(0, 0, 0, 0));
        
        const double baselineX = penX;
        const double baselineY = penY;
        
        // Create a white text color (will only use alpha channel)
        cv::Vec4b textColor(255, 255, 255, 255);
        
        // Shape and render text
        ShapingOptions shapingOptions;
        shapingOptions.direction = direction;
        
        ShapingResult shaping = SingleLineShaper::shapeText(fontRes.getFTFace(), 
                                                          fontRes.getHBFont(), 
                                                          text, 
                                                          shapingOptions);
        
        if (shaping.success && !shaping.glyphs.empty())
        {
            for (const auto &sg : shaping.glyphs)
            {
                const CachedGlyph *glyph = getGlyphByIndex(fontRes, sg.glyphIndex);
                
                const int drawX = static_cast<int>(std::lround(penX + static_cast<double>(sg.xOffset) / kHbScale));
                const int drawY = static_cast<int>(std::lround(penY - static_cast<double>(sg.yOffset) / kHbScale));
                
                if (glyph)
                {
                    // Use the alpha blending function for glyphs
                    alpha_blend::compositeGlyph(alphaMask, *glyph, drawX, drawY, textColor);
                }
                
                penX += static_cast<double>(sg.xAdvance) / kHbScale;
                penY -= static_cast<double>(sg.yAdvance) / kHbScale; // for vertical text
            }
        }
        
        // Crop to bounding box of non-transparent pixels
        cv::Rect bbox = computeAlphaBoundingBox(alphaMask);
        if (bbox.width <= 0 || bbox.height <= 0)
        {
            return {};
        }
        
        // Expand vertical bbox to full font metrics (ascender / descender)
        const int fontTop = static_cast<int>(baselineY) - ascender;
        const int fontBottom = static_cast<int>(baselineY) - descender;
        
        int newTop = std::min(bbox.y, fontTop);
        int newBottom = std::max(bbox.y + bbox.height, fontBottom);
        newTop = std::max(0, newTop);
        newBottom = std::min(alphaMask.rows, newBottom);
        
        bbox.y = newTop;
        bbox.height = newBottom - newTop;
        
        if (bbox.width <= 0 || bbox.height <= 0)
        {
            return {};
        }
        
        return alphaMask(bbox).clone();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error in renderTightText: " << e.what() << std::endl;
        return {};
    }
}