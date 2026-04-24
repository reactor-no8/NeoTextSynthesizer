#pragma once

#include <memory>
#include <string>
#include <vector>
#include <filesystem>

#include "fonts/font_resource.hpp"
#include "fonts/library.hpp"

namespace fs = std::filesystem;

class FontSelector
{
public:
    FontSelector() = default;
    
    // Initialize FontSelector with SharedFontMeta list
    explicit FontSelector(std::vector<SharedFontMeta> fontMetas, FontLibrary& library);

    FontSelector(const FontSelector&) = delete;
    FontSelector& operator=(const FontSelector&) = delete;
    
    FontSelector(FontSelector&&) = default;
    FontSelector& operator=(FontSelector&&) = default;

    // Static method to build SharedFontMeta from file paths
    static std::vector<SharedFontMeta> buildSharedFontMeta(
        const std::vector<std::string> &paths, int fontSize);

    // Select a font for the input text, modifying the text if some characters aren't supported
    // Returns index of selected font in the fonts_ collection
    size_t selectFont(const std::string &in_text, std::string &out_text);
    
    // Get font by index
    const FontResource& getFont(size_t index) const;

    // Get all available fonts
    const std::vector<FontResource> &getFonts() const { return fonts_; }
    
    // Create a thread-local copy of FontSelector with shared multiFontBitmap
    std::vector<FontSelector> createThreadSelectors(std::vector<FontLibrary>& libraries) const;

private:
    std::vector<FontResource> fonts_;
    std::vector<SharedFontMeta> fontMetas_;
    std::string strategy_ = "font-first";
    std::shared_ptr<MultiFontBitmap> multiFontBitmap_;

    size_t selectFontFirst(const std::string &in_text, std::string &out_text);
    size_t selectSampleFirst(const std::string &in_text, std::string &out_text);
    size_t selectAutoFallback(const std::string &in_text, std::string &out_text);

    void initMultiFontBitmap();
};