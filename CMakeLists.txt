cmake_minimum_required(VERSION 3.10)
project(CoreHeartbeat CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(CoreRelay CoreRelay.cpp)
add_executable(Heartbeat Heartbeat.cpp)
add_executable(Dummy Dummy.cpp)

if(WIN32)
    target_link_libraries(CoreRelay ws2_32)
    target_link_libraries(Heartbeat ws2_32)
    target_link_libraries(Dummy ws2_32)
else()
    target_link_libraries(CoreRelay pthread)
    target_link_libraries(Heartbeat pthread)
    target_link_libraries(Dummy pthread)
endif()
