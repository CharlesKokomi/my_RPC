#include "processor.h"
#include <iostream>

ImageProcessor::ImageProcessor() : env(ORT_LOGGING_LEVEL_ERROR, "Inference") {
    // 1. 初始化推理设置
    session_options.SetIntraOpNumThreads(1);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // 2. 加载模型 (注意路径与你 ls -lh 展示的一致)
    const char* model_path = "models/super_resolution.onnx";
    session = std::make_unique<Ort::Session>(env, model_path, session_options);
}
void ImageProcessor::runSuperResolution(const cv::Mat& src, cv::Mat& dst) {
    // 1. 获取原始尺寸，后续用于还原或对比
    int org_w = src.cols;
    int org_h = src.rows;

    // 2. 预处理：强制 Resize 到模型要求的 224x224
    cv::Mat resized;
    cv::resize(src, resized, cv::Size(224, 224));

    // 3. 颜色空间转换：提取 Y 通道 (AI 只处理亮度)
    cv::Mat ycrcb, y_channel;
    cv::cvtColor(resized, ycrcb, cv::COLOR_BGR2YCrCb);
    std::vector<cv::Mat> channels;
    cv::split(ycrcb, channels);
    y_channel = channels[0];

    // 4. 构建张量 (Tensor)
    // 根据脚本输出：Batch=1, Channel=1, H=224, W=224
    std::vector<int64_t> input_dims = {1, 1, 224, 224};
    std::vector<float> input_tensor_values;
    
    cv::Mat float_y;
    y_channel.convertTo(float_y, CV_32F, 1.0 / 255.0); // 归一化到 [0, 1]
    input_tensor_values.assign((float*)float_y.datastart, (float*)float_y.dataend);

    // 5. 推理执行
    const char* input_names[] = {"input"};   // 确保名称与脚本输出一致
    const char* output_names[] = {"output"}; // 通常为 output
    
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input_tensor_values.data(), input_tensor_values.size(), 
        input_dims.data(), input_dims.size());

    auto output_tensors = session->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);

    // 6. 后处理：将结果转回图像
    float* output_data = output_tensors[0].GetTensorMutableData<float>();
    cv::Mat result_y(224, 224, CV_32F, output_data);
    result_y.convertTo(result_y, CV_8U, 255.0); // 反归一化

    // 7. 合并通道并缩放到原始大小或放大显示
    channels[0] = result_y;
    cv::merge(channels, ycrcb);
    cv::Mat out_bgr;
    cv::cvtColor(ycrcb, out_bgr, cv::COLOR_YCrCb2BGR);

    // 演示突破：如果想展示超分效果，可以返回 org_w*2 的尺寸
    cv::resize(out_bgr, dst, cv::Size(org_w*2, org_h*2)); 
}

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
            case 6: // 超分辨率重建
                runSuperResolution(src, dst);
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