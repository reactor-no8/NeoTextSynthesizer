#include "backgrounds/background_sampler.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

cv::Mat BackgroundSampler::getRandomCropBackground(const cv::Mat &bgOrigin, int height, int width)
{
    if (bgOrigin.empty())
    {
        return cv::Mat(height, width, CV_8UC3, cv::Scalar(255, 255, 255));
    }

    cv::Mat result;
    
    // If both dimensions are smaller, just resize
    if (bgOrigin.rows < height && bgOrigin.cols < width)
    {
        cv::resize(bgOrigin, result, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);
        return result;
    }

    if (bgOrigin.rows < height || bgOrigin.cols < width)
    {
        double scaleX = static_cast<double>(width) / bgOrigin.cols;
        double scaleY = static_cast<double>(height) / bgOrigin.rows;
        double scale = std::max(scaleX, scaleY);
        
        int newWidth = static_cast<int>(bgOrigin.cols * scale);
        int newHeight = static_cast<int>(bgOrigin.rows * scale);
        
        cv::Mat resized;
        cv::resize(bgOrigin, resized, cv::Size(newWidth, newHeight), 0, 0, cv::INTER_LINEAR);
        
        // Crop the center portion
        int startX = (newWidth - width) / 2;
        int startY = (newHeight - height) / 2;
        
        // Ensure stay within bounds
        startX = std::max(0, std::min(startX, newWidth - width));
        startY = std::max(0, std::min(startY, newHeight - height));
        
        result = resized(cv::Rect(startX, startY, width, height));
        return result;
    }
    
    // Both dimensions are larger - random crop
    int maxX = bgOrigin.cols - width;
    int maxY = bgOrigin.rows - height;
    
    int startX = randInt(0, maxX);
    int startY = randInt(0, maxY);
    
    result = bgOrigin(cv::Rect(startX, startY, width, height));
    return result;
}