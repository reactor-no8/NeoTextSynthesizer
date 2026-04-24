#include "fonts/font_resource.hpp"

#include <stdexcept>
#include <utility>

namespace
{
constexpr int kHbScale = 64;
}

FontResource::FontResource(FontLibrary &library, const SharedFontMeta& descriptor)
    : descriptor_(&descriptor)
{
    if (descriptor_->cmap.empty())
    {
        throw std::runtime_error("Font memory buffer is empty");
    }

    if (FT_New_Memory_Face(library.get(),
                           descriptor_->cmap.data(),
                           descriptor_->cmap.size(),
                           0,
                           &ftFace_))
    {
        throw std::runtime_error("Failed to create font face from memory");
    }

    FT_Set_Pixel_Sizes(ftFace_, 0, descriptor_->fontSize);

    hbFont_ = hb_ft_font_create(ftFace_, nullptr);
    if (!hbFont_)
    {
        reset();
        throw std::runtime_error("Failed to create HarfBuzz font");
    }

    hb_ft_font_set_funcs(hbFont_);
    hb_font_set_scale(
        hbFont_,
        static_cast<int>(ftFace_->size->metrics.x_scale),
        static_cast<int>(ftFace_->size->metrics.y_scale));
    hb_ft_font_changed(hbFont_);
    hb_font_set_ppem(hbFont_, static_cast<unsigned int>(descriptor_->fontSize), static_cast<unsigned int>(descriptor_->fontSize));
}

FontResource::~FontResource()
{
    reset();
}

FontResource::FontResource(FontResource &&other) noexcept
    : descriptor_(other.descriptor_),
      ftFace_(std::exchange(other.ftFace_, nullptr)),
      hbFont_(std::exchange(other.hbFont_, nullptr))
{
}

FontResource &FontResource::operator=(FontResource &&other) noexcept
{
    if (this != &other)
    {
        reset();
        descriptor_ = other.descriptor_;
        ftFace_ = std::exchange(other.ftFace_, nullptr);
        hbFont_ = std::exchange(other.hbFont_, nullptr);
    }
    return *this;
}

void FontResource::reset()
{
    if (hbFont_)
    {
        hb_font_destroy(hbFont_);
        hbFont_ = nullptr;
    }
    if (ftFace_)
    {
        FT_Done_Face(ftFace_);
        ftFace_ = nullptr;
    }
}