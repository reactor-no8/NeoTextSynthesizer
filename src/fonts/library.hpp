#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

#include "algorithms/paged_bitmap.hpp"

namespace fs = std::filesystem;

struct SharedFontMeta
{
    std::vector<uint8_t> cmap;
    std::shared_ptr<SingleFontBitmap> bitmap;
    int fontSize = 0;
    size_t index = 0;
};

class FontLibrary
{
public:
    FontLibrary();
    ~FontLibrary();

    FontLibrary(const FontLibrary &) = delete;
    FontLibrary &operator=(const FontLibrary &) = delete;

    FontLibrary(FontLibrary &&other) noexcept;
    FontLibrary &operator=(FontLibrary &&other) noexcept;

    FT_Library get() const { return library_; }

private:
    FT_Library library_ = nullptr;
};