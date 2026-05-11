#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

class ImageProcessor {
public:
    // 统一的入口，根据 type 选择不同的算法
    static bool process(int type, const std::string& input, std::string& output);

private:
    // 各种具体的算法函数
    static void makeGray(const cv::Mat& src, cv::Mat& dst);
    static void detectEdges(const cv::Mat& src, cv::Mat& dst);
    static void blurImage(const cv::Mat& src, cv::Mat& dst);
    static void invertImage(const cv::Mat& src, cv::Mat& dst);
    static void thresholdImage(const cv::Mat& src, cv::Mat& dst);
};

#endif