## 使用方式

### API
1. void TimeWheel::**schedulePeriod**(const std::string &tag, uint32_t delay, uint32_t period, F &&f, Args &&...args) 创建周期执行任务，首次延迟delay毫秒，之后每period毫秒执行一次
2. void TimeWheel::**scheduleDelay**(const std::string &tag, uint32_t delay, F &&f, Args &&...args) 创建延迟执行任务，延迟执行delay毫秒

### 示例
```C++
#include <iostream>
#include "TimeWheel.hpp"
#include "ThreadPool.hpp"

using namespace std;
using namespace mahay;

int main() {
    TimeWheel &timer = TimeWheel::getInstance();
    TimePoint start = chrono::steady_clock::now();
    timer.schedulePeriod("hello", 4000, 1000, [&start](int a) {
        cout << "Hello " << a << "  ";
        cout << chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - start).count() << endl;
        }, 2);

    std::this_thread::sleep_for(100h);

    return 0;
}
```
