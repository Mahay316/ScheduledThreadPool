cmake_minimum_required(VERSION 3.23)
project(ScheduledThreadPool)

set(CMAKE_CXX_STANDARD 17)

add_executable(ScheduledThreadPool main.cpp
        ThreadPool.hpp
        BlockingQueue.hpp
        TimeWheel.hpp
        TimeHolder.hpp
)
