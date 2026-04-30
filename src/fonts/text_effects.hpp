#pragma once

#include <opencv2/opencv.hpp>

// Forward declaration
struct CachedGlyph;

namespace text_effects
{

// Apply italic (shear) effect to a single glyph on the alpha mask.
// The glyph is first rendered to a temporary canvas, then sheared.
void applyItalic(cv::Mat &alphaMask, const CachedGlyph &glyph,
                 int drawX, int drawY, int advanceX, int fontSize);

// Draw an underline below a glyph on the alpha mask.
// x/y are the baseline position (same as glyph drawX/drawY).
void applyUnderline(cv::Mat &alphaMask, int x, int y, int width, int fontSize);

// Draw a strikethrough line over a glyph on the alpha mask.
// x/y are the baseline position (same as glyph drawX/drawY).
void applyStrikethrough(cv::Mat &alphaMask, int x, int y, int width, int fontSize);

}