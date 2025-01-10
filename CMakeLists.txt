cmake_minimum_required(VERSION 3.10)

# 设置项目名称和版本
project(NetworkLibrary VERSION 1.0 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 配置不同平台的源文件
if(WIN32)
    # Windows 平台
    set(TCP_STREAM_IMPL_SRC src/TcpStreamImplWindows.cpp)
elseif(APPLE)
    # macOS 平台
    set(TCP_STREAM_IMPL_SRC src/TcpStreamImplMac.cpp)
else()
    # Linux 平台 (或其他 Unix-like 系统)
    set(TCP_STREAM_IMPL_SRC src/TcpStreamImplLinux.cpp)
endif()

# 包含头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 创建网络库的静态库
add_library(NetworkLibrary STATIC
    src/TcpStream.cpp
    ${TCP_STREAM_IMPL_SRC}
)

# 查找并链接平台相关的库
if(WIN32)
    target_link_libraries(NetworkLibrary PRIVATE ws2_32)
elseif(APPLE)
    # 在 macOS 上可能需要链接到 system 库
    target_link_libraries(NetworkLibrary PRIVATE "-framework CoreFoundation")
else()
    # 在 Linux 上，可能需要链接到 pthread 和其他网络相关库
    find_package(Threads REQUIRED)
    target_link_libraries(NetworkLibrary PRIVATE ${CMAKE_THREAD_LIBS_INIT})
    target_link_libraries(NetworkLibrary PRIVATE rt)
endif()

# 设置目标输出目录
set_target_properties(NetworkLibrary PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib
)

# 示例：安装规则
install(TARGETS NetworkLibrary DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

# 如果你要创建可执行文件（例如一个测试程序）
# add_executable(NetworkTest src/NetworkTest.cpp)
# target_link_libraries(NetworkTest PRIVATE NetworkLibrary)
