#pragma once

#include <mutex>
#include <opencv2/opencv.hpp>
#include <string>
#include <unordered_map>
#include <vector>

class BackgroundResources
{
public:
    std::vector<std::string> bgFiles;
    std::unordered_map<std::string, cv::Mat> bgImageCache;
    std::unordered_map<std::string, cv::Vec3b> bgApproxColorCache;
    size_t bgCacheMax = 64;
    std::mutex bgCacheMutex;

    static std::vector<std::string> listBgFiles(const std::string &dir);
};