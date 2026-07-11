// engine/core/include/hue/core/window.h
//
// Platform bootstrap + window (Week 1 spec). GLFW stays behind this header:
// engine code sees hue::Platform / hue::Window, never GLFW types. Errors
// surface as Result (no exceptions in engine code); the GLFW error callback
// logs instead of throwing.

#pragma once

#include "hue/core/result.h"

#include <memory>

struct GLFWwindow; // keeps GLFW out of the public interface

namespace hue {

// RAII around glfwInit/glfwTerminate. Create exactly one, before any Window.
class Platform {
public:
    static Result<Platform> create();
    ~Platform();

    Platform(Platform&& other) noexcept : m_active(other.m_active) { other.m_active = false; }
    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;
    Platform& operator=(Platform&&) = delete;

private:
    Platform() = default;
    bool m_active = false;
};

struct WindowDesc {
    int width = 1280;
    int height = 720;
    const char* title = "Hue";
};

class Window {
public:
    static Result<Window> create(const WindowDesc& desc);

    bool should_close() const;
    void poll_events(); // pumps the OS message queue (all windows)
    GLFWwindow* handle() const { return m_window.get(); }

private:
    Window() = default;

    struct Deleter {
        void operator()(GLFWwindow* w) const;
    };
    std::unique_ptr<GLFWwindow, Deleter> m_window;
};

} // namespace hue
