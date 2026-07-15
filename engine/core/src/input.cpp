// engine/core/src/input.cpp

#include "hue/core/input.h"

#include "hue/core/trace.h"
#include "hue/core/window.h"

#include <GLFW/glfw3.h>

#include <cstring>

namespace hue {

// The public header mirrors GLFW codes instead of including GLFW; if GLFW
// ever renumbers (it won't - they're ABI), fail the build here.
static_assert(Input::kKeyCount == GLFW_KEY_LAST + 1);
static_assert(Input::kMouseCount == GLFW_MOUSE_BUTTON_LAST + 1);
static_assert(key::kEscape == GLFW_KEY_ESCAPE && key::kSpace == GLFW_KEY_SPACE);
static_assert(key::kW == GLFW_KEY_W && key::kA == GLFW_KEY_A && key::kS == GLFW_KEY_S &&
              key::kD == GLFW_KEY_D);
static_assert(pad::kA == GLFW_GAMEPAD_BUTTON_A && pad::kDpadLeft == GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
static_assert(pad::kAxisLeftX == GLFW_GAMEPAD_AXIS_LEFT_X &&
              pad::kAxisRightTrigger == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER);

void Input::update(const Window& window) {
    HUE_PROFILE_ZONE("Input::update");
    GLFWwindow* w = window.handle();

    std::memcpy(m_prev_keys, m_keys, sizeof(m_keys));
    std::memcpy(m_prev_mouse, m_mouse, sizeof(m_mouse));
    m_prev_mouse_x = m_mouse_x;
    m_prev_mouse_y = m_mouse_y;
    m_prev_pad = m_pad;

    // GLFW rejects key tokens below GLFW_KEY_SPACE (32) with an error callback
    for (int k = GLFW_KEY_SPACE; k < kKeyCount; ++k) {
        m_keys[k] = glfwGetKey(w, k) == GLFW_PRESS;
    }
    for (int b = 0; b < kMouseCount; ++b) {
        m_mouse[b] = glfwGetMouseButton(w, b) == GLFW_PRESS;
    }
    glfwGetCursorPos(w, &m_mouse_x, &m_mouse_y);

    m_pad = GamepadState{};
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        GLFWgamepadstate gp;
        if (glfwJoystickIsGamepad(jid) && glfwGetGamepadState(jid, &gp)) {
            m_pad.connected = true;
            const char* name = glfwGetGamepadName(jid);
            if (name) {
                std::strncpy(m_pad.name, name, sizeof(m_pad.name) - 1);
            }
            for (int a = 0; a <= GLFW_GAMEPAD_AXIS_LAST; ++a) m_pad.axes[a] = gp.axes[a];
            for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; ++b) {
                m_pad.buttons[b] = gp.buttons[b] == GLFW_PRESS;
            }
            break; // first connected gamepad wins for now
        }
    }
}

} // namespace hue
