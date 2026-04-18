#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include "algorithms/glyph_cache.hpp"
#include "algorithms/paged_bitmap.hpp"

using json = nlohmann::json;

class TextSampler;
class SingleLineRender;
class BackgroundResources;

struct SharedFontMeta
{
    std::string path;
    std::shared_ptr<SingleFontBitmap> bitmap;
    size_t index = 0;
    int ascender = 0;
    int descender = 0;
};

struct SingleLineImageResult
{
    cv::Mat image;
    std::string text;
    int width = 0;
    int height = 0;
};

class SingleLineTextSynthesizer
{
public:
    explicit SingleLineTextSynthesizer(const std::string &configStr);
    ~SingleLineTextSynthesizer();

    SingleLineTextSynthesizer(const SingleLineTextSynthesizer &) = delete;
    SingleLineTextSynthesizer &operator=(const SingleLineTextSynthesizer &) = delete;

    SingleLineImageResult generateSingleImage(const std::string &text) const;

    struct ImageResult
    {
        std::vector<uint8_t> data;
        int height = 0;
        int width = 0;
    };

    void generateInstanceFile(const std::string &text, const std::string &savePath) const;
    ImageResult generateInstanceExplicit(const std::string &text) const;

    std::string makeJsonRecord(const std::string &relPath,
                               const std::string &text,
                               int width,
                               int height) const;

    const json &getConfig() const { return config_; }
    const std::vector<SharedFontMeta> &getFontMeta() const { return defaultMeta_; }
    std::shared_ptr<MultiFontBitmap<256>> getMultiFontBitmap() const { return multiFontBitmap_; }
    GlyphCache &getGlyphCache() const { return *glyphCache_; }
    BackgroundResources &getBackgroundResources() const { return *bgResources_; }

private:
    json config_;
    std::vector<SharedFontMeta> defaultMeta_;
    std::shared_ptr<MultiFontBitmap<256>> multiFontBitmap_;
    std::unique_ptr<GlyphCache> glyphCache_;
    std::unique_ptr<BackgroundResources> bgResources_;

    void initResources();
};