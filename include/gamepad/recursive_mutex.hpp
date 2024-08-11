#include "pros/apix.h"
#include "pros/rtos.h"
#include <system_error>

namespace Gamepad::_impl {

class RecursiveMutex {
    public:
        RecursiveMutex()
            : mutex(pros::c::mutex_recursive_create()) {}

        bool take(std::uint32_t timeout = TIMEOUT_MAX) { return pros::c::mutex_recursive_take(mutex, timeout); }

        void lock() {
            while (!this->take()) pros::delay(2);
        }

        bool try_lock() { return this->take(0); }

        bool give() { return pros::c::mutex_recursive_give(mutex); }

        void unlock() { this->give(); }
    private:
        pros::mutex_t mutex;
};

} // namespace Gamepad::_impl