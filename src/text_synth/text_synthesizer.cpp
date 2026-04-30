#include "text_synth/text_synthesizer.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "algorithms/colors.hpp"
#include "algorithms/alpha_blend.hpp"
#include "text_synth/renderer.hpp"
#include "algorithms/glyph_cache.hpp"
#include "algorithms/transforms.hpp"
#include "backgrounds/background_resources.hpp"
#include "backgrounds/background_sampler.hpp"
#include "utils/utils.hpp"
#include "utils/yaml_utils.hpp"

namespace fs = std::filesystem;

SingleLineTextSynthesizer::SingleLineTextSynthesizer(const json &config)
    : config_(config)
{
}

SingleLineTextSynthesizer::~SingleLineTextSynthesizer() = default;

SingleLineImageResult SingleLineTextSynthesizer::generateSingleImage(
    const std::string &text,
    SingleLineRenderer &renderer,
    FontSelector &fontSelector,
    BackgroundResources &bgResources) const
{
    const json &genCfg = config_["generate"];
    const json &textCfg = config_["text_sampler"];
    const json &bgCfg = config_["bg_sampler"];
    const json &pasteCfg = config_["post_process"]["text_paste"];
    const json &transformCfg = config_["post_process"]["transforms"];

    const int outputHeight = genCfg["output_height"].get<int>();
    const int fontSize = textCfg.value("font_size", 55);
    const double scaleMin = pasteCfg.contains("scale_range") ? pasteCfg["scale_range"][0].get<double>() : 1.0;
    const double scaleMax = pasteCfg.contains("scale_range") ? pasteCfg["scale_range"][1].get<double>() : 1.0;
    const int mMin = pasteCfg["margin_range"][0].get<int>();
    const int mMax = pasteCfg["margin_range"][1].get<int>();
    const double offsetProb = pasteCfg.value("offset_prob", 0.0);
    const int hOffMin = pasteCfg.contains("h_offset_range") ? pasteCfg["h_offset_range"][0].get<int>() : 0;
    const int hOffMax = pasteCfg.contains("h_offset_range") ? pasteCfg["h_offset_range"][1].get<int>() : 0;
    const int vOffMin = pasteCfg.contains("v_offset_range") ? pasteCfg["v_offset_range"][0].get<int>() : 0;
    const int vOffMax = pasteCfg.contains("v_offset_range") ? pasteCfg["v_offset_range"][1].get<int>() : 0;
    const double rotationProb = transformCfg.value("rotation_prob", 0.0);
    const int rotationMax = transformCfg.contains("rotation_range") ? transformCfg["rotation_range"][1].get<int>() : 0;
    const int rotationMin = transformCfg.contains("rotation_range") ? transformCfg["rotation_range"][0].get<int>() : 0;
    const double distortionProb = transformCfg.value("distortion_prob", 0.0);
    const double distortionLevel = transformCfg.value("distortion_level", 0.0);
    const double verticalProb = textCfg.value("vertical_prob", 0.0);
    const double bgImageProb = bgCfg.value("bg_image_prob", 0.3);
    const double grayBgProb = bgCfg.value("gray_bg_prob", 0.7);
    const bool vertical = randDouble(0, 1) < verticalProb;

    // Select a font
    std::string mutableText;
    size_t fontIndex = fontSelector.selectFont(text, mutableText);
    
    // Render text as alpha mask
    cv::Mat alphaMask = renderer.renderTightText(
        mutableText,
        fontIndex,
        vertical ? TextDirection::Vertical : TextDirection::Horizontal);
    
    if (alphaMask.empty())
    {
        throw std::runtime_error("Failed to render text.");
    }

    if (vertical)
    {
        cv::rotate(alphaMask, alphaMask, cv::ROTATE_90_COUNTERCLOCKWISE);
    }

    // Sample background based on configuration
    cv::Mat backgroundImg;
    cv::Vec3b textColor;
    
    // Get background and determine text color based on configuration
    if (randDouble(0, 1) < bgImageProb && !bgResources.isEmpty())
    {
        // Use a real background image
        backgroundImg = bgResources.getRandomBackground();
        
        // Determine text color based on configuration or contrast
        if (bgCfg.contains("text_color"))
        {
            if (bgCfg["text_color"].is_string())
            {
                const std::string colorMode = bgCfg["text_color"];
                if (colorMode == "auto")
                {
                    // Calculate the average color of the background for contrast
                    cv::Scalar avgColor = cv::mean(backgroundImg);
                    cv::Vec3b bgColor(avgColor[0], avgColor[1], avgColor[2]);
                    textColor = colors::getContrastiveColor(bgColor);
                }
                else if (isValidHexColor(colorMode))
                {
                    textColor = parseHexColor(colorMode);
                }
                else
                {
                    // Default to black if invalid
                    textColor = cv::Vec3b(0, 0, 0);
                }
            }
            else if (bgCfg["text_color"].is_array() && bgCfg["text_color"].size() == 2)
            {
                // Use a random color from the range
                std::string color1 = bgCfg["text_color"][0];
                std::string color2 = bgCfg["text_color"][1];
                if (isValidHexColor(color1) && isValidHexColor(color2))
                {
                    textColor = randomColorInRange(color1, color2);
                }
                else
                {
                    textColor = cv::Vec3b(0, 0, 0); // Default to black if invalid
                }
            }
            else
            {
                // Default to auto contrast
                cv::Scalar avgColor = cv::mean(backgroundImg);
                cv::Vec3b bgColor(avgColor[0], avgColor[1], avgColor[2]);
                textColor = colors::getContrastiveColor(bgColor);
            }
        }
        else
        {
            // Default to auto contrast if not specified
            cv::Scalar avgColor = cv::mean(backgroundImg);
            cv::Vec3b bgColor(avgColor[0], avgColor[1], avgColor[2]);
            textColor = colors::getContrastiveColor(bgColor);
        }
    }
    else
    {
        // Generate a solid color background
        cv::Vec3b bgColor;
        
        if (bgCfg.contains("bg_color"))
        {
            if (bgCfg["bg_color"].is_string())
            {
                const std::string colorMode = bgCfg["bg_color"];
                if (colorMode == "auto")
                {
                    // Use gray or random light color
                    if (randDouble(0, 1) < grayBgProb)
                    {
                        int gray = randInt(180, 250);
                        bgColor = cv::Vec3b(gray, gray, gray);
                    }
                    else
                    {
                        int r = randInt(180, 250);
                        int g = randInt(180, 250);
                        int b = randInt(180, 250);
                        bgColor = cv::Vec3b(b, g, r); // BGR order for OpenCV
                    }
                }
                else if (isValidHexColor(colorMode))
                {
                    bgColor = parseHexColor(colorMode);
                }
                else
                {
                    // Default to white if invalid
                    bgColor = cv::Vec3b(255, 255, 255);
                }
            }
            else if (bgCfg["bg_color"].is_array() && bgCfg["bg_color"].size() == 2)
            {
                // Use a random color from the range
                std::string color1 = bgCfg["bg_color"][0];
                std::string color2 = bgCfg["bg_color"][1];
                if (isValidHexColor(color1) && isValidHexColor(color2))
                {
                    bgColor = randomColorInRange(color1, color2);
                }
                else
                {
                    bgColor = cv::Vec3b(255, 255, 255); // Default to white if invalid
                }
            }
            else
            {
                // Default to white
                bgColor = cv::Vec3b(255, 255, 255);
            }
        }
        else
        {
            // Default background color logic if not specified
            if (randDouble(0, 1) < grayBgProb)
            {
                int gray = randInt(180, 250);
                bgColor = cv::Vec3b(gray, gray, gray);
            }
            else
            {
                int r = randInt(180, 250);
                int g = randInt(180, 250);
                int b = randInt(180, 250);
                bgColor = cv::Vec3b(b, g, r); // BGR order for OpenCV
            }
        }
        
        backgroundImg = cv::Mat(alphaMask.rows, alphaMask.cols, CV_8UC3, bgColor);
        
        // Determine text color based on configuration or contrast with background color
        if (bgCfg.contains("text_color"))
        {
            if (bgCfg["text_color"].is_string())
            {
                const std::string colorMode = bgCfg["text_color"];
                if (colorMode == "auto")
                {
                    textColor = colors::getContrastiveColor(bgColor);
                }
                else if (isValidHexColor(colorMode))
                {
                    textColor = parseHexColor(colorMode);
                }
                else
                {
                    textColor = cv::Vec3b(0, 0, 0); // Default to black if invalid
                }
            }
            else if (bgCfg["text_color"].is_array() && bgCfg["text_color"].size() == 2)
            {
                // Use a random color from the range
                std::string color1 = bgCfg["text_color"][0];
                std::string color2 = bgCfg["text_color"][1];
                if (isValidHexColor(color1) && isValidHexColor(color2))
                {
                    textColor = randomColorInRange(color1, color2);
                }
                else
                {
                    textColor = cv::Vec3b(0, 0, 0); // Default to black if invalid
                }
            }
            else
            {
                // Default to auto contrast
                textColor = colors::getContrastiveColor(bgColor);
            }
        }
        else
        {
            // Default to auto contrast if not specified
            textColor = colors::getContrastiveColor(bgColor);
        }
    }
    
    // Resize background to match the alpha mask if needed
    if (backgroundImg.rows != alphaMask.rows || backgroundImg.cols != alphaMask.cols)
    {
        backgroundImg = BackgroundSampler::getRandomCropBackground(backgroundImg, alphaMask.rows, alphaMask.cols);
    }

    int origW = alphaMask.cols;
    int origH = alphaMask.rows;
    double fontScale = static_cast<double>(origH) / fontSize;

    double scale = randDouble(scaleMin, scaleMax);
    if (std::abs(scale - 1.0) > 1e-4)
    {
        int nw = static_cast<int>(origW * scale);
        int nh = static_cast<int>(origH * scale);
        cv::resize(alphaMask, alphaMask, cv::Size(nw, nh), 0, 0, cv::INTER_CUBIC);
    }

    if (randDouble(0, 1) < rotationProb)
    {
        alphaMask = geometric_transforms::applyAffineTransform(alphaMask, rotationMax, rotationMin);
    }
    if (randDouble(0, 1) < distortionProb)
    {
        alphaMask = geometric_transforms::applyPerspectiveTransform(alphaMask, distortionLevel);
    }

    int scaledW = alphaMask.cols;
    int scaledH = alphaMask.rows;
    int baseCw = scaledW;
    int baseCh = scaledH;

    // Determine margins and drawing position
    int marginX = static_cast<int>(randInt(mMin, mMax) * fontScale);
    int marginY = static_cast<int>(randInt(mMin, mMax) * fontScale);
    int cw = std::max(10, baseCw + marginX + marginY);
    int ch = std::max(10, baseCh);
    
    // Resize background to proper size with margins
    cv::resize(backgroundImg, backgroundImg, cv::Size(cw, ch));

    int drawX = marginX + (baseCw - scaledW) / 2;
    int drawY = (baseCh - scaledH) / 2;
    drawX += static_cast<int>(randInt(-5, 5) * fontScale);
    drawY += static_cast<int>(randInt(-3, 3) * fontScale);

    if (randDouble(0, 1) < offsetProb)
    {
        drawX += static_cast<int>(randInt(hOffMin, hOffMax) * fontScale);
        drawY += static_cast<int>(randInt(vOffMin, vOffMax) * fontScale);
    }

    if (backgroundImg.channels() != 3)
    {
        if (backgroundImg.channels() == 1)
            cv::cvtColor(backgroundImg, backgroundImg, cv::COLOR_GRAY2BGR);
        else if (backgroundImg.channels() == 4)
            cv::cvtColor(backgroundImg, backgroundImg, cv::COLOR_BGRA2BGR);
    }
    // Blend alpha mask with text color onto background
    alpha_blend::blendAlphaMaskOnto(alphaMask, backgroundImg, drawX, drawY, textColor);
    
    // Resize to output height
    int outW = static_cast<int>(static_cast<double>(backgroundImg.cols) * outputHeight / backgroundImg.rows);
    cv::Mat finalImg;
    cv::resize(backgroundImg, finalImg, cv::Size(outW, outputHeight), 0, 0, cv::INTER_CUBIC);

    SingleLineImageResult result;
    result.image = finalImg;
    result.text = mutableText;
    result.width = finalImg.cols;
    result.height = finalImg.rows;
    result.vertical = vertical;
    return result;
}

void SingleLineTextSynthesizer::generateInstanceFile(
    const std::string &text, 
    const std::string &savePath,
    SingleLineRenderer &renderer,
    FontSelector &fontSelector,
    BackgroundResources &bgResources) const
{
    SingleLineImageResult result = generateSingleImage(text, renderer, fontSelector, bgResources);
    fs::path p(savePath);
    if (p.has_parent_path())
    {
        fs::create_directories(p.parent_path());
    }
    cv::imwrite(savePath, result.image);
}

SingleLineTextSynthesizer::ImageResult SingleLineTextSynthesizer::generateInstanceExplicit(
    const std::string &text,
    SingleLineRenderer &renderer,
    FontSelector &fontSelector,
    BackgroundResources &bgResources) const
{
    SingleLineImageResult imageResult = generateSingleImage(text, renderer, fontSelector, bgResources);
    cv::Mat rgb;
    cv::cvtColor(imageResult.image, rgb, cv::COLOR_BGR2RGB);

    ImageResult result;
    result.height = rgb.rows;
    result.width = rgb.cols;
    result.vertical = imageResult.vertical;
    result.data.assign(rgb.data, rgb.data + static_cast<size_t>(rgb.rows) * rgb.cols * 3);
    return result;
}