cmake_minimum_required(VERSION 3.10)
project(ImageRpcServer)

set(CMAKE_CXX_STANDARD 17)

# 1. 查找依赖库
find_package(Protobuf REQUIRED)
find_package(OpenCV REQUIRED)

# 2. 包含头文件目录
include_directories(include src ${PROTOBUF_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})

# 3. 指定源文件
set(SOURCES 
    src/main.cpp 
    src/message.pb.cc
    src/processor.cpp
)

# 4. 生成可执行程序
add_executable(server ${SOURCES})

# 5. 链接库
target_link_libraries(server 
    ${PROTOBUF_LIBRARIES} 
    ${OpenCV_LIBS}
    pthread
)