#include "algorithms/transforms.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <cmath>

namespace geometric_transforms
{

cv::Mat applyAffineTransform(cv::Mat& mask, double angleMax, double angleMin) {
    int w = mask.cols;
    int h = mask.rows;

    cv::Point2f center(w / 2.0f, h / 2.0f);
    double angle = randDouble(angleMin, angleMax);
    cv::Mat M = cv::getRotationMatrix2D(center, angle, 1.0);

    // Compute where the 4 corners land after rotation
    std::vector<cv::Point2f> corners = {
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(static_cast<float>(w), 0.0f),
        cv::Point2f(static_cast<float>(w), static_cast<float>(h)),
        cv::Point2f(0.0f, static_cast<float>(h))
    };

    std::vector<cv::Point2f> transformedCorners;
    cv::transform(corners, transformedCorners, M);

    // Tight bounding box of the rotated rectangle
    float minX = transformedCorners[0].x;
    float minY = transformedCorners[0].y;
    float maxX = minX;
    float maxY = minY;
    for (const auto& pt : transformedCorners) {
        minX = std::min(minX, pt.x);
        minY = std::min(minY, pt.y);
        maxX = std::max(maxX, pt.x);
        maxY = std::max(maxY, pt.y);
    }

    int newW = static_cast<int>(std::ceil(maxX - minX));
    int newH = static_cast<int>(std::ceil(maxY - minY));

    // Shift the rotation matrix to put the bounding box at origin
    M.at<double>(0, 2) -= minX;
    M.at<double>(1, 2) -= minY;

    cv::Mat result;
    cv::warpAffine(mask, result, M, cv::Size(newW, newH),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
    return result;
}

cv::Mat applyPerspectiveTransform(cv::Mat& mask, double maxDistortion) {
    int w = mask.cols;
    int h = mask.rows;

    std::vector<cv::Point2f> srcPoints = {
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(static_cast<float>(w), 0.0f),
        cv::Point2f(static_cast<float>(w), static_cast<float>(h)),
        cv::Point2f(0.0f, static_cast<float>(h))
    };

    std::vector<cv::Point2f> dstPoints = srcPoints;
    for (auto& pt : dstPoints) {
        pt.x += randDouble(-maxDistortion, maxDistortion) * w;
        pt.y += randDouble(-maxDistortion, maxDistortion) * h;
    }

    // Bounding rect of the destination quad
    float minX = dstPoints[0].x;
    float minY = dstPoints[0].y;
    float maxX = minX;
    float maxY = minY;
    for (const auto& pt : dstPoints) {
        minX = std::min(minX, pt.x);
        minY = std::min(minY, pt.y);
        maxX = std::max(maxX, pt.x);
        maxY = std::max(maxY, pt.y);
    }

    int newW = static_cast<int>(std::ceil(maxX - minX));
    int newH = static_cast<int>(std::ceil(maxY - minY));

    // Shift destination points to align with the new canvas origin
    for (auto& pt : dstPoints) {
        pt.x -= minX;
        pt.y -= minY;
    }

    cv::Mat M = cv::getPerspectiveTransform(srcPoints, dstPoints);
    cv::Mat result;
    cv::warpPerspective(mask, result, M, cv::Size(newW, newH),
                        cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));

    // Tight-crop to non-zero content (removes any remaining black borders)
    cv::Rect contentRect = cv::boundingRect(result);
    if (contentRect.area() > 0) {
        result = result(contentRect).clone();
    }

    return result;
}

}