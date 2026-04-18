#include "backgrounds/background_resources.hpp"

#include <filesystem>

#include "utils/utils.hpp"

namespace fs = std::filesystem;

std::vector<std::string> BackgroundResources::listBgFiles(const std::string &dir)
{
    std::vector<std::string> files;
    for (auto &entry : fs::directory_iterator(dir))
    {
        if (entry.is_regular_file() && isImageFile(entry.path().string()))
        {
            files.push_back(entry.path().string());
        }
    }
    return files;
}