#pragma once

#include <memory>
#include <string>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

#include "algorithms/paged_bitmap.hpp"
#include "fonts/library.hpp"


class FontResource
{
public:
    FontResource() = default;
    FontResource(FontLibrary &library, const SharedFontMeta& descriptor);

    ~FontResource();

    FontResource(const FontResource &) = delete;
    FontResource &operator=(const FontResource &) = delete;

    FontResource(FontResource &&other) noexcept;
    FontResource &operator=(FontResource &&other) noexcept;

    bool isValid() const { return ftFace_ != nullptr && hbFont_ != nullptr; }
    FT_Face getFTFace() const { return ftFace_; }
    hb_font_t *getHBFont() const { return hbFont_; }
    const SharedFontMeta &getDescriptor() const { return *descriptor_; }

private:
    const SharedFontMeta* descriptor_ = nullptr;
    FT_Face ftFace_ = nullptr;
    hb_font_t *hbFont_ = nullptr;

    void reset();
};