#include <iostream>
#include "TimeWheel.hpp"
#include "ThreadPool.hpp"

using namespace std;
using namespace mahay;

int add(int a, int b) {
    this_thread::sleep_for(5s);
    return a + b;
}

int main() {
    /*ThreadPool pool(1, 2);
    auto res = pool.submit(add, 1, 3);
    auto res2 = pool.submit([](int a, int b) { return a * b; }, 2, 3);
    cout << res2.get() << endl;
    cout << res.get() << endl;

    pool.shutDown();*/

    TimeWheel &timer = TimeWheel::getInstance();
    TimePoint start = chrono::steady_clock::now();
    timer.schedulePeriod("hello", 4000, 1000, [&start](int a) {
        cout << "Hello " << a << "  ";
        cout << chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - start).count() << endl;
        }, 2);

    std::this_thread::sleep_for(100h);

    return 0;
}
