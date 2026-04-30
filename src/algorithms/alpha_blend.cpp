#include "algorithms/alpha_blend.hpp"
#include "algorithms/glyph_cache.hpp"

#include <algorithm>
#include <cmath>

namespace alpha_blend
{

void blendAlphaMaskOnto(const cv::Mat &alphaMask, cv::Mat &backgroundImage, int x, int y, const cv::Vec3b &color) {
    cv::Rect bgRect(0, 0, backgroundImage.cols, backgroundImage.rows);
    cv::Rect maskRect(x, y, alphaMask.cols, alphaMask.rows);
    cv::Rect intersect = bgRect & maskRect;

    if (intersect.area() <= 0) return;

    // Calculate starting points in the alpha mask and background image
    int maskStartX = intersect.x - x;
    int maskStartY = intersect.y - y;

    uint16_t b_src = color[0], g_src = color[1], r_src = color[2];

    for (int i = 0; i < intersect.height; ++i) {
        const uint8_t* maskPtr = alphaMask.ptr<uint8_t>(maskStartY + i) + maskStartX;
        uint8_t* dstPtr = backgroundImage.ptr<uint8_t>(intersect.y + i) + intersect.x * 3;

        #pragma omp simd
        for (int j = 0; j < intersect.width; ++j) {
            uint16_t alpha = maskPtr[j];
            uint16_t invAlpha = 255 - alpha;
            dstPtr[0] = (uint8_t)(((color[0] * alpha) + (dstPtr[0] * invAlpha) + 127) / 255);
            dstPtr[1] = (uint8_t)(((color[1] * alpha) + (dstPtr[1] * invAlpha) + 127) / 255);
            dstPtr[2] = (uint8_t)(((color[2] * alpha) + (dstPtr[2] * invAlpha) + 127) / 255);
            dstPtr += 3;
        }
    }
}

void compositeGlyph(cv::Mat &canvas, const CachedGlyph &glyph, int x, int y) {
    const int bx = x + glyph.bitmapLeft;
    const int by = y - glyph.bitmapTop;

    // Crop
    cv::Rect canvasRect(0, 0, canvas.cols, canvas.rows);
    cv::Rect glyphRect(bx, by, glyph.width, glyph.rows);
    cv::Rect intersect = canvasRect & glyphRect;

    if (intersect.area() <= 0) return;

    int srcStartX = intersect.x - bx;
    int srcStartY = intersect.y - by;
    size_t absPitch = static_cast<size_t>(std::abs(glyph.pitch));
    
    // Calculate the starting point
    const uint8_t* bufStart = (glyph.pitch < 0)
        ? glyph.buffer.data() + (glyph.rows - 1) * absPitch
        : glyph.buffer.data();
    int rowStep = (glyph.pitch < 0) ? -1 : 1;

    for (int i = 0; i < intersect.height; ++i) {
        const uint8_t* rowStart = bufStart + (srcStartY + i) * rowStep * (int)absPitch;
        const uint8_t* srcPtr = rowStart + srcStartX;

        // Blend the glyph's alpha values onto the canvas
        uint8_t* dstPtr = canvas.ptr<uint8_t>(intersect.y + i) + intersect.x;
        #pragma omp simd
        for (int j = 0; j < intersect.width; ++j)
        {
            dstPtr[j] = std::max(dstPtr[j], srcPtr[j]);
        }
    }
}

}