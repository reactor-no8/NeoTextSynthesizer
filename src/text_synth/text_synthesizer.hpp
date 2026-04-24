#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include "text_synth/renderer.hpp"

using json = nlohmann::json;

struct SingleLineImageResult
{
    cv::Mat image;
    std::string text;
    int width = 0;
    int height = 0;
    bool vertical = false;
};

class SingleLineTextSynthesizer
{
public:
    explicit SingleLineTextSynthesizer(const json &config);
    ~SingleLineTextSynthesizer();

    SingleLineTextSynthesizer(const SingleLineTextSynthesizer &) = delete;
    SingleLineTextSynthesizer &operator=(const SingleLineTextSynthesizer &) = delete;

    SingleLineImageResult generateSingleImage(
        const std::string &text,
        SingleLineRenderer &renderer,
        FontSelector &fontSelector,
        BackgroundResources &bgResources) const;

    struct ImageResult
    {
        std::vector<uint8_t> data;
        int height = 0;
        int width = 0;
        bool vertical = false;
    };

    void generateInstanceFile(const std::string &text, const std::string &savePath,
                             SingleLineRenderer &renderer, FontSelector &fontSelector,
                             BackgroundResources &bgResources) const;
    
    ImageResult generateInstanceExplicit(const std::string &text,
                                        SingleLineRenderer &renderer, FontSelector &fontSelector,
                                        BackgroundResources &bgResources) const;

    const json &getConfig() const { return config_; }

private:
    json config_;
};