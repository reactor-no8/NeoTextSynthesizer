#pragma once
#include <cstdint>
#include <vector>
#include <bitset>
#include <memory>

// Page size for the bitmap. 512 bits = 64 bytes.
constexpr size_t PAGE_SIZE = 512;
constexpr size_t NUM_PAGES = 0x110000 / PAGE_SIZE; // Unicode range up to 0x10FFFF

// Single font bitmap (for font-first strategy)
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

// Multi-font bitmap (for sample-first and auto-fallback strategies)
// N is the maximum number of fonts supported.
template <size_t N>
class MultiFontBitmap {
public:
    MultiFontBitmap() {
        pages_.resize(NUM_PAGES);
    }

    // Set a codepoint as supported by a specific font index
    void set(uint32_t codepoint, size_t font_index) {
        if (codepoint >= 0x110000 || font_index >= N) return;
        size_t page_idx = codepoint / PAGE_SIZE;
        size_t bit_idx = codepoint % PAGE_SIZE;

        if (!pages_[page_idx]) {
            pages_[page_idx] = std::make_unique<Page>();
        }
        pages_[page_idx]->bits[bit_idx].set(font_index);
    }

    // Get the bitset of fonts supporting a codepoint
    std::bitset<N> query(uint32_t codepoint) const {
        if (codepoint >= 0x110000) return std::bitset<N>();
        size_t page_idx = codepoint / PAGE_SIZE;
        size_t bit_idx = codepoint % PAGE_SIZE;

        if (!pages_[page_idx]) {
            return std::bitset<N>();
        }
        return pages_[page_idx]->bits[bit_idx];
    }

private:
    struct Page {
        std::bitset<N> bits[PAGE_SIZE];
    };
    std::vector<std::unique_ptr<Page>> pages_;
};