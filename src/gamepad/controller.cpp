#include "gamepad/controller.hpp"
#include "gamepad/todo.hpp"
#include "pros/misc.h"
#include "pros/rtos.hpp"
#include <atomic>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Gamepad {
bool Button::onPress(std::string listenerName, std::function<void(void)> func) const {
    return this->onPressEvent.add_listener(std::move(listenerName) + "_user", std::move(func));
}

bool Button::onLongPress(std::string listenerName, std::function<void(void)> func) const {
    return this->onLongPressEvent.add_listener(std::move(listenerName) + "_user", std::move(func));
}

bool Button::onRelease(std::string listenerName, std::function<void(void)> func) const {
    return this->onReleaseEvent.add_listener(std::move(listenerName) + "_user", std::move(func));
}

bool Button::onShortRelease(std::string listenerName, std::function<void(void)> func) const {
    return this->onShortReleaseEvent.add_listener(std::move(listenerName) + "_user", std::move(func));
}

bool Button::addListener(EventType event, std::string listenerName, std::function<void(void)> func) const {
    switch (event) {
        case Gamepad::EventType::ON_PRESS: return this->onPress(std::move(listenerName), std::move(func));
        case Gamepad::EventType::ON_LONG_PRESS: return this->onLongPress(std::move(listenerName), std::move(func));
        case Gamepad::EventType::ON_RELEASE: return this->onRelease(std::move(listenerName), std::move(func));
        case Gamepad::EventType::ON_SHORT_RELEASE:
            return this->onShortRelease(std::move(listenerName), std::move(func));
        default:
            TODO("add error logging")
            errno = EINVAL;
            return false;
    }
}

bool Button::removeListener(std::string listenerName) const {
    return this->onPressEvent.remove_listener(listenerName + "_user") ||
           this->onLongPressEvent.remove_listener(listenerName + "_user") ||
           this->onReleaseEvent.remove_listener(listenerName + "_user") ||
           this->onShortReleaseEvent.remove_listener(listenerName + "_user");
}

void Button::update(const bool is_held) {
    this->rising_edge = !this->is_pressed && is_held;
    this->falling_edge = this->is_pressed && !is_held;
    this->is_pressed = is_held;
    if (is_held) this->time_held += pros::millis() - this->last_update_time;
    else this->time_released += pros::millis() - this->last_update_time;

    if (this->rising_edge) {
        this->onPressEvent.fire();
    } else if (this->is_pressed && this->time_held >= this->long_press_threshold &&
               this->last_long_press_time <= pros::millis() - this->time_held) {
        this->onLongPressEvent.fire();
        this->last_long_press_time = pros::millis();
    } else if (this->falling_edge) {
        this->onReleaseEvent.fire();
        if (this->time_held < this->long_press_threshold) this->onShortReleaseEvent.fire();
    }
    if (this->rising_edge) this->time_held = 0;
    if (this->falling_edge) this->time_released = 0;
    this->last_update_time = pros::millis();
}

void Controller::updateButton(pros::controller_digital_e_t button_id) {
    Button Controller::*button = Controller::button_to_ptr(button_id);
    bool is_held = this->controller.get_digital(button_id);
    (this->*button).update(is_held);
}

void Controller::updateScreen() {
    // Lock Mutexes for Thread Safety
    std::lock_guard<pros::Mutex> guard_scheduling(this->scheduling_mut);
    std::lock_guard<pros::Mutex> guard_print(this->print_mut);
    std::lock_guard<pros::Mutex> guard_alert(this->alert_mut);

    // Check if enough time has passed for the controller to poll for updates
    if (pros::millis() - this->last_print_time < 50)
        return;

    for (int i = 1; i <= 4; i++) {
        // start from the line thats after the line thats been set so we dont get stuck setting the first line
        int line = (this->last_printed_line + i) % 4;

        // if the last alert's duration expired
        if (pros::millis() - this->line_set_time[line] >= this->screen_contents[line].duration) {
            // No alerts to print
            if (this->screen_buffer[line].size() == 0) {
                // text on screen is the same as last frame's text so no use updating
                if (this->screen_contents[line].text == this->next_print[line] && line != 3) {
                    this->next_print[line] = "";
                    continue;
                }
                // UPDATE TEXT/RUMBLE
                if (line == 3) this->controller.rumble(this->next_print[line].c_str());
                else this->controller.set_text(line, 0, this->next_print[line] + std::string(40, ' '));
                this->screen_contents[line].text = std::move(this->next_print[line]);
                this->next_print[line] = "";
            } else {
                // text on screen is the same as the alert's text so just set vars, dont update controller
                if (this->screen_contents[line].text == this->screen_buffer[line][0].text && line != 3) {
                    this->screen_contents[line] = this->screen_buffer[line][0];
                    this->screen_buffer[line].pop_front();
                    this->line_set_time[line] = pros::millis();
                    continue;
                }

                // SET ALERT/RUMBLE ALERT
                if (line == 3) this->controller.rumble(this->screen_buffer[line][0].text.c_str());
                else this->controller.set_text(line, 0, this->screen_buffer[line][0].text + std::string(40, ' '));
                this->screen_contents[line] = this->screen_buffer[line][0];
                this->screen_buffer[line].pop_front();
                this->line_set_time[line] = pros::millis();
            }
            this->last_printed_line = line;
            this->last_print_time = pros::millis();
        } else if (this->screen_contents[line].text == "") {
            // text is the same as last frame's text so no use updating
            if (this->screen_contents[line].text == this->next_print[line] && line != 3) {
                this->next_print[line] = "";
                continue;
            }

            // UPDATE TEXT/RUMBLE
            if (line == 3) this->controller.rumble(this->next_print[line].c_str());
            else this->controller.set_text(line, 0, this->next_print[line] + std::string(40, ' '));
            this->screen_contents[line].text = std::move(this->next_print[line]);
            this->next_print[line] = "";
            this->last_printed_line = line;
            this->last_print_time = pros::millis();
        }
    }
}

void Controller::update() {
    for (int i = pros::E_CONTROLLER_DIGITAL_L1; i <= pros::E_CONTROLLER_DIGITAL_A; ++i) {
        this->updateButton(static_cast<pros::controller_digital_e_t>(i));
    }

    this->m_LeftX = this->controller.get_analog(pros::E_CONTROLLER_ANALOG_LEFT_X);
    this->m_LeftY = this->controller.get_analog(pros::E_CONTROLLER_ANALOG_LEFT_Y);
    this->m_RightX = this->controller.get_analog(pros::E_CONTROLLER_ANALOG_RIGHT_X);
    this->m_RightY = this->controller.get_analog(pros::E_CONTROLLER_ANALOG_RIGHT_Y);

    this->updateScreen();
}

uint Controller::getTotalDuration(uint8_t line) {
    uint total = 0; 
    for (Line msg : this->screen_buffer[line])
        total += msg.duration;
    return total;
}

void Controller::add_alert(uint8_t line, std::string str, uint32_t duration, std::string rumble) {
    TODO("change handling for off screen lines")
    if (line > 2) std::exit(1);

    if (str.find('\n') != std::string::npos) {
        TODO("warn instead of throw error if there are too many lines")
        if (std::ranges::count(str, '\n') > 2) std::exit(1);

        std::vector<std::string> strs(3);
        std::stringstream ss(str);

        for (int i = line; i < 3; i++) {
            if (!std::getline(ss, strs[i], '\n')) break;
        }

        add_alerts(strs, duration, rumble);
        return;
    }

    std::lock_guard<pros::Mutex> guard(this->alert_mut);
    this->screen_buffer[line].push_back({ .text = std::move(str), .duration = duration });
}

void Controller::add_alerts(std::vector<std::string> strs, uint32_t duration, std::string rumble) {
    TODO("change handling for too many lines")
    if (strs.size() > 3) std::exit(1);

    std::lock_guard<pros::Mutex> guard(this->alert_mut);

    // get next available time slot for all lines
    uint minSpot = -1; // max uint value
    for (uint8_t line = 0; line < 4; line++) minSpot = std::min(minSpot, getTotalDuration(line));

    // Schedule alerts
    for (int i = 0; i < 4; i++) {
        // add delay until theres a spot for all lines together
        if (getTotalDuration(i) < minSpot)
            this->screen_buffer[i].push_back({ .text = "", .duration = (minSpot - getTotalDuration(i)) });

        if (i == 3) this->screen_buffer[i].push_back({.text = std::move(rumble), .duration = duration});
        else this->screen_buffer[i].push_back({.text = std::move(strs[i]), .duration = 0});
    }
}

void Controller::print_line(uint8_t line, std::string str) {
    TODO("change handling for off screen lines")
    if (line > 2) std::exit(1);

    std::lock_guard<pros::Mutex> guard(this->print_mut);

    if (str.find('\n') != std::string::npos) {
        TODO("warn instead of throw error if there are too many lines")
        if (std::ranges::count(str, '\n') > 2) std::exit(1);

        std::vector<std::string> strs(3);
        std::stringstream ss(str);

        for (int i = line; i < 3; i++) {
            if (!std::getline(ss, strs[i], '\n')) break;
        }

        for (uint8_t l = 0; l < 3; l++)
            this->next_print[l] = std::move(strs[l]);
        return;
    }

    this->next_print[line] = std::move(str);
}

void Controller::rumble(std::string rumble_pattern) {
    TODO("change handling for too long rumble patterns")
    if (rumble_pattern.size() > 8) std::exit(1);

    std::lock_guard<pros::Mutex> guard(this->print_mut);
    this->next_print[3] = std::move(rumble_pattern);
}

const Button& Controller::operator[](pros::controller_digital_e_t button) {
    return this->*Controller::button_to_ptr(button);
}

float Controller::operator[](pros::controller_analog_e_t axis) {
    switch (axis) {
        case pros::E_CONTROLLER_ANALOG_LEFT_X: return this->LeftX;
        case pros::E_CONTROLLER_ANALOG_LEFT_Y: return this->LeftY;
        case pros::E_CONTROLLER_ANALOG_RIGHT_X: return this->RightX;
        case pros::E_CONTROLLER_ANALOG_RIGHT_Y: return this->RightY;
        default:
            TODO("add error logging")
            errno = EINVAL;
            return 0;
    }
}

std::string Controller::unique_name() {
    static std::atomic<uint32_t> i = 0;
    return std::to_string(i++) + "_internal";
}

Button Controller::*Controller::button_to_ptr(pros::controller_digital_e_t button) {
    switch (button) {
        case pros::E_CONTROLLER_DIGITAL_L1: return &Controller::m_L1;
        case pros::E_CONTROLLER_DIGITAL_L2: return &Controller::m_L2;
        case pros::E_CONTROLLER_DIGITAL_R1: return &Controller::m_R1;
        case pros::E_CONTROLLER_DIGITAL_R2: return &Controller::m_R2;
        case pros::E_CONTROLLER_DIGITAL_UP: return &Controller::m_Up;
        case pros::E_CONTROLLER_DIGITAL_DOWN: return &Controller::m_Down;
        case pros::E_CONTROLLER_DIGITAL_LEFT: return &Controller::m_Left;
        case pros::E_CONTROLLER_DIGITAL_RIGHT: return &Controller::m_Right;
        case pros::E_CONTROLLER_DIGITAL_X: return &Controller::m_X;
        case pros::E_CONTROLLER_DIGITAL_B: return &Controller::m_B;
        case pros::E_CONTROLLER_DIGITAL_Y: return &Controller::m_Y;
        case pros::E_CONTROLLER_DIGITAL_A: return &Controller::m_A;
        default:
            TODO("add error logging")
            errno = EINVAL;
            return &Controller::Fake;
    }
}
} // namespace Gamepad
