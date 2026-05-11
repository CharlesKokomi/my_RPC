#include "processor.h"
#include <iostream>

bool ImageProcessor::process(int type, const std::string& input, std::string& output) {
    try {
        // 1. 解码
        std::vector<uchar> data(input.begin(), input.end());
        cv::Mat src = cv::imdecode(data, cv::IMREAD_COLOR);
        if (src.empty()) {
            std::cerr << "[Processor] 解码失败，数据为空或格式错误" << std::endl;
            return false;
        }

        cv::Mat dst;
        // 2. 算法分发
        switch (type) {
            case 1: // 灰度化
                makeGray(src, dst);
                break;
            case 2: // Canny 边缘检测
                detectEdges(src, dst);
                break;
            case 3: // 高斯模糊 (去噪)
                blurImage(src, dst);
                break;
            case 4: // 反色处理 (底片效果)
                invertImage(src, dst);
                break;
            case 5: // 阈值分割 (二值化)
                thresholdImage(src, dst);
                break;
            default:
                std::cout << "[Processor] 未知类型，原样返回" << std::endl;
                src.copyTo(dst);
        }

        // 3. 编码
        std::vector<uchar> buf;
        cv::imencode(".jpg", dst, buf);
        output.assign(buf.begin(), buf.end());
        return true;

    } catch (const cv::Exception& e) {
        std::cerr << "[Processor] OpenCV 异常: " << e.what() << std::endl;
        return false;
    }
}

// --- 算法实现 ---

void ImageProcessor::makeGray(const cv::Mat& src, cv::Mat& dst) {
    cv::cvtColor(src, dst, cv::COLOR_BGR2GRAY);
}

void ImageProcessor::detectEdges(const cv::Mat& src, cv::Mat& dst) {
    cv::Mat gray;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY); // 先转灰度提高效果
    cv::Canny(gray, dst, 100, 200);
}

void ImageProcessor::blurImage(const cv::Mat& src, cv::Mat& dst) {
    // 使用 15x15 的核进行高斯模糊
    cv::GaussianBlur(src, dst, cv::Size(15, 15), 0);
}

void ImageProcessor::invertImage(const cv::Mat& src, cv::Mat& dst) {
    // 图像反色：255 - pixel_value
    cv::bitwise_not(src, dst);
}

void ImageProcessor::thresholdImage(const cv::Mat& src, cv::Mat& dst) {
    cv::Mat gray;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    // 大津法自动寻找阈值进行二值化
    cv::threshold(gray, dst, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
}