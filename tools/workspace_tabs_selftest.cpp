// Exercise nested dock-tab selection across inactive workspaces without a GUI backend.
#include "gui/workspace_panel.h"
#include "imgui_internal.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class WorkspaceFixture {
public:
    WorkspaceFixture(const char* savedSettings = nullptr)
    {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = ImVec2(1200, 800);
        io.DeltaTime = 1.0f / 60;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
        io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;
        // Exercise ImGui's actual platform-window lifecycle without opening OS
        // windows or needing graphics/SDR hardware.
        auto& platform = ImGui::GetPlatformIO();
        platform.Platform_CreateWindow = [](ImGuiViewport* vp) { vp->PlatformHandle = vp; };
        platform.Platform_DestroyWindow = [](ImGuiViewport* vp) { vp->PlatformHandle = nullptr; };
        platform.Platform_ShowWindow = [](ImGuiViewport*) {};
        platform.Platform_SetWindowPos = [](ImGuiViewport*, ImVec2) {};
        platform.Platform_GetWindowPos = [](ImGuiViewport* vp) { return vp->Pos; };
        platform.Platform_SetWindowSize = [](ImGuiViewport*, ImVec2) {};
        platform.Platform_GetWindowSize = [](ImGuiViewport* vp) { return vp->Size; };
        platform.Platform_SetWindowFocus = [](ImGuiViewport*) {};
        platform.Platform_GetWindowFocus = [](ImGuiViewport*) { return false; };
        platform.Platform_GetWindowMinimized = [](ImGuiViewport*) { return false; };
        platform.Platform_SetWindowTitle = [](ImGuiViewport*, const char*) {};
        ImGuiPlatformMonitor monitor;
        monitor.MainPos = monitor.WorkPos = ImVec2(0, 0);
        monitor.MainSize = monitor.WorkSize = ImVec2(2400, 1400);
        platform.Monitors.push_back(monitor);
        ImGui::GetMainViewport()->PlatformHandle = this;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        if (savedSettings) ImGui::LoadIniSettingsFromMemory(savedSettings);
    }

    ~WorkspaceFixture()
    {
        ImGui::DestroyPlatformWindows();
        ImGui::DestroyContext();
    }

    bool frame(const char* select = nullptr)
    {
        ImGui::NewFrame();
        if (select) {
            auto* window = ImGui::FindWindowByName(select);
            require(window != nullptr, "missing tab to select");
            ImGui::FocusWindow(window);
            if (window->DockNode && window->DockNode->TabBar)
                window->DockNode->TabBar->NextSelectedTabId = window->TabId;
        }

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1200, 800));
        ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
        ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking);
        outerDock = ImGui::GetID("ApplicationDockSpace");
        innerDock = ImGui::GetID("CellScopeDockSpace");
        if (!ImGui::DockBuilderGetNode(outerDock)) {
            ImGui::DockBuilderAddNode(outerDock, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(outerDock, ImVec2(1200, 800));
            ImGui::DockBuilderFinish(outerDock);
        }
        if (!ImGui::DockBuilderGetNode(innerDock)) {
            ImGui::DockBuilderAddNode(innerDock, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(innerDock, ImVec2(1000, 700));
            for (const char* panel : {"UEs", "Traffic", "Calls"})
                ImGui::DockBuilderDockWindow(panel, innerDock);
            ImGui::DockBuilderFinish(innerDock);
        }
        ImGui::DockSpace(outerDock);
        ImGui::End();

        const bool visible = beginWorkspace("CellScope", outerDock);
        ImGui::DockSpace(innerDock, ImVec2(0, 0), visible ? 0 : ImGuiDockNodeFlags_KeepAliveOnly);
        ImGui::End();
        phreakVisible = beginWorkspace("phreakShow", outerDock);
        phreakDock = ImGui::GetID("PhreakShowDockSpace");
        if (!ImGui::DockBuilderGetNode(phreakDock)) {
            ImGui::DockBuilderAddNode(phreakDock, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(phreakDock, ImVec2(1000, 700));
            ImGui::DockBuilderDockWindow("Map", phreakDock);
            ImGui::DockBuilderDockWindow("Details", phreakDock);
            ImGui::DockBuilderFinish(phreakDock);
        }
        ImGui::DockSpace(phreakDock, ImVec2(0, 0), phreakVisible ? 0 : ImGuiDockNodeFlags_KeepAliveOnly);
        ImGui::End();

        for (const char* panel : {"UEs", "Traffic", "Calls", "Map", "Details"}) {
            const bool ownWorkspaceVisible = std::strcmp(panel, "Map") == 0 || std::strcmp(panel, "Details") == 0
                ? phreakVisible : visible;
            if (shouldDrawWorkspacePanel(panel, ownWorkspaceVisible)) {
                beginWorkspacePanel(panel);
                ImGui::TextUnformatted(panel);
                ImGui::End();
            }
        }
        ImGui::Render();
        ImGui::UpdatePlatformWindows();
        return visible;
    }

    void frames(int count) { while (count-- > 0) frame(); }

    void expectSelected(const char* name) const
    {
        const auto* dock = ImGui::DockBuilderGetNode(innerDock);
        require(dock && dock->VisibleWindow && std::strcmp(dock->VisibleWindow->Name, name) == 0,
                "workspace switch changed the selected panel");
    }

    void expectSubmitted(const char* name, bool expected) const
    {
        const auto* window = ImGui::FindWindowByName(name);
        require(window != nullptr, "missing panel");
        if ((window->LastFrameActive == ImGui::GetFrameCount()) != expected)
            throw std::runtime_error(std::string(name) + (expected ? " panel was not submitted" : " panel remained active"));
    }

    ImGuiID outerDock = 0;
    ImGuiID innerDock = 0;
    ImGuiID phreakDock = 0;
    bool phreakVisible = false;
};

static void testSelection()
{
    WorkspaceFixture fixture;
    fixture.frames(8);
    fixture.frame("CellScope");
    fixture.frames(4);
    for (const char* panel : {"UEs", "Traffic", "Calls", "UEs"}) {
        fixture.frame(panel);
        fixture.frames(4);
        fixture.expectSelected(panel);

        fixture.frame("phreakShow");
        fixture.frames(12);
        require(!fixture.frame(), "inactive workspace remained visible");
        fixture.expectSubmitted(panel, false);

        fixture.frame("CellScope");
        fixture.frames(5);
        fixture.expectSelected(panel);
    }
}

static void testDocking()
{
    std::string saved;
    ImGuiID mapDock;
    {
        WorkspaceFixture fixture;
        fixture.frames(10);
        fixture.frame("phreakShow");
        fixture.frames(6);
        auto* phreak = ImGui::FindWindowByName("phreakShow");
        require(phreak && phreak->DockId == fixture.outerDock, "new workspace was not docked");
        auto* map = ImGui::FindWindowByName("Map");
        require(map != nullptr, "map panel was not created");
        mapDock = map->DockId;
        ImGui::SetWindowDock(phreak, 0, ImGuiCond_Always);
        ImGui::SetWindowPos(phreak, ImVec2(1400, 80));
        ImGui::SetWindowSize(phreak, ImVec2(600, 600));
        fixture.frames(8);
        require(phreak->DockId == 0, "detached workspace was forced back into the host");
        require(fixture.phreakVisible, "detached workspace is hidden");
        require(phreak->Viewport != ImGui::GetMainViewport(), "dragging outside did not create a platform window");
        require(map->Viewport == phreak->Viewport, "map did not follow its detached workspace viewport");
        require(ImGui::FindWindowByName("Map")->DockId == mapDock, "undocking lost nested map layout");
        fixture.expectSubmitted("Map", true);
        saved = ImGui::SaveIniSettingsToMemory();
    }
    {
        WorkspaceFixture fixture(saved.c_str());
        fixture.frames(10);
        auto* phreak = ImGui::FindWindowByName("phreakShow");
        require(phreak && phreak->DockId == 0, "saved floating workspace was forced into the host");
        auto* map = ImGui::FindWindowByName("Map");
        require(map != nullptr, "saved map panel was not submitted");
        require(map->DockId == mapDock, "saved nested map layout was lost");

        ImGuiID left, right;
        ImGui::DockBuilderSplitNode(fixture.outerDock, ImGuiDir_Right, 0.45f, &right, &left);
        ImGui::DockBuilderDockWindow("CellScope", left);
        ImGui::DockBuilderDockWindow("phreakShow", right);
        ImGui::DockBuilderFinish(fixture.outerDock);
        fixture.frames(8);
        require(fixture.frame() && fixture.phreakVisible, "side-by-side workspaces are not both visible");
        require(phreak->DockId == right, "workspace did not stay in its chosen split");

        // Use the same queued request as a drag/drop. Moving the last tab out
        // of a split merges its sibling, which invalidates saved builder IDs.
        ImGui::SetWindowDock(phreak, 0, ImGuiCond_Always);
        fixture.frames(4);
        auto* cellscope = ImGui::FindWindowByName("CellScope");
        ImGui::DockContextQueueDock(ImGui::GetCurrentContext(), cellscope, cellscope->DockNode,
                                   phreak, ImGuiDir_None, 0.5f, false);
        fixture.frames(4);
        fixture.frame("CellScope");
        fixture.frames(6);
        require(!fixture.phreakVisible, "tabbed workspace did not become inactive");
        ImGui::DockBuilderDockWindow("Map", fixture.innerDock);
        ImGui::DockBuilderFinish(fixture.innerDock);
        fixture.frames(6);
        fixture.expectSubmitted("Map", true); // PhreakShow panel moved to CellScope
        fixture.frame("phreakShow");
        fixture.frames(6);
        require(!fixture.frame(), "CellScope remained visible after selecting phreakShow");
        fixture.expectSubmitted("Map", false);

        auto* calls = ImGui::FindWindowByName("Calls");
        require(calls != nullptr, "calls panel was not submitted");
        ImGui::SetWindowDock(calls, 0, ImGuiCond_Always);
        ImGui::SetWindowPos(calls, ImVec2(1400, 100));
        fixture.frames(6);
        fixture.expectSubmitted("Calls", true); // floating panel survives workspace switch
        require(calls->Viewport != ImGui::GetMainViewport(), "tool panel did not detach outside the main window");
    }
}

int main()
{
    try {
        testSelection();
        testDocking();
        std::cout << "Workspace selection, undocking, redocking, splits, persistence and moved panels passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
