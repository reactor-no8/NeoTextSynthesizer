#pragma once

#include <opencv2/opencv.hpp>

class BackgroundSampler
{
public:
    // Get a random crop from the background that matches the required dimensions
    // If background dimensions are smaller, it will stretch to fit
    // If larger, it will randomly crop a section of the required size
    static cv::Mat getRandomCropBackground(const cv::Mat &bgOrigin, int height, int width);
};