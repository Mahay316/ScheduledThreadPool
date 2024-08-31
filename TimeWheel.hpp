#ifndef SCHEDULEDTHREADPOOL_TIMEWHEEL_HPP
#define SCHEDULEDTHREADPOOL_TIMEWHEEL_HPP

#include <chrono>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <utility>
#include <future>
#include "TimeHolder.hpp"
#include "ThreadPool.hpp"

// the number of slots that the active wheel has
#define ATW_BITS 8
#define ATW_SIZE (1 << ATW_BITS)

// the number of slots that a passive wheel has
#define PTW_BITS 6
#define PTW_SIZE (1 << PTW_BITS)

// the number of passive wheels
#define PTW_COUNT 4

// time is encoded (concatenated) into a 32-bit unsigned integer
// ATW_MASK and PTW_MASK are used to retrieve values for certain time wheel
#define ATW_MASK (ATW_SIZE - 1)
#define PTW_MASK (PTW_SIZE - 1)
#define FIRST_INDEX(v) ((v) & ATW_MASK)
#define NTH_INDEX(v, n) (((v) >> (ATW_BITS + (n - 1) * PTW_BITS)) & PTW_MASK)

// time unit in milliseconds
#define TIME_UNIT 50

namespace mahay {
    class Timer;

    using MilliSec = std::chrono::milliseconds;
    using TimePoint = std::chrono::steady_clock::time_point;
    using TaskList = std::list<std::shared_ptr<Timer>>;
    using Callback = std::function<void()>;

    class Timer {
    private:
        const std::string tag;

        // timeout when the task is executed for the first time
        uint32_t delay;

        // periodically executed timeout, set to 0 if the task is not looped
        uint32_t period;

        Callback callback;
        const bool loopFlag;

        TimePoint timerGenesis;

        // calculate the proper position (slot indices)
        // where a task should reside
        void calcWheelSeq(uint32_t timeout) {
            TimePoint endTime = MilliSec(timeout) + std::chrono::steady_clock::now();
            uint32_t gap = std::chrono::duration_cast<MilliSec>(endTime - timerGenesis).count() / TIME_UNIT;
            wheelSeq[0] = FIRST_INDEX(gap);
            for (int i = 1; i <= 4; ++i)
                wheelSeq[i] = NTH_INDEX(gap, i);
        }

    public:
        std::array<uint8_t, 5> wheelSeq{};

        // disable copy/move constructor and copy/move assignment operator
        Timer(const Timer &) = delete;

        Timer(Timer &&) = delete;

        Timer &operator=(const Timer &) = delete;

        Timer &operator=(Timer &&) = delete;

        Timer(std::string tag, uint32_t _delay, uint32_t _period, bool loop, Callback &&func, TimePoint genesis)
                : tag(std::move(tag)),
                  delay(_delay),
                  period(_period),
                  loopFlag(loop),
                  callback(std::move(func)),
                  timerGenesis(genesis) {
            calcWheelSeq(delay);
        }

        Callback getCallback() const { return callback; }

        bool isLoop() const { return loopFlag; }

        void doLoop() {
            calcWheelSeq(period);
        }

        const std::string &getTag() const { return tag; }
    };

    template<int SLOT>
    class SingleTimeWheel {
    private:
        uint8_t currSlot{0};
        std::array<TaskList, SLOT> slots;

        TaskList popTasks(int slotIdx) {
            TaskList res;
            // TODO: throws an exception
            if (slotIdx >= SLOT) return res;

            slots[slotIdx].swap(res);
            return res;
        }

    public:
        /**
         * move the currSlot forward by 1 and pop out due tasks
         * @return a list of due tasks and a boolean indicating whether to carry
         */
        std::pair<TaskList, bool> tick() {
            currSlot = (currSlot + 1) % SLOT;
            TaskList &&res = popTasks(currSlot);
            return std::make_pair(res, currSlot == 0);
        }

        void add(uint8_t idx, std::shared_ptr<Timer> &&task) {
            if (idx >= SLOT) return;

            slots[idx].emplace_front(std::move(task));
        }

        uint8_t current() const {
            return currSlot;
        }
    };

    class TimeWheel {
    private:
        // Time Point when the timer was created
        const TimePoint genesis{std::chrono::steady_clock::now()};

        // flag to indicate whether the timer is alive
        std::atomic_bool alive{true};

        std::recursive_mutex mtx;
        std::unordered_map<std::string, std::shared_ptr<Timer>> taskMap;

        SingleTimeWheel<ATW_SIZE> activeWheel{};
        std::array<SingleTimeWheel<PTW_SIZE>, PTW_COUNT> passiveWheels{};

        ThreadPool pool{10, 16};

        TimeWheel() {
            // start the counting thread
            std::thread th([this]() {
                TimeHolder holder;
                uint32_t times = 1;
                while (alive) {
                    if (times == 0) {
                        holder.reset();
                        times = 1;
                    }
                    tick();
                    holder.hold(MilliSec(TIME_UNIT) * times++);
                }
            });
            th.detach();
        }

        ~TimeWheel() {
            pool.shutDown();
        }

        void tick() {
            std::lock_guard<std::recursive_mutex> g(mtx);
            auto &&res = activeWheel.tick();
            auto &tasks = res.first;
            for (auto &t: tasks) {
                // the task has already been deleted
                if (t.unique()) continue;

                // execute the task
                // t->getCallback()();
                pool.submit(t->getCallback());

                if (t->isLoop()) {
                    t->doLoop(); // re-calculate the wheel sequence
                    _reschedule(std::move(t));
                } else {
                    taskMap.erase(t->getTag());
                }
            }

            // carry to the next level
            if (res.second) {
                for (int i = 0; i < 4; i++) {
                    auto &&res2 = passiveWheels[i].tick();
                    TaskList &taskList2 = res.first;
                    for (auto &t: taskList2) {
                        // move the task to the next (lower) level
                        loadTimer(std::move(t));
                    }
                    if (!res.second) break;
                }
            }
        }

        // load the task onto proper time wheel
        void loadTimer(std::shared_ptr<Timer> &&task) {
            const std::array<uint8_t, 5> &wheelSeq = task->wheelSeq;
            bool processed = false;
            for (int i = 3; i >= 0; --i) {
                if (wheelSeq[i + 1] != 0 && wheelSeq[i + 1] != passiveWheels[i].current()) {
                    passiveWheels[i].add(wheelSeq[i + 1], std::move(task));
                    processed = true;
                    break;
                }
            }
            if (!processed) activeWheel.add(wheelSeq[0], std::move(task));
        }

        // re-add the task
        // there is no need to construct the Timer object again
        // this function may be used when moving or looping the task
        void _reschedule(std::shared_ptr<Timer> &&task) {
            loadTimer(std::move(task));
        }

        template<typename F, typename... Args>
        void _schedule(const std::string &tag, uint32_t delay, uint32_t period, bool loop, F &&f, Args &&...args) {
            std::lock_guard<std::recursive_mutex> g(mtx);

//            auto func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
            auto task = [=] {
                f(args...);
            };

            auto timerTask = std::make_shared<Timer>(tag, delay, period, loop, std::move(task), genesis);
            taskMap.emplace(tag, timerTask);
            loadTimer(std::move(timerTask));
        }

    public:
        // disable copy/move constructor and copy/move assignment operator
        TimeWheel(const TimeWheel &) = delete;

        TimeWheel(TimeWheel &&) = delete;

        TimeWheel &operator=(const TimeWheel &) = delete;

        TimeWheel &operator=(TimeWheel &&) = delete;

        // singleton
        static TimeWheel &getInstance() {
            // C++ 11 magic static makes sure that the following code is
            // lazy and thread-safe
            static TimeWheel inst;
            return inst;
        }

        // destroy the timer and stop the counting thread
        void destroy() {
            alive = false;
            taskMap.clear();
        }

        // execute the callback after delay ms
        // then periodically execute it every period ms
        template<typename F, typename... Args>
        void schedulePeriod(const std::string &tag, uint32_t delay, uint32_t period, F &&f, Args &&...args) {
            _schedule(tag, delay, period, true, std::forward<F>(f), std::forward<Args>(args)...);
        }

        // execute the callback after delay ms
        template<typename F, typename... Args>
        void scheduleDelay(const std::string &tag, uint32_t delay, F &&f, Args &&...args) {
            _schedule(tag, delay, 0, true, std::forward<F>(f), std::forward<Args>(args)...);
        }

        void removeTimer(const std::string &tag) {
            std::lock_guard<std::recursive_mutex> g(mtx);
            taskMap.erase(tag);
        }
    };
}

#endif //SCHEDULEDTHREADPOOL_TIMEWHEEL_HPP
