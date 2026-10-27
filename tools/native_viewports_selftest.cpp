// Native GLFW/OpenGL regression: detach, cross the app boundary, and verify
// changing pixels are rendered into a distinct OS window on every frame.
// Run on Linux with: xvfb-run -a ./native_viewports_selftest
#include "gui/workspace_panel.h"
#include "gui/workspace_renderer.h"
#include "imgui_impl_glfw.h"

#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

namespace {
ImGuiWindow* probe = nullptr;
ImVec2 markerCenter;
bool redMarker = false;
bool markerRendered = false;
void (*backendRenderWindow)(ImGuiViewport*, void*) = nullptr;

void renderAndCheckWindow(ImGuiViewport* viewport, void* user)
{
    backendRenderWindow(viewport, user);
    if (!probe || viewport != probe->Viewport) return;
    const auto* data = viewport->DrawData;
    const auto scale = data->FramebufferScale;
    const int x = static_cast<int>((markerCenter.x - data->DisplayPos.x) * scale.x);
    const int y = static_cast<int>((data->DisplaySize.y - markerCenter.y + data->DisplayPos.y) * scale.y) - 1;
    unsigned char pixel[4]{};
    glReadBuffer(GL_BACK);
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    require(pixel[0] == (redMarker ? 255 : 0) && pixel[1] == 0 && pixel[2] == (redMarker ? 0 : 255),
            "detached window did not render the current frame");
    markerRendered = true;
}
}

int main(int argc, char** argv)
{
    glfwSetErrorCallback([](int code, const char* message) {
        std::cerr << "GLFW " << code << ": " << message << '\n';
    });
#if defined(__linux__) && defined(GLFW_PLATFORM)
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    auto* mainWindow = glfwCreateWindow(800, 600, "CellScope viewport regression", nullptr, nullptr);
    if (!mainWindow) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(mainWindow);
    glfwSwapInterval(0);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    configureWorkspaceWindows();
    // This option demonstrates the old auto-merge behavior as a negative check.
    if (argc > 1 && std::string(argv[1]) == "--allow-auto-merge") io.ConfigViewportsNoAutoMerge = false;
    ImGui_ImplGlfw_InitForOpenGL(mainWindow, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    auto& platform = ImGui::GetPlatformIO();
    backendRenderWindow = platform.Renderer_RenderWindow;
    platform.Renderer_RenderWindow = renderAndCheckWindow;
    int result = 0;
    try {
        require((io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) != 0, "GLFW cannot create native viewports");
        ImGuiID dock = 0;
        for (int frame = 0; frame < 36; ++frame) {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            const auto* mainViewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowViewport(mainViewport->ID);
            ImGui::SetNextWindowPos(mainViewport->Pos);
            ImGui::SetNextWindowSize(mainViewport->Size);
            ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking);
            dock = ImGui::GetID("PanelDockSpace");
            if (!ImGui::DockBuilderGetNode(dock)) {
                ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dock, ImVec2(780, 560));
                ImGui::DockBuilderDockWindow("Viewport probe", dock);
                ImGui::DockBuilderFinish(dock);
            }
            ImGui::DockSpace(dock);
            ImGui::End();

            if (frame == 6 || frame == 16 || frame == 26) {
                probe = ImGui::FindWindowByName("Viewport probe");
                require(probe != nullptr, "probe panel was not created");
                ImGui::SetWindowDock(probe, 0, ImGuiCond_Always);
                const bool inside = frame == 16;
                ImGui::SetWindowPos(probe, ImVec2(mainViewport->Pos.x + (inside ? 100 : 850), mainViewport->Pos.y + 80));
                ImGui::SetWindowSize(probe, ImVec2(360, 240));
            }

            beginWorkspacePanel("Viewport probe");
            probe = ImGui::GetCurrentWindow();
            ImGui::Text("Frame %d", frame);
            const auto origin = ImGui::GetCursorScreenPos();
            markerCenter = ImVec2(origin.x + 40, origin.y + 40);
            redMarker = frame % 2 == 0;
            ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + 80, origin.y + 80),
                redMarker ? IM_COL32(255, 0, 0, 255) : IM_COL32(0, 0, 255, 255));
            ImGui::Dummy(ImVec2(80, 80));
            ImGui::End();
            markerRendered = false;
            ImGui::Render();
            renderWorkspaceWindows(mainWindow, ImVec4(0.06f, 0.07f, 0.09f, 1));
            require(glfwGetCurrentContext() == mainWindow, "main OpenGL context was not restored");
            if (frame >= 9) {
                require(probe->Viewport != mainViewport, "floating panel merged into the main framebuffer");
                auto* native = static_cast<GLFWwindow*>(probe->Viewport->PlatformHandle);
                require(native && native != mainWindow, "detached panel has no separate OS window");
                require(glfwGetWindowAttrib(native, GLFW_DECORATED), "detached panel has no native window frame");
                require(markerRendered, "native viewport stopped rendering");
            }
        }
        std::cout << "Native viewport: separate decorated OS window, live pixels across boundary moves, GL context restored\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    platform.Renderer_RenderWindow = backendRenderWindow;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(mainWindow);
    glfwTerminate();
    return result;
}
