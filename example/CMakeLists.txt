# 示例源文件
set(EXAMPLE_SOURCES
    TcpClient.cpp
    TcpServer.cpp
    UdpServer.cpp
)

# 添加头文件路径
include_directories(${CMAKE_SOURCE_DIR}/src)

# 添加可执行文件
foreach(EXAMPLE_SOURCE ${EXAMPLE_SOURCES})
    # 提取源文件名
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_SOURCE} NAME_WE)

    # 定义目标
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_SOURCE})

    # 链接动态库和静态库（可以选择其一）
    target_link_libraries(${EXAMPLE_NAME} PRIVATE NetworkLibShared)
endforeach()
