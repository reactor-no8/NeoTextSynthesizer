#pragma once
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <opencv2/opencv.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <nlohmann/json.hpp>
#include "glyph_cache.hpp"

using json = nlohmann::json;

// ---- Shared (read-only after construction) font metadata ----

#include "paged_bitmap.hpp"

struct SharedFontMeta
{
    std::string path;
    std::shared_ptr<SingleFontBitmap> bitmap; // Unicode codepoints covered by this font
    size_t index;             // stable index into the SharedFontMeta vector (used as GlyphCache key)

    // Font-level metrics obtained once at load time.
    int ascender;  // pixels (from size->metrics, already shifted)
    int descender; // pixels (negative for below baseline)
};

// ---- Shared background resources ----

struct SharedBgResources
{
    std::vector<std::string> bgFiles;

    // Cache frequently used background images and their approximate mean color.
    std::unordered_map<std::string, cv::Mat> bgImageCache;
    std::unordered_map<std::string, cv::Vec3b> bgApproxColorCache;
    size_t bgCacheMax = 64;
    std::mutex bgCacheMutex;
};

// ---- Per-thread font handle ----

struct ThreadFont
{
    FT_Face ftFace = nullptr;
    hb_font_t *hbFont = nullptr;
    const SharedFontMeta *meta = nullptr; // points into the shared vector
};

// ---- Background description ----

struct BgInfo
{
    bool isImage;
    std::string imagePath;
    cv::Vec3b color; // used when !isImage
};

class Renderer
{
public:
    // Construct a per-thread renderer.
    // `defaultMeta` is the shared font metadata vectors.
    // `glyphCache` is the shared glyph bitmap cache.
    // `bgRes` is the shared background resource pool.
    Renderer(const json &postCfg,
             const json &bgCfg,
             const std::vector<SharedFontMeta> &defaultMeta,
             std::shared_ptr<MultiFontBitmap<256>> multiFontBitmap,
             GlyphCache &glyphCache,
             SharedBgResources &bgRes);
    ~Renderer();

    // Non-copyable, movable
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    Renderer(Renderer &&) = default;
    Renderer &operator=(Renderer &&) = default;

    // ---- Background helpers ----
    BgInfo getRandomBgPredict();
    cv::Vec3b getBgApproxColor(const BgInfo &bg);
    std::pair<cv::Mat, cv::Vec3b> getBgCropAndColor(const BgInfo &bg, int tw, int th);
    cv::Vec3b getTextColor(const cv::Vec3b &bgColor);

    // ---- Text rendering ----
    cv::Mat renderTightText(std::string &text, const cv::Vec3b &bgColor, const std::string& sampleStrategy);

    // ---- Static builders ----

    // Load all fonts from `dir`, build SharedFontMeta entries starting at `indexOffset`.
    // Returns the metadata vector; no FT_Face is kept (caller manages FT_Library lifetime).
    static std::vector<SharedFontMeta> buildSharedFontMeta(
        const std::string &dir, int fontSize, FT_Library ftLib, size_t indexOffset = 0);

    // Load background file list from `dir`.
    static std::vector<std::string> listBgFiles(const std::string &dir);

private:
    json postCfg_;
    json bgCfg_;
    int fontSize_;

    // References to shared, read-only data.
    const std::vector<SharedFontMeta> &defaultMeta_;
    std::shared_ptr<MultiFontBitmap<256>> multiFontBitmap_;
    GlyphCache &glyphCache_;
    SharedBgResources &bgRes_;

    // Per-thread FT library + font handles.
    FT_Library ftLib_ = nullptr;
    std::vector<ThreadFont> defaultFonts_;

    // Open per-thread FT_Face / hb_font for each SharedFontMeta entry.
    void openThreadFonts(const std::vector<SharedFontMeta> &meta,
                         std::vector<ThreadFont> &out);

    // Rasterise or fetch from cache.  Returns a pointer to a CachedGlyph.
    const CachedGlyph *getGlyph(const ThreadFont &tf, uint32_t codepoint);

    // Composite a cached glyph bitmap onto a BGRA canvas at (x, y).
    static void compositeGlyph(cv::Mat &canvas, const CachedGlyph &glyph,
                                int x, int y, const cv::Vec4b &color);
};