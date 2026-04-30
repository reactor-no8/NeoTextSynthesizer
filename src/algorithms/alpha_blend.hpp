#pragma once

#include <opencv2/opencv.hpp>

// Forward declaration
struct CachedGlyph;

namespace alpha_blend
{

// Blend a single-channel alpha mask with color onto a 3-channel background image
void blendAlphaMaskOnto(const cv::Mat &alphaMask, cv::Mat &backgroundImage, int x, int y, const cv::Vec3b &color);

// Alpha blend a single glyph onto a canvas
void compositeGlyph(cv::Mat &canvas, const CachedGlyph &glyph, int x, int y);

}