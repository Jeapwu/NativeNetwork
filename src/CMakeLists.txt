# 添加 src 目录中的源文件
set(SOURCES
    impl/listener/TcpListener.cpp
    impl/socket/UdpSocket.cpp
    impl/stream/TcpStream.cpp
)

# 包含头文件目录
set(INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/listener
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/socket
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/stream
)

# 添加头文件路径
include_directories(${INCLUDE_DIRS})

# 动态库
add_library(NetworkLibShared SHARED ${SOURCES})
target_include_directories(NetworkLibShared PUBLIC ${INCLUDE_DIRS})
set_target_properties(NetworkLibShared PROPERTIES OUTPUT_NAME "NativeNetwork")

# 静态库
add_library(NetworkLibStatic STATIC ${SOURCES})
target_include_directories(NetworkLibStatic PUBLIC ${INCLUDE_DIRS})
set_target_properties(NetworkLibStatic PROPERTIES OUTPUT_NAME "NativeNetwork")
