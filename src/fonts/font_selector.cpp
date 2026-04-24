#include "fonts/font_selector.hpp"

#include <algorithm>
#include <stdexcept>
#include <fstream>

#include <boost/dynamic_bitset.hpp>

#include "utils/utf8_helper.hpp"
#include "utils/utils.hpp"


std::vector<SharedFontMeta> FontSelector::buildSharedFontMeta(
    const std::vector<std::string> &paths, int fontSize)
{
    std::vector<SharedFontMeta> out;
    FontLibrary tempLib;
    size_t index = 0;

    for (const auto &path : paths)
    {
        fs::path fsPath(path);
        
        if (fs::is_directory(fsPath))
        {
            for (auto &entry : fs::directory_iterator(path))
            {
                if (!entry.is_regular_file() || !isFontFile(entry.path().string()))
                {
                    continue;
                }

                std::string fontPath = entry.path().string();
                FT_Face face;
                if (FT_New_Face(tempLib.get(), fontPath.c_str(), 0, &face))
                {
                    continue;
                }
                FT_Set_Pixel_Sizes(face, 0, fontSize);

                // Read font file into memory
                std::ifstream fontFile(fontPath, std::ios::binary);
                if (!fontFile)
                {
                    FT_Done_Face(face);
                    continue;
                }

                fontFile.seekg(0, std::ios::end);
                size_t fileSize = fontFile.tellg();
                fontFile.seekg(0, std::ios::beg);

                std::vector<uint8_t> cmapData(fileSize);
                fontFile.read(reinterpret_cast<char *>(cmapData.data()), fileSize);
                fontFile.close();

                SharedFontMeta meta;
                meta.cmap = std::move(cmapData);
                meta.fontSize = fontSize;
                meta.index = index++;
                meta.bitmap = std::make_shared<SingleFontBitmap>();

                // Build bitmap of supported codepoints
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
        }
        else if (fs::is_regular_file(fsPath) && isFontFile(path))
        {
            // Single font file
            FT_Face face;
            if (FT_New_Face(tempLib.get(), path.c_str(), 0, &face))
            {
                continue;
            }
            FT_Set_Pixel_Sizes(face, 0, fontSize);

            std::ifstream fontFile(path, std::ios::binary);
            if (!fontFile)
            {
                FT_Done_Face(face);
                continue;
            }

            fontFile.seekg(0, std::ios::end);
            size_t fileSize = fontFile.tellg();
            fontFile.seekg(0, std::ios::beg);

            std::vector<uint8_t> cmapData(fileSize);
            fontFile.read(reinterpret_cast<char *>(cmapData.data()), fileSize);
            fontFile.close();

            SharedFontMeta meta;
            meta.cmap = std::move(cmapData);
            meta.fontSize = fontSize;
            meta.index = index++;
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
    }

    return out;
}

FontSelector::FontSelector(std::vector<SharedFontMeta> fontMetas, FontLibrary& library)
    : fontMetas_(std::move(fontMetas))
{
    fonts_.reserve(fontMetas_.size());
    
    for (auto &meta : fontMetas_)
    {
        fonts_.emplace_back(library, meta);
    }

    if (!fonts_.empty())
    {
        initMultiFontBitmap();
    }
}

std::vector<FontSelector> FontSelector::createThreadSelectors(std::vector<FontLibrary>& libraries) const
{
    std::vector<FontSelector> selectors;
    selectors.reserve(libraries.size());

    for (size_t i = 0; i < libraries.size(); ++i)
    {
        selectors.emplace_back(); 
        FontSelector& ts = selectors.back();

        ts.strategy_ = strategy_;
        ts.multiFontBitmap_ = multiFontBitmap_;
        
        ts.fonts_.reserve(fontMetas_.size());
        for (const auto &meta : fontMetas_)
        {
            ts.fonts_.emplace_back(libraries[i], meta);
        }
    }
    return selectors;
}


void FontSelector::initMultiFontBitmap()
{
    if (fonts_.empty())
    {
        return;
    }

    multiFontBitmap_ = std::make_shared<MultiFontBitmap>(fonts_.size());
    
    for (size_t i = 0; i < fonts_.size(); ++i)
    {
        const auto &font = fonts_[i];
        const auto &descriptor = font.getDescriptor();
        
        if (descriptor.bitmap)
        {
            auto codepoints = descriptor.bitmap->get_all_codepoints();
            for (uint32_t cp : codepoints)
            {
                multiFontBitmap_->set(cp, i);
            }
        }
    }
}

size_t FontSelector::selectFont(const std::string &in_text, std::string &out_text)
{
    if (fonts_.empty())
    {
        out_text = in_text;
        throw std::runtime_error("No fonts available for selection");
    }

    if (strategy_ == "sample-first")
    {
        return selectSampleFirst(in_text, out_text);
    }
    else if (strategy_ == "auto-fallback")
    {
        return selectAutoFallback(in_text, out_text);
    }
    else
    {
        return selectFontFirst(in_text, out_text);
    }
}

const FontResource& FontSelector::getFont(size_t index) const
{
    if (index >= fonts_.size())
    {
        throw std::out_of_range("Font index out of range");
    }
    return fonts_[index];
}

size_t FontSelector::selectFontFirst(const std::string &in_text, std::string &out_text)
{
    // Pick a random font; keep only the characters it supports
    const size_t selectedIdx = randInt(0, static_cast<int>(fonts_.size()) - 1);
    const FontResource &selectedFont = fonts_[selectedIdx];
    
    auto chars = UTF8Helper::Split(in_text);
    auto codepoints = UTF8Helper::ToCodepoints(in_text);
    
    if (chars.empty() || codepoints.empty())
    {
        out_text = in_text;
        return selectedIdx;
    }

    // Filter out unsupported characters
    std::string filteredText;
    for (size_t i = 0; i < codepoints.size(); ++i)
    {
        if (selectedFont.getDescriptor().bitmap->test(codepoints[i]))
        {
            filteredText += chars[i];
        }
    }
    
    out_text = filteredText.empty() ? in_text : filteredText;
    return selectedIdx;
}

size_t FontSelector::selectSampleFirst(const std::string &in_text, std::string &out_text)
{
    auto chars = UTF8Helper::Split(in_text);
    auto codepoints = UTF8Helper::ToCodepoints(in_text);
    
    if (chars.empty() || codepoints.empty())
    {
        out_text = in_text;
        return 0;
    }

    if (!multiFontBitmap_)
    {
        initMultiFontBitmap();
    }

    const size_t numFonts = fonts_.size();

    // Try to find a single font that covers ALL characters.
    // Start with all bits set (every font is a candidate), then AND with each codepoint's coverage.
    boost::dynamic_bitset<> mask(numFonts);
    mask.set(); // all fonts are candidates initially
    for (uint32_t cp : codepoints)
    {
        mask &= multiFontBitmap_->query(cp);
        if (mask.none())
            break;
    }

    if (mask.any())
    {
        // At least one font covers everything — pick one randomly
        std::vector<int> avail;
        for (size_t i = mask.find_first(); i != boost::dynamic_bitset<>::npos; i = mask.find_next(i))
        {
            avail.push_back(static_cast<int>(i));
        }
        out_text = in_text;
        return avail[randInt(0, static_cast<int>(avail.size()) - 1)];
    }
    else
    {
        // No single font covers all — pick the highest-coverage font and drop characters it cannot render
        std::vector<int> coverage(numFonts, 0);
        for (uint32_t cp : codepoints)
        {
            boost::dynamic_bitset<> bs = multiFontBitmap_->query(cp);
            for (size_t i = bs.find_first(); i != boost::dynamic_bitset<>::npos; i = bs.find_next(i))
            {
                coverage[i]++;
            }
        }

        int maxCov = -1;
        std::vector<int> bestFonts;
        for (size_t i = 0; i < numFonts; ++i)
        {
            if (coverage[i] > maxCov)
            {
                maxCov = coverage[i];
                bestFonts = {static_cast<int>(i)};
            }
            else if (coverage[i] == maxCov)
            {
                bestFonts.push_back(static_cast<int>(i));
            }
        }

        if (!bestFonts.empty() && maxCov > 0)
        {
            int bestFontIdx = bestFonts[randInt(0, static_cast<int>(bestFonts.size()) - 1)];
            const FontResource &bestFont = fonts_[bestFontIdx];
            
            // Filter out unsupported characters
            std::string filteredText;
            for (size_t i = 0; i < codepoints.size(); ++i)
            {
                if (bestFont.getDescriptor().bitmap->test(codepoints[i]))
                {
                    filteredText += chars[i];
                }
            }
            
            out_text = filteredText.empty() ? in_text : filteredText;
            return bestFontIdx;
        }
    }

    // Fallback: just pick a random font
    out_text = in_text;
    return randInt(0, static_cast<int>(fonts_.size()) - 1);
}

size_t FontSelector::selectAutoFallback(const std::string &in_text, std::string &out_text)
{
    auto chars = UTF8Helper::Split(in_text);
    auto codepoints = UTF8Helper::ToCodepoints(in_text);
    
    if (chars.empty() || codepoints.empty())
    {
        out_text = in_text;
        return 0;
    }

    if (!multiFontBitmap_)
    {
        initMultiFontBitmap();
    }

    const size_t numFonts = fonts_.size();

    // Build a full-coverage mask: fonts that cover ALL codepoints in the text
    boost::dynamic_bitset<> fullMask(numFonts);
    fullMask.set();
    for (uint32_t cp : codepoints)
    {
        fullMask &= multiFontBitmap_->query(cp);
        if (fullMask.none())
            break;
    }

    // Step 1: check if the randomly selected font already covers everything
    const size_t selectedIdx = randInt(0, static_cast<int>(numFonts) - 1);
    if (fullMask.test(selectedIdx))
    {
        out_text = in_text;
        return selectedIdx;
    }

    // If there are other fonts that cover everything, pick one randomly from them
    if (fullMask.any())
    {
        std::vector<int> avail;
        for (size_t i = fullMask.find_first(); i != boost::dynamic_bitset<>::npos; i = fullMask.find_next(i))
        {
            avail.push_back(static_cast<int>(i));
        }
        out_text = in_text;
        return avail[randInt(0, static_cast<int>(avail.size()) - 1)];
    }

    // If no font covers everything — find the font(s) with the best coverage and pick randomly
    std::vector<int> coverage(numFonts, 0);
    for (uint32_t cp : codepoints)
    {
        boost::dynamic_bitset<> bs = multiFontBitmap_->query(cp);
        for (size_t i = bs.find_first(); i != boost::dynamic_bitset<>::npos; i = bs.find_next(i))
        {
            coverage[i]++;
        }
    }

    int maxCov = -1;
    std::vector<int> bestFonts;
    for (size_t i = 0; i < numFonts; ++i)
    {
        if (coverage[i] > maxCov)
        {
            maxCov = coverage[i];
            bestFonts = {static_cast<int>(i)};
        }
        else if (coverage[i] == maxCov)
        {
            bestFonts.push_back(static_cast<int>(i));
        }
    }

    if (!bestFonts.empty() && maxCov > 0)
    {
        int bestFontIdx = bestFonts[randInt(0, static_cast<int>(bestFonts.size()) - 1)];
        const FontResource &bestFont = fonts_[bestFontIdx];

        std::string filteredText;
        for (size_t i = 0; i < codepoints.size(); ++i)
        {
            if (bestFont.getDescriptor().bitmap->test(codepoints[i]))
            {
                filteredText += chars[i];
            }
        }

        out_text = filteredText.empty() ? in_text : filteredText;
        return bestFontIdx;
    }

    // ...Return the originally selected font with the text as-is
    out_text = in_text;
    return selectedIdx;
}