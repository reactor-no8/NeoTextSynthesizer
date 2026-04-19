#include "text_synth/text_synthesizer.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "backgrounds/background_resources.hpp"
#include "text_synth/renderer.hpp"
#include "utils/utils.hpp"
#include "utils/yaml_utils.hpp"

namespace fs = std::filesystem;

namespace
{
std::string escapeJsonString(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += static_cast<char>(c);
            break;
        }
    }
    return out;
}
}

SingleLineTextSynthesizer::SingleLineTextSynthesizer(const std::string &configStr)
    : glyphCache_(std::make_unique<GlyphCache>()),
      bgResources_(std::make_unique<BackgroundResources>())
{
    config_ = yaml_utils::yamlStringToJson(configStr);
    initResources();
}

SingleLineTextSynthesizer::~SingleLineTextSynthesizer() = default;

void SingleLineTextSynthesizer::initResources()
{
    const json &textCfg = config_["text_sampler"];
    const json &bgCfg = config_["bg_sampler"];
    const int fontSize = textCfg.value("font_size", 55);

    FT_Library tempFtLib;
    if (FT_Init_FreeType(&tempFtLib))
    {
        throw std::runtime_error("Failed to init FreeType");
    }

    std::vector<std::string> fontList;
    if (textCfg.contains("font_list"))
    {
        for (const auto &item : textCfg["font_list"])
        {
            fontList.push_back(item.get<std::string>());
        }
    }

    for (const auto &fontPath : fontList)
    {
        if (fs::is_directory(fontPath))
        {
            auto metas = SingleLineRender::buildSharedFontMeta(fontPath, fontSize, tempFtLib, defaultMeta_.size());
            defaultMeta_.insert(defaultMeta_.end(), std::make_move_iterator(metas.begin()), std::make_move_iterator(metas.end()));
        }
    }

    if (defaultMeta_.empty())
    {
        throw std::runtime_error("No valid fonts found.");
    }

    std::string sampleStrategy = textCfg.value("sample_strategy", "font-first");
    if (sampleStrategy == "sample-first" || sampleStrategy == "auto-fallback")
    {
        multiFontBitmap_ = std::make_shared<MultiFontBitmap<256>>();
        for (size_t i = 0; i < defaultMeta_.size(); ++i)
        {
            auto cps = defaultMeta_[i].bitmap->get_all_codepoints();
            for (uint32_t cp : cps)
            {
                multiFontBitmap_->set(cp, i);
            }
        }
    }

    FT_Done_FreeType(tempFtLib);

    std::vector<std::string> bgList;
    if (bgCfg.contains("bg_list"))
    {
        for (const auto &item : bgCfg["bg_list"])
        {
            bgList.push_back(item.get<std::string>());
        }
    }

    for (const auto &bgPath : bgList)
    {
        if (fs::is_directory(bgPath))
        {
            auto files = BackgroundResources::listBgFiles(bgPath);
            bgResources_->bgFiles.insert(bgResources_->bgFiles.end(), files.begin(), files.end());
        }
        else if (fs::is_regular_file(bgPath))
        {
            bgResources_->bgFiles.push_back(bgPath);
        }
    }
}

SingleLineImageResult SingleLineTextSynthesizer::generateSingleImage(const std::string &text) const
{
    static thread_local std::unique_ptr<SingleLineRender> threadLocalRenderer = nullptr;
    
    if (!threadLocalRenderer) {
        threadLocalRenderer = std::make_unique<SingleLineRender>(
            config_, defaultMeta_, multiFontBitmap_, *glyphCache_, *bgResources_
        );
    }

    const json &genCfg = config_["generate"];
    const json &textCfg = config_["text_sampler"];
    const json &pasteCfg = config_["post_process"]["text_paste"];

    const int outputHeight = genCfg["output_height"].get<int>();
    const int fontSize = textCfg.value("font_size", 55);
    const double scaleMin = pasteCfg.contains("scale_range") ? pasteCfg["scale_range"][0].get<double>() : 1.0;
    const double scaleMax = pasteCfg.contains("scale_range") ? pasteCfg["scale_range"][1].get<double>() : 1.0;
    const bool recomputeWidth = pasteCfg.value("recompute_width", false);
    const int mMin = pasteCfg["margin_range"][0].get<int>();
    const int mMax = pasteCfg["margin_range"][1].get<int>();
    const double offsetProb = pasteCfg.value("offset_prob", 0.0);
    const int hOffMin = pasteCfg.contains("h_offset_range") ? pasteCfg["h_offset_range"][0].get<int>() : 0;
    const int hOffMax = pasteCfg.contains("h_offset_range") ? pasteCfg["h_offset_range"][1].get<int>() : 0;
    const int vOffMin = pasteCfg.contains("v_offset_range") ? pasteCfg["v_offset_range"][0].get<int>() : 0;
    const int vOffMax = pasteCfg.contains("v_offset_range") ? pasteCfg["v_offset_range"][1].get<int>() : 0;
    const std::string sampleStrategy = textCfg.value("sample_strategy", "font-first");

    std::string mutableText = text;
    BgInfo bgInfo = threadLocalRenderer->getRandomBgPredict();
    cv::Vec3b approxColor = threadLocalRenderer->getBgApproxColor(bgInfo);
    const double verticalProb = textCfg.value("vertical_prob", 0.0);
    const bool vertical = randDouble(0, 1) < verticalProb;
    cv::Mat textImg = threadLocalRenderer->renderTightText(
        mutableText,
        approxColor,
        sampleStrategy,
        vertical ? TextDirection::Vertical : TextDirection::Horizontal);
    if (textImg.empty())
    {
        throw std::runtime_error("Failed to render text.");
    }

    if (vertical)
    {
        cv::rotate(textImg, textImg, cv::ROTATE_90_COUNTERCLOCKWISE);
    }

    int origW = textImg.cols;
    int origH = textImg.rows;
    double fontScale = static_cast<double>(origH) / fontSize;

    double scale = randDouble(scaleMin, scaleMax);
    if (std::abs(scale - 1.0) > 1e-4)
    {
        int nw = static_cast<int>(origW * scale);
        int nh = static_cast<int>(origH * scale);
        cv::resize(textImg, textImg, cv::Size(nw, nh), 0, 0, cv::INTER_CUBIC);
    }

    int scaledW = textImg.cols;
    int scaledH = textImg.rows;
    int baseCw = recomputeWidth ? scaledW : origW;
    int baseCh = origH;

    int marginX = static_cast<int>(randInt(mMin, mMax) * fontScale);
    int marginY = static_cast<int>(randInt(mMin, mMax) * fontScale);
    int cw = std::max(10, baseCw + marginX + marginY);
    int ch = std::max(10, baseCh);

    int drawX = marginX + (baseCw - scaledW) / 2;
    int drawY = (baseCh - scaledH) / 2;
    drawX += static_cast<int>(randInt(-5, 5) * fontScale);
    drawY += static_cast<int>(randInt(-3, 3) * fontScale);

    if (randDouble(0, 1) < offsetProb)
    {
        drawX += static_cast<int>(randInt(hOffMin, hOffMax) * fontScale);
        drawY += static_cast<int>(randInt(vOffMin, vOffMax) * fontScale);
    }

    auto [finalBg, _unused] = threadLocalRenderer->getBgCropAndColor(bgInfo, cw, ch);
    cv::Mat finalBgOut = finalBg;
    for (int r = 0; r < textImg.rows; ++r)
    {
        const cv::Vec4b *srcRow = textImg.ptr<cv::Vec4b>(r);
        int dy = drawY + r;
        if (dy < 0 || dy >= finalBgOut.rows)
        {
            continue;
        }
        cv::Vec3b *dstRow = finalBgOut.ptr<cv::Vec3b>(dy);
        for (int c = 0; c < textImg.cols; ++c)
        {
            int dx = drawX + c;
            if (dx < 0 || dx >= finalBgOut.cols)
            {
                continue;
            }
            const cv::Vec4b &src = srcRow[c];
            uint8_t alpha = src[3];
            if (alpha == 0)
            {
                continue;
            }
            cv::Vec3b &dst = dstRow[dx];
            float a = alpha / 255.0f;
            dst[0] = static_cast<uint8_t>(src[0] * a + dst[0] * (1 - a));
            dst[1] = static_cast<uint8_t>(src[1] * a + dst[1] * (1 - a));
            dst[2] = static_cast<uint8_t>(src[2] * a + dst[2] * (1 - a));
        }
    }

    int outW = static_cast<int>(static_cast<double>(finalBgOut.cols) * outputHeight / finalBgOut.rows);
    cv::Mat finalImg;
    cv::resize(finalBgOut, finalImg, cv::Size(outW, outputHeight), 0, 0, cv::INTER_CUBIC);

    SingleLineImageResult result;
    result.image = finalImg;
    result.text = mutableText;
    result.width = finalImg.cols;
    result.height = finalImg.rows;
    result.vertical = vertical;
    return result;
}

void SingleLineTextSynthesizer::generateInstanceFile(const std::string &text, const std::string &savePath) const
{
    SingleLineImageResult result = generateSingleImage(text);
    fs::path p(savePath);
    if (p.has_parent_path())
    {
        fs::create_directories(p.parent_path());
    }
    cv::imwrite(savePath, result.image);
}

SingleLineTextSynthesizer::ImageResult SingleLineTextSynthesizer::generateInstanceExplicit(const std::string &text) const
{
    SingleLineImageResult imageResult = generateSingleImage(text);
    cv::Mat rgb;
    cv::cvtColor(imageResult.image, rgb, cv::COLOR_BGR2RGB);

    ImageResult result;
    result.height = rgb.rows;
    result.width = rgb.cols;
    result.vertical = imageResult.vertical;
    result.data.assign(rgb.data, rgb.data + static_cast<size_t>(rgb.rows) * rgb.cols * 3);
    return result;
}