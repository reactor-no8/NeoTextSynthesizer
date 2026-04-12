#include "paged_bitmap.hpp"

SingleFontBitmap::SingleFontBitmap() {
    pages_.resize(NUM_PAGES);
}

SingleFontBitmap::~SingleFontBitmap() = default;

void SingleFontBitmap::set(uint32_t codepoint) {
    if (codepoint >= 0x110000) return;
    size_t page_idx = codepoint / PAGE_SIZE;
    size_t bit_idx = codepoint % PAGE_SIZE;

    if (!pages_[page_idx]) {
        pages_[page_idx] = std::make_unique<Page>();
    }
    pages_[page_idx]->bits.set(bit_idx);
}

bool SingleFontBitmap::test(uint32_t codepoint) const {
    if (codepoint >= 0x110000) return false;
    size_t page_idx = codepoint / PAGE_SIZE;
    size_t bit_idx = codepoint % PAGE_SIZE;

    if (!pages_[page_idx]) {
        return false;
    }
    return pages_[page_idx]->bits.test(bit_idx);
}

std::vector<uint32_t> SingleFontBitmap::get_all_codepoints() const {
        std::vector<uint32_t> res;
        for (size_t p = 0; p < pages_.size(); ++p) {
            if (pages_[p]) {
                for (size_t b = 0; b < PAGE_SIZE; ++b) {
                    if (pages_[p]->bits.test(b)) {
                        res.push_back(p * PAGE_SIZE + b);
                    }
                }
            }
        }
        return res;
}