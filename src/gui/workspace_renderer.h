#pragma once

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

// Present every viewport, including detached windows while the main window is
// minimized. Always leave the main GL context current for the next frame.
inline void renderWorkspaceWindows(GLFWwindow* mainWindow, const ImVec4& clearColor)
{
    glfwMakeContextCurrent(mainWindow);
    int width, height;
    glfwGetFramebufferSize(mainWindow, &width, &height);
    const bool drawMain = width > 0 && height > 0 && !glfwGetWindowAttrib(mainWindow, GLFW_ICONIFIED);
    if (drawMain) {
        glViewport(0, 0, width, height);
        glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    glfwMakeContextCurrent(mainWindow);
    if (drawMain) glfwSwapBuffers(mainWindow);
}
