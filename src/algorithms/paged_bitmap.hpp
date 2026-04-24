#pragma once
#include <cstdint>
#include <vector>
#include <bitset>
#include <memory>

#include <boost/dynamic_bitset.hpp>

// Page size for the bitmap. 512 bits = 64 bytes.
constexpr size_t PAGE_SIZE = 512;
constexpr size_t NUM_PAGES = 0x110000 / PAGE_SIZE; // Unicode range up to 0x10FFFF

// Single font bitmap
class SingleFontBitmap {
public:
    SingleFontBitmap();
    ~SingleFontBitmap();

    // Set a codepoint as supported
    void set(uint32_t codepoint);

    // Check if a codepoint is supported
    bool test(uint32_t codepoint) const;

    // Get all supported codepoints
    std::vector<uint32_t> get_all_codepoints() const;

private:
    struct Page {
        std::bitset<PAGE_SIZE> bits;
    };
    std::vector<std::unique_ptr<Page>> pages_;
};

// Multi-font bitmap
class MultiFontBitmap {
public:
    explicit MultiFontBitmap(size_t num_fonts);

    void set(uint32_t codepoint, size_t font_index);

    boost::dynamic_bitset<> query(uint32_t codepoint) const;

    size_t num_fonts() const;

private:
    struct Page {
        std::vector<boost::dynamic_bitset<>> bits;
        explicit Page(size_t num_fonts, size_t page_size);
    };

    std::vector<std::unique_ptr<Page>> pages_;
    size_t num_fonts_;
};