#ifndef SCHEDULEDTHREADPOOL_THREADPOOL_HPP
#define SCHEDULEDTHREADPOOL_THREADPOOL_HPP

#include <functional>
#include <future>
#include <iostream>
#include <thread>
#include <unordered_set>
#include <chrono>
#include "BlockingQueue.hpp"

namespace mahay {
    using TC = std::chrono::steady_clock;
    using TimePoint = std::chrono::steady_clock::time_point;

    class ThreadPool {
    private:
        BlockingQueue <std::function<void()>> workQueue{};
        std::unordered_set<std::thread::id> workers;
        std::unordered_set<std::thread::id> idleWorkers;

        uint32_t finishedCnt{0};

        bool isDestructing{false};

        std::condition_variable waitCV;

        std::mutex mtx;

        uint32_t coreThreadSize;
        uint32_t maxThreadSize;
        std::chrono::milliseconds keepAliveTime;

        // the function worker threads execute
        // temporary worker will be destroyed after a certain duration of idling
        void workerLogic(bool temporary = false) {
            std::function<void()> task;
            TimePoint idleStart;
            while (true) {
                if (workQueue.tryTake(task)) {
                    idleWorkers.erase(std::this_thread::get_id());
                    task();
                } else if (isDestructing) {
                    std::lock_guard<std::mutex> lock(mtx);
                    --finishedCnt;
                    waitCV.notify_one();
                    workers.erase(std::this_thread::get_id());
                    return;
                } else {
                    if (temporary) {
                        if (idleWorkers.count(std::this_thread::get_id())) {
                            // idle for enough time
                            if ((TC::now() - idleStart) >= keepAliveTime) {
                                return;
                            }
                        } else {
                            idleStart = TC::now();
                            idleWorkers.emplace(std::this_thread::get_id());
                        }
                    }
                    std::this_thread::yield();
                }

                /*if (shutdownCnt <= 0) {

                } else {
                    // it has to shut down shutdownCnt threads no matter what
                    std::lock_guard<std::mutex> lock(mtx);
                    // double-checking
                    if (shutdownCnt > 0) {
                        --shutdownCnt;
                        workers.erase(std::this_thread::get_id());
                        return;
                    }
                }*/
            }
        }

        // addWorker() is not synchronized
        // because its invoker is guaranteed to be synchronized
        void addWorker(bool temporary = false) {
            std::thread t(&ThreadPool::workerLogic, this, temporary);
            workers.emplace(t.get_id());
            t.detach();
        }

        // delWorker() is not synchronized
        // because its invoker is guaranteed to be synchronized
        /*void removeWorker() {
            if (shutdownCnt >= workers.size()) throw std::runtime_error("There is no more worker thread to delete.");

            ++shutdownCnt;
        }*/

    public:
        explicit ThreadPool(uint32_t core = 1, uint32_t ma = 1) : coreThreadSize(core), maxThreadSize(ma) {}

        ThreadPool(const ThreadPool &) = delete;

        ThreadPool(ThreadPool &&) = delete;

        ThreadPool &operator=(const ThreadPool &) = delete;

        ThreadPool &operator=(ThreadPool &&) = delete;

        template<typename F, typename... Args>
        auto submit(F &&f, Args &&...args) -> std::future<decltype(f(args...))> {
            if (isDestructing) {
                throw std::runtime_error("Could not submitted tasks to a closing/closed thread pool!");
            }

            // deduce the return type of the submitted task
            using RET = decltype(f(args...));
            // the type of the submitted task after binding
            using FUNC = RET();

            std::function<FUNC> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
            auto taskPtr = std::make_shared<std::packaged_task<FUNC>>(func);

            adjustWorkerNumber();

            workQueue.put([taskPtr] {
                (*taskPtr)();
            });

            return taskPtr->get_future();
        }

        // 1. when worker number < coreThreadSize, it creates a new worker for every submitted task
        // 2. when coreThreadSize <= worker number < maxThreadSize, it creates a new worker if other threads are busy
        // 3. when worker number >= coreThreadSize, its destroys a random worker if there is idle workers
        void adjustWorkerNumber() {
            std::lock_guard<std::mutex> lock(mtx);
            const auto workNum = workers.size();
            if (workNum < coreThreadSize) {
                addWorker();
            } else if (workNum < maxThreadSize && idleWorkers.empty()) {
                addWorker(true);
            }
        }

        // wait until all submitted tasks to finish and then close the thread pool
        // reject any newly created tasks
        // blocking
        void shutDown() {
            std::unique_lock<std::mutex> lock(mtx);

            if (isDestructing) throw std::runtime_error("This thread pool has already been shut down.");

            isDestructing = true;
            finishedCnt = workers.size();
            waitCV.wait(lock, [this] { return finishedCnt == 0; });
        }

        // close the thread pool without waiting all submitted tasks to finish
        // non-blocking
        void shutDownNow() {
            std::lock_guard<std::mutex> lock(mtx);

            if (isDestructing) throw std::runtime_error("This thread pool has already been shut down.");

            isDestructing = true;
//            shutdownCnt = workers.size();
        }
    };
}

#endif //SCHEDULEDTHREADPOOL_THREADPOOL_HPP
