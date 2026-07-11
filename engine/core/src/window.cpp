// engine/core/src/window.cpp

#include "hue/core/window.h"

#include "hue/core/log.h"

#include <GLFW/glfw3.h>

namespace hue {

namespace {
void glfw_error_callback(int error, const char* description) {
    HUE_LOG_ERROR("GLFW error %d: %s", error, description);
}
} // namespace

Result<Platform> Platform::create() {
    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() != GLFW_TRUE) {
        return ErrorCode::kUnsupported;
    }
    Platform platform;
    platform.m_active = true;
    return platform;
}

Platform::~Platform() {
    if (m_active) {
        glfwTerminate();
    }
}

void Window::Deleter::operator()(GLFWwindow* w) const {
    glfwDestroyWindow(w);
}

Result<Window> Window::create(const WindowDesc& desc) {
    // No GL context; Vulkan owns presentation from Week 4 on.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* handle = glfwCreateWindow(desc.width, desc.height, desc.title, nullptr, nullptr);
    if (!handle) {
        return ErrorCode::kUnsupported;
    }

    Window window;
    window.m_window.reset(handle);
    return window;
}

bool Window::should_close() const {
    return glfwWindowShouldClose(m_window.get()) == GLFW_TRUE;
}

void Window::poll_events() {
    glfwPollEvents();
}

} // namespace hue
