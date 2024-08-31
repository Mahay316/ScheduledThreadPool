#ifndef SCHEDULEDTHREADPOOL_TIMEHOLDER_HPP
#define SCHEDULEDTHREADPOOL_TIMEHOLDER_HPP

#include <chrono>
#include <thread>

namespace mahay
{
    class TimeHolder
    {
    public:
        TimeHolder() : t(std::chrono::steady_clock::now()) {}

        void hold(std::chrono::milliseconds timeout) {
            std::this_thread::sleep_until(timeout + t);
        }

        void reset(){
            t = std::chrono::steady_clock::now();
        }
    private:
        std::chrono::steady_clock::time_point t;
    };
}


#endif //SCHEDULEDTHREADPOOL_TIMEHOLDER_HPP
