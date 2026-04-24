#pragma once
#include <string>
#include <vector>
#include <tuple>
#include <opencv2/opencv.hpp>

// Normalize probabilities in-place, warn if not close to 1.0
void normalizeProbsVec(std::vector<double> &probs);

// HSV (all in [0,1]) -> RGB (all in [0,1])
std::tuple<double, double, double> hsvToRgb(double h, double s, double v);

// RGB (all in [0,1]) -> HSV (all in [0,1])
std::tuple<double, double, double> rgbToHsv(double r, double g, double b);

// Parse a hex color string "#RRGGBB" to an OpenCV BGR color
cv::Vec3b parseHexColor(const std::string &hexColor);

// Check if a string is a valid hex color format "#RRGGBB"
bool isValidHexColor(const std::string &hexColor);

// Generate a random color between two hex color values
cv::Vec3b randomColorInRange(const std::string &hexColor1, const std::string &hexColor2);

// Map a 0-based global index to a hierarchical relative path (e.g. "00000001/00000002/00000003.png").
// levels: each element is the max number of entries per directory at that depth.
// Empty levels means flat storage ("00000001.png").
std::string indexToHierarchicalPath(int64_t index, const std::vector<int64_t> &levels);

// Check if filename has an image extension
bool isImageFile(const std::string &filename);

// Check if filename has a font extension
bool isFontFile(const std::string &filename);

// List full paths of files in a directory
std::vector<std::string> listFiles(const std::string &dir);

// Random int in [lo, hi]
int randInt(int lo, int hi);

// Random double in [lo, hi)
double randDouble(double lo, double hi);