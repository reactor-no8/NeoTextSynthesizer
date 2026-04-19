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
#include "fonts/line_shaper.hpp"
#include "text_synth/text_synthesizer.hpp"

using json = nlohmann::json;

struct ThreadFont
{
    FT_Face ftFace = nullptr;
    hb_font_t *hbFont = nullptr;
    const SharedFontMeta *meta = nullptr;
};

struct BgInfo
{
    bool isImage = false;
    std::string imagePath;
    cv::Vec3b color;
};

class SingleLineRender
{
public:
    SingleLineRender(const json &config,
                     const std::vector<SharedFontMeta> &defaultMeta,
                     std::shared_ptr<MultiFontBitmap<256>> multiFontBitmap,
                     GlyphCache &glyphCache,
                     BackgroundResources &bgRes);
    ~SingleLineRender();

    SingleLineRender(const SingleLineRender &) = delete;
    SingleLineRender &operator=(const SingleLineRender &) = delete;

    BgInfo getRandomBgPredict();
    cv::Vec3b getBgApproxColor(const BgInfo &bg);
    std::pair<cv::Mat, cv::Vec3b> getBgCropAndColor(const BgInfo &bg, int tw, int th);
    cv::Vec3b getTextColor(const cv::Vec3b &bgColor);
    cv::Mat renderTightText(std::string &text,
                            const cv::Vec3b &bgColor,
                            const std::string &sampleStrategy,
                            TextDirection direction);

    static std::vector<SharedFontMeta> buildSharedFontMeta(
        const std::string &dir, int fontSize, FT_Library ftLib, size_t indexOffset = 0);

private:
    json config_;
    int fontSize_ = 55;
    const std::vector<SharedFontMeta> &defaultMeta_;
    std::shared_ptr<MultiFontBitmap<256>> multiFontBitmap_;
    GlyphCache &glyphCache_;
    BackgroundResources &bgRes_;

    FT_Library ftLib_ = nullptr;
    std::vector<ThreadFont> defaultFonts_;

    void openThreadFonts(const std::vector<SharedFontMeta> &meta,
                         std::vector<ThreadFont> &out);
    const CachedGlyph *getGlyphByIndex(const ThreadFont &tf, uint32_t glyphIndex);
    static void compositeGlyph(cv::Mat &canvas, const CachedGlyph &glyph,
                               int x, int y, const cv::Vec4b &color);
};