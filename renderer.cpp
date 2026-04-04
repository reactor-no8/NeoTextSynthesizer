#include "renderer.hpp"
#include "utils.hpp"
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
namespace
{
    constexpr int kBgApproxSample = 50;
}

// ---- UTF-8 helpers ----
static std::vector<uint32_t> utf8ToCodepoints(const std::string &s)
{
    std::vector<uint32_t> cps;
    size_t i = 0;
    while (i < s.size())
    {
        uint32_t cp = 0;
        unsigned char c = s[i];
        int len = 1;
        if ((c & 0x80) == 0x00)
        {
            cp = c;
            len = 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            cp = c & 0x1F;
            len = 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            cp = c & 0x0F;
            len = 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            cp = c & 0x07;
            len = 4;
        }
        for (int j = 1; j < len && i + j < s.size(); j++)
            cp = (cp << 6) | (((unsigned char)s[i + j]) & 0x3F);
        cps.push_back(cp);
        i += len;
    }
    return cps;
}

static std::vector<std::string> utf8SplitR(const std::string &s)
{
    std::vector<std::string> chars;
    size_t i = 0;
    while (i < s.size())
    {
        unsigned char c = s[i];
        int len = 1;
        if ((c & 0xE0) == 0xC0)
            len = 2;
        else if ((c & 0xF0) == 0xE0)
            len = 3;
        else if ((c & 0xF8) == 0xF0)
            len = 4;
        chars.push_back(s.substr(i, len));
        i += len;
    }
    return chars;
}

std::vector<SharedFontMeta> Renderer::buildSharedFontMeta(
    const std::string &dir, int fontSize, FT_Library ftLib, size_t indexOffset)
{
    std::vector<SharedFontMeta> out;
    for (auto &entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || !isFontFile(entry.path().string()))
            continue;
        std::string path = entry.path().string();
        FT_Face face;
        if (FT_New_Face(ftLib, path.c_str(), 0, &face))
            continue;
        FT_Set_Pixel_Sizes(face, 0, fontSize);

        SharedFontMeta meta;
        meta.path = path;
        meta.index = indexOffset + out.size();
        meta.ascender = (int)(face->size->metrics.ascender >> 6);
        meta.descender = (int)(face->size->metrics.descender >> 6);

        // Build cmap
        FT_UInt idx;
        FT_ULong cp = FT_Get_First_Char(face, &idx);
        while (idx != 0)
        {
            meta.cmap.insert((uint32_t)cp);
            cp = FT_Get_Next_Char(face, cp, &idx);
        }
        out.push_back(std::move(meta));

        FT_Done_Face(face); // temporary face; each thread opens its own
    }
    return out;
}

std::vector<std::string> Renderer::listBgFiles(const std::string &dir)
{
    std::vector<std::string> files;
    for (auto &entry : fs::directory_iterator(dir))
        if (entry.is_regular_file() && isImageFile(entry.path().string()))
            files.push_back(entry.path().string());
    return files;
}

Renderer::Renderer(const json &imgCfg,
                   const std::vector<SharedFontMeta> &defaultMeta,
                   const std::vector<SharedFontMeta> &fallbackMeta,
                   GlyphCache &glyphCache,
                   SharedBgResources &bgRes)
    : imgCfg_(imgCfg),
      defaultMeta_(defaultMeta),
      fallbackMeta_(fallbackMeta),
      glyphCache_(glyphCache),
      bgRes_(bgRes)
{
    fontSize_ = imgCfg_.value("font_size", 55);

    if (FT_Init_FreeType(&ftLib_))
        throw std::runtime_error("Failed to init FreeType (per-thread)");

    openThreadFonts(defaultMeta_, defaultFonts_);
    openThreadFonts(fallbackMeta_, fallbackFonts_);
}

Renderer::~Renderer()
{
    for (auto &tf : defaultFonts_)
    {
        if (tf.hbFont)
            hb_font_destroy(tf.hbFont);
        if (tf.ftFace)
            FT_Done_Face(tf.ftFace);
    }
    for (auto &tf : fallbackFonts_)
    {
        if (tf.hbFont)
            hb_font_destroy(tf.hbFont);
        if (tf.ftFace)
            FT_Done_Face(tf.ftFace);
    }
    if (ftLib_)
        FT_Done_FreeType(ftLib_);
}

void Renderer::openThreadFonts(const std::vector<SharedFontMeta> &meta,
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
            tf.ftFace = nullptr;
            tf.hbFont = nullptr;
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

const CachedGlyph *Renderer::getGlyph(const ThreadFont &tf, uint32_t codepoint)
{
    size_t fontIdx = tf.meta->index;

    // Fast path: cache hit (shared_lock, fully parallel).
    const CachedGlyph *cached = glyphCache_.find(fontIdx, codepoint);
    if (cached)
        return cached;

    // Slow path: rasterise using our thread-local FT_Face (no lock needed for FT).
    if (!tf.ftFace)
        return nullptr;

    FT_UInt gi = FT_Get_Char_Index(tf.ftFace, codepoint);
    if (FT_Load_Glyph(tf.ftFace, gi, FT_LOAD_RENDER))
        return nullptr;

    FT_Bitmap &bmp = tf.ftFace->glyph->bitmap;
    CachedGlyph g;
    g.bitmapLeft = tf.ftFace->glyph->bitmap_left;
    g.bitmapTop = tf.ftFace->glyph->bitmap_top;
    g.advanceX = (int)(tf.ftFace->glyph->advance.x >> 6);
    g.rows = (int)bmp.rows;
    g.width = (int)bmp.width;
    g.pitch = (int)bmp.pitch;
    g.buffer.assign(bmp.buffer, bmp.buffer + (size_t)bmp.rows * std::abs(bmp.pitch));

    // Insert into the shared cache (unique_lock, brief).
    return glyphCache_.insert(fontIdx, codepoint, std::move(g));
}

void Renderer::compositeGlyph(cv::Mat &canvas, const CachedGlyph &glyph,
                                int x, int y, const cv::Vec4b &color)
{
    int bx = x + glyph.bitmapLeft;
    int by = y - glyph.bitmapTop;
    for (int row = 0; row < glyph.rows; row++)
    {
        int cy = by + row;
        if (cy < 0 || cy >= canvas.rows)
            continue;
        for (int col = 0; col < glyph.width; col++)
        {
            int cx = bx + col;
            if (cx < 0 || cx >= canvas.cols)
                continue;
            uint8_t alpha = glyph.buffer[(size_t)row * std::abs(glyph.pitch) + col];
            if (alpha == 0)
                continue;
            auto &dst = canvas.at<cv::Vec4b>(cy, cx);
            float a = alpha / 255.0f;
            dst[0] = (uint8_t)(color[0] * a + dst[0] * (1.0f - a));
            dst[1] = (uint8_t)(color[1] * a + dst[1] * (1.0f - a));
            dst[2] = (uint8_t)(color[2] * a + dst[2] * (1.0f - a));
            dst[3] = std::max(dst[3], alpha);
        }
    }
}

BgInfo Renderer::getRandomBgPredict()
{
    if (!bgRes_.bgFiles.empty() && randDouble(0, 1) < imgCfg_.value("bg_image_prob", 0.3))
    {
        BgInfo bi;
        bi.isImage = true;
        bi.imagePath = bgRes_.bgFiles[randInt(0, (int)bgRes_.bgFiles.size() - 1)];
        return bi;
    }
    BgInfo bi;
    bi.isImage = false;
    double h, s_val, v_val;
    if (randDouble(0, 1) < imgCfg_.value("gray_bg_prob", 0.7))
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
    bi.color = cv::Vec3b((uint8_t)(b * 255), (uint8_t)(g * 255), (uint8_t)(r * 255)); // BGR
    return bi;
}

cv::Vec3b Renderer::getBgApproxColor(const BgInfo &bg)
{
    if (!bg.isImage)
        return bg.color;

    {
        std::lock_guard<std::mutex> lock(bgRes_.bgCacheMutex);
        auto it = bgRes_.bgApproxColorCache.find(bg.imagePath);
        if (it != bgRes_.bgApproxColorCache.end())
            return it->second;
    }

    cv::Mat img = cv::imread(bg.imagePath, cv::IMREAD_COLOR);
    if (img.empty())
        return cv::Vec3b(128, 128, 128);

    cv::Mat small;
    cv::resize(img, small, cv::Size(kBgApproxSample, kBgApproxSample), 0, 0, cv::INTER_AREA);
    cv::Scalar m = cv::mean(small);
    cv::Vec3b approx((uint8_t)m[0], (uint8_t)m[1], (uint8_t)m[2]);

    {
        std::lock_guard<std::mutex> lock(bgRes_.bgCacheMutex);
        if (bgRes_.bgApproxColorCache.size() >= bgRes_.bgCacheMax)
            bgRes_.bgApproxColorCache.clear();
        bgRes_.bgApproxColorCache[bg.imagePath] = approx;
    }
    return approx;
}

std::pair<cv::Mat, cv::Vec3b> Renderer::getBgCropAndColor(const BgInfo &bg, int tw, int th)
{
    if (bg.isImage)
    {
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
                bgRes_.bgImageCache.clear();
            bgRes_.bgImageCache[bg.imagePath] = img;
        }

        cv::Mat work = img;
        if (work.cols <= tw || work.rows <= th)
        {
            double scale = std::max((double)tw / work.cols, (double)th / work.rows) + 0.1;
            cv::resize(work, work, cv::Size((int)(work.cols * scale), (int)(work.rows * scale)), 0, 0, cv::INTER_CUBIC);
        }

        int sx = randInt(0, work.cols - tw), sy = randInt(0, work.rows - th);
        cv::Mat crop = work(cv::Rect(sx, sy, tw, th)).clone();
        cv::Scalar mean = cv::mean(crop);
        cv::Vec3b mc((uint8_t)mean[0], (uint8_t)mean[1], (uint8_t)mean[2]);
        return {crop, mc};
    }
    else
    {
        cv::Vec3b c = bg.color;
        cv::Mat m(th, tw, CV_8UC3, cv::Scalar(c[0], c[1], c[2]));
        return {m, c};
    }
}

cv::Vec3b Renderer::getTextColor(const cv::Vec3b &bgColor)
{
    double r = bgColor[2] / 255.0, g = bgColor[1] / 255.0, b = bgColor[0] / 255.0;
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
    return cv::Vec3b((uint8_t)(nb * 255), (uint8_t)(ng * 255), (uint8_t)(nr * 255));
}


cv::Mat Renderer::renderTightText(const std::string &text, const cv::Vec3b &bgColor)
{
    if (defaultFonts_.empty() && fallbackFonts_.empty())
        return {};

    auto chars = utf8SplitR(text);
    auto codepoints = utf8ToCodepoints(text);

    ThreadFont *selectedFont = nullptr;
    if (!defaultFonts_.empty())
    {
        int selectedIdx = randInt(0, (int)defaultFonts_.size() - 1);
        selectedFont = &defaultFonts_[selectedIdx];
    }
    else
    {
        int selectedIdx = randInt(0, (int)fallbackFonts_.size() - 1);
        selectedFont = &fallbackFonts_[selectedIdx];
    }
    const SharedFontMeta *selectedMeta = selectedFont->meta;
    
    // Select font: pick a random default font, fall back if needed.
    bool needFallback = false;
    for (uint32_t cp : codepoints)
        if (!selectedMeta->cmap.count(cp))
        {
            needFallback = true;
            break;
        }
    if (needFallback)
    {
        std::vector<int> avail;
        for (int fi = 0; fi < (int)fallbackFonts_.size(); fi++)
        {
            bool ok = true;
            for (uint32_t cp : codepoints)
                if (!fallbackFonts_[fi].meta->cmap.count(cp))
                {
                    ok = false;
                    break;
                }
            if (ok)
                avail.push_back(fi);
        }
        if (!avail.empty())
        {
            int pick = avail[randInt(0, (int)avail.size() - 1)];
            selectedFont = &fallbackFonts_[pick];
            selectedMeta = selectedFont->meta;
        }
        else if (!fallbackFonts_.empty())
        {
            int pick = randInt(0, (int)fallbackFonts_.size() - 1);
            selectedFont = &fallbackFonts_[pick];
            selectedMeta = selectedFont->meta;
        }
    }

    cv::Vec3b textColorBGR = getTextColor(bgColor);
    cv::Vec4b textColor(textColorBGR[0], textColorBGR[1], textColorBGR[2], 255);

    bool applyEffect = randDouble(0, 1) < imgCfg_.value("effect_prob", 0.2);
    std::string activeEffect;
    std::set<int> effectRange;
    if (applyEffect)
    {
        static const char *effects[] = {"italic", "underline", "strikethrough"};
        activeEffect = effects[randInt(0, 2)];
        if (randDouble(0, 1) < imgCfg_.value("partial_effect_prob", 0.8))
        {
            int length = randInt(3, 10);
            int start = randInt(0, std::max(0, (int)chars.size() - length));
            for (int i = start; i < start + length; i++)
                effectRange.insert(i);
        }
        else
        {
            for (int i = 0; i < (int)chars.size(); i++)
                effectRange.insert(i);
        }
    }

    int fs = fontSize_;
    int canvasW = (int)chars.size() * fs * 4 + fs * 4;
    int canvasH = fs * 6;
    cv::Mat tempImg(canvasH, canvasW, CV_8UC4, cv::Scalar(0, 0, 0, 0));

    int currX = fs * 2, yOffset = fs * 3; // baseline approx

    for (int i = 0; i < (int)chars.size(); i++)
    {
        bool inEffect = effectRange.count(i) > 0;
        uint32_t cp = codepoints[i];

        // Get the glyph (from cache or rasterise via thread-local FT_Face).
        const CachedGlyph *glyph = getGlyph(*selectedFont, cp);
        if (!glyph)
            continue;

        int charW = glyph->advanceX;
        int actualW = charW;

        if (inEffect && activeEffect == "italic")
        {
            float shearFactor = 0.3f;
            int tmpW = charW + (int)(shearFactor * fs * 2) + 8;
            int tmpH = fs * 2;
            int baseY = fs;

            cv::Mat charCanvas(tmpH, tmpW, CV_8UC4, cv::Scalar(0, 0, 0, 0));
            compositeGlyph(charCanvas, *glyph, 2, baseY, textColor);

            for (int row = 0; row < tmpH; row++)
            {
                int shift = (int)(shearFactor * (baseY - row));
                for (int col = 0; col < tmpW; col++)
                {
                    cv::Vec4b src = charCanvas.at<cv::Vec4b>(row, col);
                    if (src[3] == 0)
                        continue;

                    int dx = currX + (col + shift - 2);
                    int dy = (yOffset - baseY) + row;
                    if (dx < 0 || dx >= tempImg.cols || dy < 0 || dy >= tempImg.rows)
                        continue;

                    auto &dst = tempImg.at<cv::Vec4b>(dy, dx);
                    float a = src[3] / 255.0f;
                    dst[0] = (uint8_t)(src[0] * a + dst[0] * (1 - a));
                    dst[1] = (uint8_t)(src[1] * a + dst[1] * (1 - a));
                    dst[2] = (uint8_t)(src[2] * a + dst[2] * (1 - a));
                    dst[3] = std::max(dst[3], src[3]);
                }
            }
            actualW = charW;
        }
        else
        {
            compositeGlyph(tempImg, *glyph, currX, yOffset, textColor);
        }

        if (inEffect)
        {
            int lineW = std::max(1, fs / 18);
            if (activeEffect == "underline")
            {
                int ly = yOffset - fs + fs - 2;
                cv::line(tempImg, cv::Point(currX, ly), cv::Point(currX + actualW, ly),
                         cv::Scalar(textColor[0], textColor[1], textColor[2], 255), lineW);
            }
            else if (activeEffect == "strikethrough")
            {
                int ly = yOffset - fs + fs / 2 + 2;
                cv::line(tempImg, cv::Point(currX, ly), cv::Point(currX + actualW, ly),
                         cv::Scalar(textColor[0], textColor[1], textColor[2], 255), lineW);
            }
        }

        currX += actualW + randInt(-1, 2);
    }

    // Crop to bounding box of non-transparent pixels
    cv::Rect bbox(0, 0, 0, 0);
    for (int r = 0; r < tempImg.rows; r++)
        for (int c = 0; c < tempImg.cols; c++)
            if (tempImg.at<cv::Vec4b>(r, c)[3] > 0)
            {
                if (bbox.width == 0 && bbox.height == 0)
                    bbox = cv::Rect(c, r, 1, 1);
                else
                {
                    bbox.x = std::min(bbox.x, c);
                    bbox.y = std::min(bbox.y, r);
                    bbox.width = std::max(bbox.x + bbox.width, c + 1) - bbox.x;
                    bbox.height = std::max(bbox.y + bbox.height, r + 1) - bbox.y;
                }
            }
    if (bbox.width <= 0 || bbox.height <= 0)
        return {};

    // Expand vertical bounding box to match font metrics (ascender/descender).
    // These values come from SharedFontMeta, which is immutable after construction.
    int ascender = selectedMeta->ascender;
    int descender = selectedMeta->descender;
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