#include "algorithms/alpha_blend.hpp"
#include "algorithms/glyph_cache.hpp"

#include <algorithm>
#include <cmath>

namespace alpha_blend
{

void blendImageOnto(const cv::Mat &textImage, cv::Mat &backgroundImage, int x, int y)
{
    if (textImage.empty() || backgroundImage.empty())
        return;
    
    if (textImage.channels() != 4)
        return;
    
    cv::Mat bgWorkingCopy = backgroundImage;
    if (bgWorkingCopy.channels() == 1) {
        cv::cvtColor(bgWorkingCopy, bgWorkingCopy, cv::COLOR_GRAY2BGR);
    }
    
    for (int row = 0; row < textImage.rows; ++row)
    {
        const int targetY = y + row;
        if (targetY < 0 || targetY >= bgWorkingCopy.rows)
            continue;
            
        for (int col = 0; col < textImage.cols; ++col)
        {
            const int targetX = x + col;
            if (targetX < 0 || targetX >= bgWorkingCopy.cols)
                continue;
                
            const cv::Vec4b &src = textImage.at<cv::Vec4b>(row, col);
            
            // Skip fully transparent pixels
            if (src[3] == 0)
                continue;
                
            // alpha blend
            const float alpha = src[3] / 255.0f;
            
            if (bgWorkingCopy.channels() == 3) {
                cv::Vec3b &dst = bgWorkingCopy.at<cv::Vec3b>(targetY, targetX);
                dst[0] = static_cast<uchar>(src[0] * alpha + dst[0] * (1.0f - alpha));
                dst[1] = static_cast<uchar>(src[1] * alpha + dst[1] * (1.0f - alpha));
                dst[2] = static_cast<uchar>(src[2] * alpha + dst[2] * (1.0f - alpha));
            } else if (bgWorkingCopy.channels() == 4) {
                cv::Vec4b &dst = bgWorkingCopy.at<cv::Vec4b>(targetY, targetX);
                dst[0] = static_cast<uchar>(src[0] * alpha + dst[0] * (1.0f - alpha));
                dst[1] = static_cast<uchar>(src[1] * alpha + dst[1] * (1.0f - alpha));
                dst[2] = static_cast<uchar>(src[2] * alpha + dst[2] * (1.0f - alpha));
                dst[3] = static_cast<uchar>(255);  // Fully opaque
            }
        }
    }
    
    // If converted the background from grayscale, convert back
    if (backgroundImage.channels() == 1 && bgWorkingCopy.channels() != 1) {
        cv::cvtColor(bgWorkingCopy, backgroundImage, cv::COLOR_BGR2GRAY);
    } else if (bgWorkingCopy.data != backgroundImage.data) {
        bgWorkingCopy.copyTo(backgroundImage);
    }
}

cv::Mat applyColorToAlphaMask(const cv::Mat &alphaMask, const cv::Vec3b &color)
{
    if (alphaMask.empty())
        return cv::Mat();
        
    if (alphaMask.channels() != 4)
        return cv::Mat();
        
    cv::Mat result(alphaMask.rows, alphaMask.cols, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    
    for (int row = 0; row < alphaMask.rows; ++row)
    {
        for (int col = 0; col < alphaMask.cols; ++col)
        {
            const cv::Vec4b &src = alphaMask.at<cv::Vec4b>(row, col);
            
            // Skip fully transparent pixels
            if (src[3] == 0)
                continue;
                
            cv::Vec4b &dst = result.at<cv::Vec4b>(row, col);
            dst[0] = color[0]; // B
            dst[1] = color[1]; // G
            dst[2] = color[2]; // R
            dst[3] = src[3];   // A
        }
    }
    
    return result;
}

void compositeGlyph(cv::Mat &canvas, const CachedGlyph &glyph, int x, int y, const cv::Vec4b &color)
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
                
            auto &dst = canvas.at<cv::Vec4b>(cy, cx);
            const float a = alpha / 255.0f;
            dst[0] = static_cast<uint8_t>(color[0] * a + dst[0] * (1.0f - a));
            dst[1] = static_cast<uint8_t>(color[1] * a + dst[1] * (1.0f - a));
            dst[2] = static_cast<uint8_t>(color[2] * a + dst[2] * (1.0f - a));
            dst[3] = std::max(dst[3], alpha);
        }
    }
}

}