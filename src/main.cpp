#include <iostream>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <arpa/inet.h> 
#include <opencv2/opencv.hpp>
#include <errno.h>
#include "protocol.h"         
#include "message.pb.h"       
#include "processor.h"
#include "ThreadPool.h"
#include <cstdlib> // 用于 system()
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>
#define MAX_EVENTS 1024
#define PORT 9000
void startHeartbeat(std::string ip, int port) {
    // 开启一个后台线程，不阻塞主程序的 epoll 循环
    std::thread([=]() {
        std::string gist_id = "8991b3af79c3b056768177e4bb84156f";
        std::string token = "";
        
        // 每个服务器使用独立的文件名，如 "node_139_159_140_251.json"
        std::string filename = "node_" + ip;
        std::replace(filename.begin(), filename.end(), '.', '_');
        filename += ".json";

        while (true) {
            long long now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            // 这里直接存放当前节点的信息，不再需要嵌套 server_key 层级
            std::string content = "{\\\\\\\"ip\\\\\\\":\\\\\\\"" + ip + "\\\\\\\",\\\\\\\"port\\\\\\\":" 
                                  + std::to_string(port) + ",\\\\\\\"last_heartbeat\\\\\\\":" 
                                  + std::to_string(now) + "}";
            
            // 注意：files 里的 Key 动态改为 filename
            std::string json_payload = "{\\\"files\\\":{\\\"" + filename + "\\\":{\\\"content\\\":\\\"" + content + "\\\"} } }";

            std::string cmd = "curl -L -X PATCH "
                              "-H \"Authorization: token " + token + "\" "
                              "-d \"" + json_payload + "\" "
                              "https://api.github.com/gists/" + gist_id + " > /dev/null 2>&1";

            int ret = system(cmd.c_str());
            if (ret != 0) {
                std::cout<<"system(cmd.c_str())返回值不为0!"<<std::endl;
            }
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    }).detach();
}
std::string getPublicIP() {
    std::array<char, 128> buffer;
    // 优先使用你测试成功的 ifconfig.me，备选 ipify.org
    std::vector<std::string> apis = {
        "http://ifconfig.me",
        "http://api.ipify.org"
    };

    for (const auto& api : apis) {
        // 使用 curl 获取，设置 3 秒超时
        std::string cmd = "curl --silent --connect-timeout 3 " + api;
        auto deleter = [](FILE* f) { pclose(f); };
        std::unique_ptr<FILE, decltype(deleter)> pipe(popen(cmd.c_str(), "r"), deleter);
        if (pipe) {
            std::string result;
            while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
                result += buffer.data();
            }
            // 清理掉返回结果中可能的换行符、空格
            result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
            result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
            result.erase(std::remove(result.begin(), result.end(), ' '), result.end());

            // 简单的格式验证：包含点号且非空
            if (!result.empty() && result.find('.') != std::string::npos) {
                return result; 
            }
        }
    }
    
    // 如果全部失败，这里可以保留一个默认值，或者抛出异常提醒
    std::cerr << "[Critical] 无法获取公网IP，请检查网络连接！" << std::endl;
    return "127.0.0.1"; 
}
// 将 FD 设为非阻塞
void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 将 FD 恢复为阻塞
void setBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

int main() {
    ThreadPool pool(2);
    ImageProcessor processor;
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        return 1;
    }
    
    listen(listen_fd, 5);
    setNonBlocking(listen_fd);

    int epoll_fd = epoll_create1(0);
    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd; 
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev);

    std::cout << "RPC Engine Started on Port " << PORT << "..." << std::endl;
    std::string my_ip = getPublicIP();
    std::cout << "[System] 自动识别公网 IP: " << my_ip << std::endl;
    startHeartbeat(my_ip,9000);
    while (true) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < nfds; ++i) {
            int current_fd = events[i].data.fd;

            if (current_fd == listen_fd) {
                int conn_fd = accept(listen_fd, NULL, NULL);
                if (conn_fd < 0) continue;
                
                setNonBlocking(conn_fd);
                ev.events = EPOLLIN | EPOLLET; // 边缘触发
                ev.data.fd = conn_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &ev);
                std::cout << "[System] 新连接进入: FD " << conn_fd << std::endl;
            } else {
                int client_fd = current_fd;
                RpcHeader header;

                // 1. 读取包头
                int n = recv(client_fd, &header, sizeof(RpcHeader), 0);
                if (n <= 0) {
                    if (n < 0 && errno == EAGAIN) continue; 
                    std::cout << "[System] 客户端连接关闭 FD: " << client_fd << std::endl;
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                    continue;
                }

                // 字节序转换
                header.magic_number = ntohl(header.magic_number);
                header.version      = ntohl(header.version);
                header.body_len     = ntohl(header.body_len);
                header.type         = ntohl(header.type);

                if (header.magic_number != 0xCAFEBABE) {
                    std::cerr << "[Protocol Error] 非法魔数！可能存在残留数据干扰，强制清理 FD: " << client_fd << std::endl;
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                    continue;
                }

                // 2. 核心修复逻辑：阻塞式读满 Body
                setBlocking(client_fd); 

                std::vector<char> body_buf(header.body_len);
                int total_recv = 0;
                bool read_error = false;

                while (total_recv < (int)header.body_len) {
                    int r = recv(client_fd, body_buf.data() + total_recv, header.body_len - total_recv, 0);
                    if (r == 0) {
                        std::cerr << "[Debug] Body读取中对端意外关闭，已读: " << total_recv << std::endl;
                        read_error = true;
                        break;
                    } else if (r < 0) {
                        if (errno == EINTR) continue; // 信号中断，不算错
                        std::cerr << "[Debug] Body读取系统错误, errno: " << errno << std::endl;
                        read_error = true;
                        break;
                    }
                    total_recv += r;
                }

                // 将数据处理丢入线程池，主线程立即去处理下一个 Epoll 事件
                // 将 FD 从 Epoll 中摘除，防止其他线程触发重复事件
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);

                pool.enqueue([client_fd, header,&processor, body_buf = std::move(body_buf)]() {
                    std::cout << "[Thread " << std::this_thread::get_id() << "] 开始处理 FD: " << client_fd << std::endl;
                    
                    myrpc::ImageRequest req;
                    if (req.ParseFromArray(body_buf.data(), body_buf.size())) {
                        std::string processed_bytes;
                        if (processor.process(header.type, req.image_data(), processed_bytes)) {
                            
                            myrpc::ImageResponse resp;
                            resp.set_processed_data(processed_bytes);
                            resp.set_success(true);

                            std::string resp_str;
                            resp.SerializeToString(&resp_str);

                            RpcHeader resp_header;
                            resp_header.magic_number = htonl(0xCAFEBABE);
                            resp_header.version      = htonl(1);
                            resp_header.body_len     = htonl((uint32_t)resp_str.size());
                            resp_header.type         = htonl(header.type);

                            // Worker 线程负责写回响应（此时 client_fd 是阻塞模式）
                            send(client_fd, &resp_header, sizeof(RpcHeader), 0);
                            send(client_fd, resp_str.data(), resp_str.size(), 0);
                        }
                    }
                    // 处理完毕，Worker 线程负责关闭连接
                    close(client_fd);
                    std::cout << "[Thread " << std::this_thread::get_id() << "] 处理完毕并关闭 FD: " << client_fd << std::endl;
                });
            }
        }
    }
    return 0;
}