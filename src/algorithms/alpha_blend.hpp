#pragma once

#include <opencv2/opencv.hpp>

// Forward declaration
struct CachedGlyph;

namespace alpha_blend
{

// Apply alpha blending of a text image (with alpha channel) onto a background image
void blendImageOnto(const cv::Mat &textImage, cv::Mat &backgroundImage, int x, int y);

// Apply a solid color to an alpha mask and return an RGBA image
cv::Mat applyColorToAlphaMask(const cv::Mat &alphaMask, const cv::Vec3b &color);

// Alpha blend a single glyph onto a canvas
void compositeGlyph(cv::Mat &canvas, const CachedGlyph &glyph, int x, int y, const cv::Vec4b &color);

}