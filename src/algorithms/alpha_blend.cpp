#include "algorithms/alpha_blend.hpp"
#include "algorithms/glyph_cache.hpp"

#include <algorithm>
#include <cmath>

namespace alpha_blend
{

void blendAlphaMaskOnto(const cv::Mat &alphaMask, cv::Mat &backgroundImage, int x, int y, const cv::Vec3b &color)
{
    if (alphaMask.empty() || backgroundImage.empty())
        return;

    // alphaMask must be single-channel
    if (alphaMask.channels() != 1)
        return;

    if (backgroundImage.channels() != 3)
    {
        if (backgroundImage.channels() == 1)
        {
            cv::cvtColor(backgroundImage, backgroundImage, cv::COLOR_GRAY2BGR);
        }
        else if (backgroundImage.channels() == 4)
        {
            cv::cvtColor(backgroundImage, backgroundImage, cv::COLOR_BGRA2BGR);
        }
    }

    for (int row = 0; row < alphaMask.rows; ++row)
    {
        const int targetY = y + row;
        if (targetY < 0 || targetY >= backgroundImage.rows)
            continue;

        for (int col = 0; col < alphaMask.cols; ++col)
        {
            const int targetX = x + col;
            if (targetX < 0 || targetX >= backgroundImage.cols)
                continue;

            const uint8_t srcAlpha = alphaMask.at<uint8_t>(row, col);

            // Skip fully transparent pixels
            if (srcAlpha == 0)
                continue;

            // Alpha blend the color onto the background
            const float alpha = srcAlpha / 255.0f;
            cv::Vec3b &dst = backgroundImage.at<cv::Vec3b>(targetY, targetX);
            dst[0] = static_cast<uint8_t>(color[0] * alpha + dst[0] * (1.0f - alpha));
            dst[1] = static_cast<uint8_t>(color[1] * alpha + dst[1] * (1.0f - alpha));
            dst[2] = static_cast<uint8_t>(color[2] * alpha + dst[2] * (1.0f - alpha));
        }
    }
}

void compositeGlyph(cv::Mat &canvas, const CachedGlyph &glyph, int x, int y)
{
    const int bx = x + glyph.bitmapLeft;
    const int by = y - glyph.bitmapTop;

    for (int row = 0; row < glyph.rows; ++row)
    {
        const int cy = by + row;
        if (cy < 0 || cy >= canvas.rows)
            continue;

        for (int col = 0; col < glyph.width; ++col)
        {
            const int cx = bx + col;
            if (cx < 0 || cx >= canvas.cols)
                continue;

            const uint8_t alpha = glyph.buffer[static_cast<size_t>(row) * std::abs(glyph.pitch) + col];
            if (alpha == 0)
                continue;

            uint8_t &dst = canvas.at<uint8_t>(cy, cx);
            // Take the maximum alpha
            dst = std::max(dst, alpha);
        }
    }
}

}