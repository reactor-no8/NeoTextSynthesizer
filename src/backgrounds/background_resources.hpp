#pragma once

#include <mutex>
#include <opencv2/opencv.hpp>
#include <string>
#include <unordered_map>
#include <vector>

class BackgroundResources
{
public:
    void addToList(const std::string &path);
    bool isEmpty() const { return bgFiles_.empty(); }
    cv::Mat getRandomBackground() const;

    const std::vector<std::string> &getFiles() const { return bgFiles_; }

private:
    std::vector<std::string> bgFiles_;
    mutable std::unordered_map<std::string, cv::Mat> bgImageCache_;
    mutable size_t bgCacheMax_ = 64;
    mutable std::mutex bgCacheMutex_;
};