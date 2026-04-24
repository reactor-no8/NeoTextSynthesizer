#include "backgrounds/background_resources.hpp"

#include <filesystem>
#include <stdexcept>

#include "utils/utils.hpp"

namespace fs = std::filesystem;

void BackgroundResources::addToList(const std::string &path)
{
    fs::path input(path);
    if (!fs::exists(input))
    {
        return;
    }

    if (fs::is_regular_file(input))
    {
        if (isImageFile(input.string()))
        {
            bgFiles_.push_back(input.string());
        }
        return;
    }

    if (!fs::is_directory(input))
    {
        return;
    }

    for (const auto &entry : fs::recursive_directory_iterator(input))
    {
        if (entry.is_regular_file() && isImageFile(entry.path().string()))
        {
            bgFiles_.push_back(entry.path().string());
        }
    }
}

cv::Mat BackgroundResources::getRandomBackground() const
{
    if (bgFiles_.empty())
    {
        return {};
    }

    const std::string &imagePath = bgFiles_[randInt(0, static_cast<int>(bgFiles_.size()) - 1)];

    {
        std::lock_guard<std::mutex> lock(bgCacheMutex_);
        auto it = bgImageCache_.find(imagePath);
        if (it != bgImageCache_.end())
        {
            return it->second.clone();
        }
    }

    cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
    if (image.empty())
    {
        return {};
    }

    {
        std::lock_guard<std::mutex> lock(bgCacheMutex_);
        if (bgImageCache_.size() >= bgCacheMax_)
        {
            bgImageCache_.clear();
        }
        bgImageCache_[imagePath] = image;
    }

    return image.clone();
}