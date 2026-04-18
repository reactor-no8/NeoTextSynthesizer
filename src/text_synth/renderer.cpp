#include "text_synth/renderer.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

#include "utils/utf8_helper.hpp"
#include "utils/utils.hpp"

namespace fs = std::filesystem;

namespace
{
constexpr int kBgApproxSample = 50;
}

std::vector<SharedFontMeta> SingleLineRender::buildSharedFontMeta(
    const std::string &dir, int fontSize, FT_Library ftLib, size_t indexOffset)
{
    std::vector<SharedFontMeta> out;
    for (auto &entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || !isFontFile(entry.path().string()))
        {
            continue;
        }

        std::string path = entry.path().string();
        FT_Face face;
        if (FT_New_Face(ftLib, path.c_str(), 0, &face))
        {
            continue;
        }
        FT_Set_Pixel_Sizes(face, 0, fontSize);

        SharedFontMeta meta;
        meta.path = path;
        meta.index = indexOffset + out.size();
        meta.ascender = static_cast<int>(face->size->metrics.ascender >> 6);
        meta.descender = static_cast<int>(face->size->metrics.descender >> 6);
        meta.bitmap = std::make_shared<SingleFontBitmap>();

        FT_UInt idx;
        FT_ULong cp = FT_Get_First_Char(face, &idx);
        while (idx != 0)
        {
            meta.bitmap->set(static_cast<uint32_t>(cp));
            cp = FT_Get_Next_Char(face, cp, &idx);
        }
        out.push_back(std::move(meta));
        FT_Done_Face(face);
    }
    return out;
}

SingleLineRender::SingleLineRender(const json &config,
                                   const std::vector<SharedFontMeta> &defaultMeta,
                                   std::shared_ptr<MultiFontBitmap<256>> multiFontBitmap,
                                   GlyphCache &glyphCache,
                                   BackgroundResources &bgRes)
    : config_(config),
      defaultMeta_(defaultMeta),
      multiFontBitmap_(std::move(multiFontBitmap)),
      glyphCache_(glyphCache),
      bgRes_(bgRes)
{
    fontSize_ = config_.value("font_size", 55);
    if (config_.contains("text_sampler"))
    {
        fontSize_ = config_["text_sampler"].value("font_size", fontSize_);
    }

    if (FT_Init_FreeType(&ftLib_))
    {
        throw std::runtime_error("Failed to init FreeType (per-thread)");
    }

    openThreadFonts(defaultMeta_, defaultFonts_);
}

SingleLineRender::~SingleLineRender()
{
    for (auto &tf : defaultFonts_)
    {
        if (tf.hbFont)
        {
            hb_font_destroy(tf.hbFont);
        }
        if (tf.ftFace)
        {
            FT_Done_Face(tf.ftFace);
        }
    }
    if (ftLib_)
    {
        FT_Done_FreeType(ftLib_);
    }
}

void SingleLineRender::openThreadFonts(const std::vector<SharedFontMeta> &meta,
                                       std::vector<ThreadFont> &out)
{
    out.reserve(meta.size());
    for (const auto &m : meta)
    {
        ThreadFont tf;
        tf.meta = &m;
        FT_Face face;
        if (FT_New_Face(ftLib_, m.path.c_str(), 0, &face))
        {
            std::cerr << "Warning: thread-local FT_New_Face failed for " << m.path << "\n";
        }
        else
        {
            FT_Set_Pixel_Sizes(face, 0, fontSize_);
            tf.ftFace = face;
            tf.hbFont = hb_ft_font_create(face, nullptr);
        }
        out.push_back(std::move(tf));
    }
}

const CachedGlyph *SingleLineRender::getGlyph(const ThreadFont &tf, uint32_t codepoint)
{
    const size_t fontIdx = tf.meta->index;
    const CachedGlyph *cached = glyphCache_.find(fontIdx, codepoint);
    if (cached)
    {
        return cached;
    }
    if (!tf.ftFace)
    {
        return nullptr;
    }

    FT_UInt gi = FT_Get_Char_Index(tf.ftFace, codepoint);
    if (FT_Load_Glyph(tf.ftFace, gi, FT_LOAD_RENDER))
    {
        return nullptr;
    }

    FT_Bitmap &bmp = tf.ftFace->glyph->bitmap;
    CachedGlyph g;
    g.bitmapLeft = tf.ftFace->glyph->bitmap_left;
    g.bitmapTop = tf.ftFace->glyph->bitmap_top;
    g.advanceX = static_cast<int>(tf.ftFace->glyph->advance.x >> 6);
    g.rows = static_cast<int>(bmp.rows);
    g.width = static_cast<int>(bmp.width);
    g.pitch = static_cast<int>(bmp.pitch);
    g.buffer.assign(bmp.buffer, bmp.buffer + static_cast<size_t>(bmp.rows) * std::abs(bmp.pitch));
    return glyphCache_.insert(fontIdx, codepoint, std::move(g));
}

void SingleLineRender::compositeGlyph(cv::Mat &canvas, const CachedGlyph &glyph,
                                      int x, int y, const cv::Vec4b &color)
{
    const int bx = x + glyph.bitmapLeft;
    const int by = y - glyph.bitmapTop;
    for (int row = 0; row < glyph.rows; ++row)
    {
        const int cy = by + row;
        if (cy < 0 || cy >= canvas.rows)
        {
            continue;
        }
        for (int col = 0; col < glyph.width; ++col)
        {
            const int cx = bx + col;
            if (cx < 0 || cx >= canvas.cols)
            {
                continue;
            }
            const uint8_t alpha = glyph.buffer[static_cast<size_t>(row) * std::abs(glyph.pitch) + col];
            if (alpha == 0)
            {
                continue;
            }
            auto &dst = canvas.at<cv::Vec4b>(cy, cx);
            const float a = alpha / 255.0f;
            dst[0] = static_cast<uint8_t>(color[0] * a + dst[0] * (1.0f - a));
            dst[1] = static_cast<uint8_t>(color[1] * a + dst[1] * (1.0f - a));
            dst[2] = static_cast<uint8_t>(color[2] * a + dst[2] * (1.0f - a));
            dst[3] = std::max(dst[3], alpha);
        }
    }
}

BgInfo SingleLineRender::getRandomBgPredict()
{
    const json &bgCfg = config_.contains("bg_sampler") ? config_["bg_sampler"] : json::object();
    if (!bgRes_.bgFiles.empty() && randDouble(0, 1) < bgCfg.value("bg_image_prob", 0.3))
    {
        BgInfo bi;
        bi.isImage = true;
        bi.imagePath = bgRes_.bgFiles[randInt(0, static_cast<int>(bgRes_.bgFiles.size()) - 1)];
        return bi;
    }

    BgInfo bi;
    bi.isImage = false;
    double h, s_val, v_val;
    if (randDouble(0, 1) < bgCfg.value("gray_bg_prob", 0.7))
    {
        double vv = (randDouble(0, 1) < 0.5) ? randInt(0, 50) / 255.0 : randInt(200, 255) / 255.0;
        s_val = randInt(0, 30) / 255.0;
        h = randDouble(0, 1);
        v_val = vv;
    }
    else
    {
        h = randDouble(0, 1);
        s_val = randInt(40, 255) / 255.0;
        v_val = randInt(50, 255) / 255.0;
    }
    auto [r, g, b] = hsvToRgb(h, s_val, v_val);
    bi.color = cv::Vec3b(static_cast<uint8_t>(b * 255), static_cast<uint8_t>(g * 255), static_cast<uint8_t>(r * 255));
    return bi;
}

cv::Vec3b SingleLineRender::getBgApproxColor(const BgInfo &bg)
{
    if (!bg.isImage)
    {
        return bg.color;
    }

    {
        std::lock_guard<std::mutex> lock(bgRes_.bgCacheMutex);
        auto it = bgRes_.bgApproxColorCache.find(bg.imagePath);
        if (it != bgRes_.bgApproxColorCache.end())
        {
            return it->second;
        }
    }

    cv::Mat img = cv::imread(bg.imagePath, cv::IMREAD_COLOR);
    if (img.empty())
    {
        return cv::Vec3b(128, 128, 128);
    }

    cv::Mat small;
    cv::resize(img, small, cv::Size(kBgApproxSample, kBgApproxSample), 0, 0, cv::INTER_AREA);
    cv::Scalar m = cv::mean(small);
    cv::Vec3b approx(static_cast<uint8_t>(m[0]), static_cast<uint8_t>(m[1]), static_cast<uint8_t>(m[2]));

    {
        std::lock_guard<std::mutex> lock(bgRes_.bgCacheMutex);
        if (bgRes_.bgApproxColorCache.size() >= bgRes_.bgCacheMax)
        {
            bgRes_.bgApproxColorCache.clear();
        }
        bgRes_.bgApproxColorCache[bg.imagePath] = approx;
    }
    return approx;
}

std::pair<cv::Mat, cv::Vec3b> SingleLineRender::getBgCropAndColor(const BgInfo &bg, int tw, int th)
{
    if (!bg.isImage)
    {
        cv::Mat m(th, tw, CV_8UC3, cv::Scalar(bg.color[0], bg.color[1], bg.color[2]));
        return {m, bg.color};
    }

    cv::Mat img;
    {
        std::lock_guard<std::mutex> lock(bgRes_.bgCacheMutex);
        auto it = bgRes_.bgImageCache.find(bg.imagePath);
        if (it != bgRes_.bgImageCache.end())
        {
            img = it->second;
        }
    }

    if (img.empty())
    {
        img = cv::imread(bg.imagePath, cv::IMREAD_COLOR);
        if (img.empty())
        {
            cv::Mat m(th, tw, CV_8UC3, cv::Scalar(128, 128, 128));
            return {m, cv::Vec3b(128, 128, 128)};
        }

        std::lock_guard<std::mutex> lock(bgRes_.bgCacheMutex);
        if (bgRes_.bgImageCache.size() >= bgRes_.bgCacheMax)
        {
            bgRes_.bgImageCache.clear();
        }
        bgRes_.bgImageCache[bg.imagePath] = img;
    }

    cv::Mat work = img;
    if (work.cols <= tw || work.rows <= th)
    {
        const double scale = std::max(static_cast<double>(tw) / work.cols, static_cast<double>(th) / work.rows) + 0.1;
        cv::resize(work, work, cv::Size(static_cast<int>(work.cols * scale), static_cast<int>(work.rows * scale)), 0, 0, cv::INTER_CUBIC);
    }

    const int sx = randInt(0, work.cols - tw);
    const int sy = randInt(0, work.rows - th);
    cv::Mat crop = work(cv::Rect(sx, sy, tw, th)).clone();
    cv::Scalar mean = cv::mean(crop);
    cv::Vec3b mc(static_cast<uint8_t>(mean[0]), static_cast<uint8_t>(mean[1]), static_cast<uint8_t>(mean[2]));
    return {crop, mc};
}

cv::Vec3b SingleLineRender::getTextColor(const cv::Vec3b &bgColor)
{
    double r = bgColor[2] / 255.0;
    double g = bgColor[1] / 255.0;
    double b = bgColor[0] / 255.0;
    auto [h, s, v] = rgbToHsv(r, g, b);

    double newV, newS, newH;
    if (randDouble(0, 1) < 0.5)
    {
        newV = (std::abs(v - 0.5) < 0.2) ? (1.0 - v) : (v > 0.5 ? 0.1 : 0.9);
        newS = s * randDouble(0, 0.5);
        newH = h;
    }
    else
    {
        newV = (v > 0.5) ? (1.0 - v) : 0.8;
        newS = randDouble(0.5, 1.0);
        newH = std::fmod(h + 0.5, 1.0);
    }
    if (randDouble(0, 1) < 0.5)
    {
        newS = 0.0;
        newV = (v > 0.5) ? 0.0 : 1.0;
    }
    auto [nr, ng, nb] = hsvToRgb(newH, newS, newV);
    return cv::Vec3b(static_cast<uint8_t>(nb * 255), static_cast<uint8_t>(ng * 255), static_cast<uint8_t>(nr * 255));
}

cv::Mat SingleLineRender::renderTightText(std::string &text, const cv::Vec3b &bgColor, const std::string &sampleStrategy)
{
    if (defaultFonts_.empty())
    {
        return {};
    }

    auto chars = UTF8Helper::Split(text);
    auto codepoints = UTF8Helper::ToCodepoints(text);
    ThreadFont *selectedFont = nullptr;
    std::vector<ThreadFont *> charFonts(codepoints.size(), nullptr);

    if (sampleStrategy == "font-first")
    {
        int selectedIdx = randInt(0, static_cast<int>(defaultFonts_.size()) - 1);
        selectedFont = &defaultFonts_[selectedIdx];

        std::string newText;
        std::vector<std::string> newChars;
        std::vector<uint32_t> newCodepoints;
        for (size_t i = 0; i < codepoints.size(); ++i)
        {
            if (selectedFont->meta->bitmap->test(codepoints[i]))
            {
                newText += chars[i];
                newChars.push_back(chars[i]);
                newCodepoints.push_back(codepoints[i]);
                charFonts[newCodepoints.size() - 1] = selectedFont;
            }
        }
        text = newText;
        chars = newChars;
        codepoints = newCodepoints;
        charFonts.resize(codepoints.size());
    }

    if (chars.empty())
    {
        return {};
    }

    cv::Vec3b textColorBGR = getTextColor(bgColor);
    cv::Vec4b textColor(textColorBGR[0], textColorBGR[1], textColorBGR[2], 255);

    const int fs = fontSize_;
    const int canvasW = static_cast<int>(chars.size()) * fs * 4 + fs * 4;
    const int canvasH = fs * 6;
    cv::Mat tempImg(canvasH, canvasW, CV_8UC4, cv::Scalar(0, 0, 0, 0));

    int currX = fs * 2;
    int yOffset = fs * 3;

    for (int i = 0; i < static_cast<int>(chars.size()); ++i)
    {
        const CachedGlyph *glyph = getGlyph(*charFonts[i], codepoints[i]);
        if (!glyph)
        {
            continue;
        }
        compositeGlyph(tempImg, *glyph, currX, yOffset, textColor);
        currX += glyph->advanceX + randInt(-1, 2);
    }

    cv::Rect bbox(0, 0, 0, 0);
    for (int r = 0; r < tempImg.rows; ++r)
    {
        for (int c = 0; c < tempImg.cols; ++c)
        {
            if (tempImg.at<cv::Vec4b>(r, c)[3] > 0)
            {
                if (bbox.width == 0 && bbox.height == 0)
                {
                    bbox = cv::Rect(c, r, 1, 1);
                }
                else
                {
                    bbox.x = std::min(bbox.x, c);
                    bbox.y = std::min(bbox.y, r);
                    bbox.width = std::max(bbox.x + bbox.width, c + 1) - bbox.x;
                    bbox.height = std::max(bbox.y + bbox.height, r + 1) - bbox.y;
                }
            }
        }
    }

    if (bbox.width <= 0 || bbox.height <= 0)
    {
        return {};
    }

    int ascender = charFonts[0]->meta->ascender;
    int descender = charFonts[0]->meta->descender;
    int fontTop = yOffset - ascender;
    int fontBottom = yOffset - descender;
    int newTop = std::min(bbox.y, fontTop);
    int newBottom = std::max(bbox.y + bbox.height, fontBottom);

    newTop = std::max(0, newTop);
    newBottom = std::min(tempImg.rows, newBottom);
    bbox.y = newTop;
    bbox.height = newBottom - newTop;

    return tempImg(bbox).clone();
}