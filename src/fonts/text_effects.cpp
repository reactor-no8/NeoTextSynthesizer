#include "fonts/text_effects.hpp"

#include <algorithm>
#include <cmath>

#include "algorithms/glyph_cache.hpp"

namespace text_effects
{

void applyItalic(cv::Mat &alphaMask, const CachedGlyph &glyph,
                 int drawX, int drawY, int advanceX, int fontSize)
{
    const float shearFactor = 0.3f;

    for (int r = 0; r < glyph.rows; ++r)
    {

        int relativeY = glyph.bitmapTop - r;

        int xShift = static_cast<int>(std::round(shearFactor * relativeY));

        for (int c = 0; c < glyph.width; ++c)
        {
            uint8_t src = glyph.buffer[r * std::abs(glyph.pitch) + c];
            if (src == 0) continue;

            int dx = drawX + glyph.bitmapLeft + c + xShift;
            int dy = drawY - glyph.bitmapTop + r;

            if (dx >= 0 && dx < alphaMask.cols && dy >= 0 && dy < alphaMask.rows)
            {
                auto &dst = alphaMask.at<uint8_t>(dy, dx);
                dst = std::max(dst, src);
            }
        }
    }
}

void applyUnderline(cv::Mat &alphaMask, int x, int y, int width, int fontSize)
{
    const int lineThickness = std::max(1, fontSize / 18);
    const int ly = y + std::max(1, fontSize / 10); 

    const int halfT = lineThickness / 2;
    for (int row = -halfT; row <= (lineThickness - halfT - 1); ++row)
    {
        const int curY = ly + row;
        if (curY < 0 || curY >= alphaMask.rows)
            continue;

        const int startX = std::max(0, x);
        const int endX = std::min(x + width, alphaMask.cols);

        if (startX < endX) {
            uint8_t* rowPtr = alphaMask.ptr<uint8_t>(curY);
            for (int cx = startX; cx < endX; ++cx)
            {
                rowPtr[cx] = 255;
            }
        }
    }
}

void applyStrikethrough(cv::Mat &alphaMask, int x, int y, int width, int fontSize)
{
    const int lineW = std::max(1, fontSize / 18);
    // Position at roughly the middle of the glyph area
    const int ly = y - fontSize + fontSize / 2 + 2;
    const int halfW = lineW / 2;
    for (int row = -halfW; row <= halfW; ++row)
    {
        const int curY = ly + row;
        if (curY < 0 || curY >= alphaMask.rows)
            continue;
        const int endX = std::min(x + width, alphaMask.cols);
        for (int cx = std::max(0, x); cx < endX; ++cx)
        {
            alphaMask.at<uint8_t>(curY, cx) = 255;
        }
    }
}

} // namespace text_effects