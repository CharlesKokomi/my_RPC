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
#include <string>
#define MAX_EVENTS 1024
#define PORT 9000
void startHeartbeat(std::string ip, int port) {
    // 开启一个后台线程，不阻塞主程序的 epoll 循环
    std::thread([=]() {
        std::string gist_id = "8991b3af79c3b056768177e4bb84156f";
        std::string token = "";
        
        // 为当前服务器生成一个唯一的 Key，例如 "server_123_45_67_89"
        // 这样多台服务器同时运行就不会互相覆盖，而是各自占据 Map 的一个 Key
        std::string server_key = "server_" + ip;
        std::replace(server_key.begin(), server_key.end(), '.', '_'); // 把 IP 里的点换成下划线

        while (true) {
            // 1. 获取当前 Unix 时间戳
            long long now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            // 2. 构造 JSON 负载
            // 注意：这里使用了 GitHub API 的 PATCH 方法。
            // 我们通过 "files" -> "nodes.json" -> "content" 来更新文件内容。
            // 这里的转义比较复杂，核心是构造出：{"ip":"x.x.x.x", "port":9000, "last_heartbeat":123456}
            std::string content = "{\\\\\\\"ip\\\\\\\":\\\\\\\"" + ip + "\\\\\\\",\\\\\\\"port\\\\\\\":" 
                                  + std::to_string(port) + ",\\\\\\\"last_heartbeat\\\\\\\":" 
                                  + std::to_string(now) + "}";
            
            // 为了支持多节点，我们需要在 Gist 里保留其他服务器的信息。
            // 简单做法是每次 PATCH 只更新属于自己的那个片段（GitHub API 会合并内容）
            std::string json_payload = "{\\\"files\\\":{\\\"nodes.json\\\":{\\\"content\\\":\\\"{\\\\\\\"" 
                                       + server_key + "\\\\\\\":" + content + "}\\\"} } }";

            // 3. 构造 curl 命令
            std::string cmd = "curl -L -X PATCH "
                              "-H \"Authorization: token " + token + "\" "
                              "-H \"Accept: application/vnd.github.v3+json\" "
                              "-d \"" + json_payload + "\" "
                              "https://api.github.com/gists/" + gist_id + " > /dev/null 2>&1";

            // 执行命令
            int ret = system(cmd.c_str());
            
            if (ret == 0) {
                std::cout << "[Heartbeat] 已续约，时间戳: " << now << std::endl;
            } else {
                std::cerr << "[Heartbeat] 续约失败，检查网络或 Token" << std::endl;
            }

            // 4. 每 30 秒跳动一次
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    }).detach(); // 使用 detach 让线程在后台独立运行
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
    startHeartbeat("139.159.140.251",9000);
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

                pool.enqueue([client_fd, header, body_buf = std::move(body_buf)]() {
                    std::cout << "[Thread " << std::this_thread::get_id() << "] 开始处理 FD: " << client_fd << std::endl;
                    
                    myrpc::ImageRequest req;
                    if (req.ParseFromArray(body_buf.data(), body_buf.size())) {
                        std::string processed_bytes;
                        if (ImageProcessor::process(header.type, req.image_data(), processed_bytes)) {
                            
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