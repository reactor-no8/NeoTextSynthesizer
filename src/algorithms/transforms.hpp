#include <opencv2/opencv.hpp>

namespace geometric_transforms
{

// Applies an random affine transformation to the input mask
cv::Mat applyAffineTransform(cv::Mat& mask, double angleMax, double angleMin);

// Applies a random perspective transformation to the input mask
cv::Mat applyPerspectiveTransform(cv::Mat& mask, double maxDistortion);

}