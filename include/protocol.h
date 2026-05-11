#include <cstdint>

// 使用 #pragma pack(1) 确保结构体按 1 字节对齐，防止 C++ 自动补齐导致与 Java 对不上的情况
#pragma pack(push, 1)
struct RpcHeader {
    uint32_t magic_number; // 魔数，例如 0xCAFEBABE，用来过滤非法包
    uint32_t version;      // 版本号
    uint32_t body_len;     // 后面跟着的 Body 数据的长度
    uint32_t type;         // 业务类型，比如 1 代表图像灰度化，2 代表边缘检测
};
#pragma pack(pop)