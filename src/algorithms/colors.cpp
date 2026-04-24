#include "algorithms/colors.hpp"

namespace colors
{

cv::Vec3b getContrastiveColor(const cv::Vec3b &bgColor)
{
    // Convert from BGR to RGB
    double r = bgColor[2] / 255.0;
    double g = bgColor[1] / 255.0;
    double b = bgColor[0] / 255.0;
    
    // Convert RGB to HSV
    double max = std::max(std::max(r, g), b);
    double min = std::min(std::min(r, g), b);
    double delta = max - min;
    
    double h = 0;
    if (delta > 0) {
        if (max == r) {
            h = 60 * fmod(((g - b) / delta), 6);
        } else if (max == g) {
            h = 60 * (((b - r) / delta) + 2);
        } else {
            h = 60 * (((r - g) / delta) + 4);
        }
    }
    if (h < 0) h += 360;
    h /= 360.0; // Normalize to [0, 1]
    
    double s = max == 0 ? 0 : delta / max;
    double v = max;
    
    // Determine new HSV values for contrast
    double newV, newS, newH;
    if (std::rand() % 2 == 0) {
        newV = (std::abs(v - 0.5) < 0.2) ? (1.0 - v) : (v > 0.5 ? 0.1 : 0.9);
        newS = s * (std::rand() / static_cast<double>(RAND_MAX)) * 0.5;
        newH = h;
    } else {
        newV = (v > 0.5) ? (1.0 - v) : 0.8;
        newS = 0.5 + (std::rand() / static_cast<double>(RAND_MAX)) * 0.5;
        newH = fmod(h + 0.5, 1.0);
    }
    
    // 50% pure black or white
    if (std::rand() % 2 == 0) {
        newS = 0.0;
        newV = (v > 0.5) ? 0.0 : 1.0;
    }
    
    // Convert back to RGB
    double c = newV * newS;
    double x = c * (1 - std::abs(fmod(newH * 6, 2) - 1));
    double m = newV - c;
    
    double nr, ng, nb;
    if (newH < 1.0/6.0) {
        nr = c; ng = x; nb = 0;
    } else if (newH < 2.0/6.0) {
        nr = x; ng = c; nb = 0;
    } else if (newH < 3.0/6.0) {
        nr = 0; ng = c; nb = x;
    } else if (newH < 4.0/6.0) {
        nr = 0; ng = x; nb = c;
    } else if (newH < 5.0/6.0) {
        nr = x; ng = 0; nb = c;
    } else {
        nr = c; ng = 0; nb = x;
    }
    
    nr = (nr + m) * 255;
    ng = (ng + m) * 255;
    nb = (nb + m) * 255;
    
    // Return in BGR format
    return cv::Vec3b(static_cast<uchar>(nb), static_cast<uchar>(ng), static_cast<uchar>(nr));
}

}