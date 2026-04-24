#include "fonts/library.hpp"

#include <stdexcept>
#include <utility>
#include "utils/utils.hpp"

FontLibrary::FontLibrary()
{
    if (FT_Init_FreeType(&library_))
    {
        throw std::runtime_error("Failed to init FreeType");
    }
}

FontLibrary::~FontLibrary()
{
    if (library_)
    {
        FT_Done_FreeType(library_);
    }
}

FontLibrary::FontLibrary(FontLibrary &&other) noexcept
    : library_(std::exchange(other.library_, nullptr))
{
}

FontLibrary &FontLibrary::operator=(FontLibrary &&other) noexcept
{
    if (this != &other)
    {
        if (library_)
        {
            FT_Done_FreeType(library_);
        }
        library_ = std::exchange(other.library_, nullptr);
    }
    return *this;
}