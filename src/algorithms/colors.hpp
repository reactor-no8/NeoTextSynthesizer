#pragma once

#include <opencv2/opencv.hpp>

namespace colors
{

cv::Vec3b getContrastiveColor(const cv::Vec3b &bgColor);

}