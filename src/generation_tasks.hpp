#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <nlohmann/json.hpp>

#include "text_synth/text_synthesizer.hpp"
#include "algorithms/glyph_cache.hpp"
#include "backgrounds/background_resources.hpp"
#include "fonts/font_selector.hpp"

using json = nlohmann::json;

class SingleLineTextGenerator
{
public:
    explicit SingleLineTextGenerator(const std::string &configStr);

    // Returns {totalGenerated, totalErrors}.
    std::pair<int64_t, int64_t> generate(int total, int workers = 0, bool showProgress = true);
    void generateInstanceFile(const std::string &text, const std::string &savePath) const;
    SingleLineTextSynthesizer::ImageResult generateInstanceExplicit(const std::string &text) const;
    std::string getConfigJson() const;

private:
    json config_;
    SingleLineTextSynthesizer synthesizer_;
    std::unique_ptr<GlyphCache> glyphCache_;
    std::unique_ptr<BackgroundResources> bgResources_;
    std::unique_ptr<FontLibrary> globalLibrary_;
    std::vector<SharedFontMeta> fontMetas_;
    std::unique_ptr<SingleLineRenderer> renderer_;
    std::unique_ptr<FontSelector> fontSelector_;
};