// engine/core/include/hue/core/input.h
//
// Input abstraction (Week 1 spec): keyboard, mouse, gamepad behind one
// snapshot API. update() is called once per frame after poll_events();
// gameplay reads immutable state + edges (pressed/released this frame).
// Key/button codes mirror GLFW's stable values so the header stays free of
// GLFW includes; the .cpp static_asserts the mirror never drifts.

#pragma once

#include <cstdint>

namespace hue {

class Window;

namespace key {
// Subset of the keyboard used by engine/game code; extend as needed.
inline constexpr int kSpace = 32;
inline constexpr int kA = 65;
inline constexpr int kD = 68;
inline constexpr int kS = 83;
inline constexpr int kW = 87;
inline constexpr int kEscape = 256;
inline constexpr int kRight = 262;
inline constexpr int kLeft = 263;
inline constexpr int kDown = 264;
inline constexpr int kUp = 265;
inline constexpr int kLeftShift = 340;
inline constexpr int kLeftControl = 341;
} // namespace key

namespace mouse {
inline constexpr int kLeft = 0;
inline constexpr int kRight = 1;
inline constexpr int kMiddle = 2;
} // namespace mouse

namespace pad {
// Buttons (GLFW gamepad mapping: Xbox layout)
inline constexpr int kA = 0;
inline constexpr int kB = 1;
inline constexpr int kX = 2;
inline constexpr int kY = 3;
inline constexpr int kLeftBumper = 4;
inline constexpr int kRightBumper = 5;
inline constexpr int kBack = 6;
inline constexpr int kStart = 7;
inline constexpr int kLeftThumb = 9;
inline constexpr int kRightThumb = 10;
inline constexpr int kDpadUp = 11;
inline constexpr int kDpadRight = 12;
inline constexpr int kDpadDown = 13;
inline constexpr int kDpadLeft = 14;
// Axes
inline constexpr int kAxisLeftX = 0;
inline constexpr int kAxisLeftY = 1;
inline constexpr int kAxisRightX = 2;
inline constexpr int kAxisRightY = 3;
inline constexpr int kAxisLeftTrigger = 4;
inline constexpr int kAxisRightTrigger = 5;
} // namespace pad

struct GamepadState {
    bool connected = false;
    char name[64] = {};
    float axes[6] = {};      // -1..1 sticks, -1..1 triggers (GLFW convention)
    bool buttons[15] = {};
};

class Input {
public:
    static constexpr int kKeyCount = 349;    // GLFW_KEY_LAST + 1
    static constexpr int kMouseCount = 8;    // GLFW_MOUSE_BUTTON_LAST + 1

    // Snapshot current device state; previous snapshot becomes the edge base.
    void update(const Window& window);

    bool key_down(int k) const { return valid_key(k) && m_keys[k]; }
    bool key_pressed(int k) const { return valid_key(k) && m_keys[k] && !m_prev_keys[k]; }
    bool key_released(int k) const { return valid_key(k) && !m_keys[k] && m_prev_keys[k]; }

    bool mouse_down(int b) const { return valid_mouse(b) && m_mouse[b]; }
    bool mouse_pressed(int b) const { return valid_mouse(b) && m_mouse[b] && !m_prev_mouse[b]; }
    double mouse_x() const { return m_mouse_x; }
    double mouse_y() const { return m_mouse_y; }
    double mouse_dx() const { return m_mouse_x - m_prev_mouse_x; }
    double mouse_dy() const { return m_mouse_y - m_prev_mouse_y; }

    const GamepadState& gamepad() const { return m_pad; }
    bool pad_pressed(int b) const {
        return b >= 0 && b < 15 && m_pad.buttons[b] && !m_prev_pad.buttons[b];
    }

private:
    static bool valid_key(int k) { return k >= 0 && k < kKeyCount; }
    static bool valid_mouse(int b) { return b >= 0 && b < kMouseCount; }

    bool m_keys[kKeyCount] = {};
    bool m_prev_keys[kKeyCount] = {};
    bool m_mouse[kMouseCount] = {};
    bool m_prev_mouse[kMouseCount] = {};
    double m_mouse_x = 0, m_mouse_y = 0;
    double m_prev_mouse_x = 0, m_prev_mouse_y = 0;
    GamepadState m_pad;
    GamepadState m_prev_pad;
};

} // namespace hue
