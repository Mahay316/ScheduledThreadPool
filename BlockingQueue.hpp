#ifndef SCHEDULEDTHREADPOOL_BLOCKINGQUEUE_HPP
#define SCHEDULEDTHREADPOOL_BLOCKINGQUEUE_HPP

#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class BlockingQueue {
private:
    std::queue<T> queue;
    size_t maxSize;
    std::mutex mtx;
    std::condition_variable condFull;
    std::condition_variable condEmpty;
public:
    explicit BlockingQueue(size_t maxSize = SIZE_MAX) : maxSize(maxSize) {}

    void put(const T& item) {
        std::unique_lock<std::mutex> lock(mtx);
//        condFull.wait(lock, [this]() { return queue.size() < maxSize; });

        // there is room for more items
        queue.push(item);
//        condEmpty.notify_one();
    }

    T take() {
        std::unique_lock<std::mutex> lock(mtx);
        condEmpty.wait(lock, [this]() { return !queue.empty(); });

        // there are items to be consumed
        T item = std::move(queue.front());
        queue.pop();
        condFull.notify_one();
        return item;
    }

    bool tryTake(T &res) {
        std::unique_lock<std::mutex> lock(mtx);
        if (!queue.empty()) {
            res = std::move(queue.front());
            queue.pop();
            return true;
        }

        return false;
    }

    size_t size() {
        std::unique_lock<std::mutex> lock(mtx);
        return queue.size();
    }

    bool empty() {
        std::unique_lock<std::mutex> lock(mtx);
        return queue.empty();
    }
};

#endif //SCHEDULEDTHREADPOOL_BLOCKINGQUEUE_HPP
