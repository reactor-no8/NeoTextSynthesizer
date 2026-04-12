#pragma once
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <nlohmann/json.hpp>
#include "glyph_cache.hpp"
#include "renderer.hpp"

using json = nlohmann::json;

class NeoTextSynthesizer
{
public:
    // Construct from a YAML/JSON string.
    // The string is parsed as YAML (which is a superset of JSON).
    explicit NeoTextSynthesizer(const std::string &configStr);

    ~NeoTextSynthesizer();

    // Non-copyable
    NeoTextSynthesizer(const NeoTextSynthesizer &) = delete;
    NeoTextSynthesizer &operator=(const NeoTextSynthesizer &) = delete;

    // total: number of images to generate
    // workers: number of threads (0 = auto-detect)
    // showProgress: whether to display progress bar
    void generate(int total, int workers = 0, bool showProgress = true);

    // Generate a single instance and save to file
    void generateInstanceFile(const std::string &text, const std::string &savePath);

    // Generate a single instance and return RGB pixel data
    struct ImageResult
    {
        std::vector<uint8_t> data; // RGB interleaved, row-major
        int height;
        int width;
    };
    ImageResult generateInstanceExplicit(const std::string &text);

    // Get the active config
    const json &getConfig() const { return config_; }

private:
    json config_;

    // Shared resources (initialized once in constructor)
    std::vector<SharedFontMeta> defaultMeta_;
    std::shared_ptr<MultiFontBitmap<256>> multiFontBitmap_; // For sample-first and auto-fallback
    GlyphCache glyphCache_;
    SharedBgResources bgRes_;

    // Initialize shared resources from config
    void initResources();
};