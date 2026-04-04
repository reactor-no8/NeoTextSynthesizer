#include "utils.hpp"
#include <iostream>
#include <cmath>
#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

static std::mt19937 &getRng()
{
    thread_local std::mt19937 rng(std::random_device{}());
    return rng;
}

int randInt(int lo, int hi)
{
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(getRng());
}

double randDouble(double lo, double hi)
{
    std::uniform_real_distribution<double> dist(lo, hi);
    return dist(getRng());
}

void normalizeProbsVec(std::vector<double> &probs)
{
    double total = 0.0;
    for (double p : probs)
        total += p;
    if (!std::isnan(total) && std::abs(total - 1.0) > 1e-5)
        std::cerr << "Warning: Probabilities sum to " << total << ", not 1.0. Normalizing...\n";
    for (double &p : probs)
        p /= total;
}

std::tuple<double, double, double> hsvToRgb(double h, double s, double v)
{
    if (s == 0.0)
        return {v, v, v};
    int i = (int)(h * 6.0);
    double f = h * 6.0 - i;
    double p = v * (1.0 - s);
    double q = v * (1.0 - f * s);
    double t = v * (1.0 - (1.0 - f) * s);
    switch (i % 6)
    {
    case 0:
        return {v, t, p};
    case 1:
        return {q, v, p};
    case 2:
        return {p, v, t};
    case 3:
        return {p, q, v};
    case 4:
        return {t, p, v};
    default:
        return {v, p, q};
    }
}

std::tuple<double, double, double> rgbToHsv(double r, double g, double b)
{
    double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    double v = mx, s = (mx == 0.0) ? 0.0 : (mx - mn) / mx, h = 0.0;
    if (mx != mn)
    {
        double d = mx - mn;
        if (mx == r)
            h = (g - b) / d + (g < b ? 6.0 : 0.0);
        else if (mx == g)
            h = (b - r) / d + 2.0;
        else
            h = (r - g) / d + 4.0;
        h /= 6.0;
    }
    return {h, s, v};
}

std::string indexToHierarchicalPath(int64_t index, const std::vector<int64_t> &levels)
{
    // Build path components from innermost (filename) to outermost (top dir).
    // Number of path components = levels.size() + 1  (dirs + filename stem).
    int L = (int)levels.size();
    std::vector<int64_t> components(L + 1);

    int64_t remaining = index;
    for (int i = L - 1; i >= 0; i--)
    {
        components[i + 1] = remaining % levels[i]; // directory index at depth i+1 (0-based)
        remaining /= levels[i];
    }
    components[0] = remaining; // outermost directory (unbounded)

    // Format each component as 1-based, zero-padded to 8 digits.
    auto pad8 = [](int64_t v) -> std::string
    {
        std::ostringstream ss;
        ss << std::setfill('0') << std::setw(8) << (v + 1);
        return ss.str();
    };

    if (L == 0)
    {
        // Flat: just the filename
        return pad8(index) + ".png";
    }

    std::string result;
    for (int i = 0; i < L; i++)
    {
        result += pad8(components[i]);
        result += '/';
    }
    result += pad8(components[L]) + ".png";
    return result;
}

bool isImageFile(const std::string &filename)
{
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto &ext : {".jpg", ".jpeg", ".png", ".bmp", ".webp"})
        if (lower.size() > strlen(ext) && lower.substr(lower.size() - strlen(ext)) == ext)
            return true;
    return false;
}

bool isFontFile(const std::string &filename)
{
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto &ext : {".ttf", ".otf"})
        if (lower.size() > strlen(ext) && lower.substr(lower.size() - strlen(ext)) == ext)
            return true;
    return false;
}

std::vector<std::string> listFiles(const std::string &dir)
{
    std::vector<std::string> result;
    for (auto &entry : fs::directory_iterator(dir))
        if (entry.is_regular_file())
            result.push_back(entry.path().string());
    return result;
}
