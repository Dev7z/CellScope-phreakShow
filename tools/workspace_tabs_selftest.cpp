// Exercise nested dock-tab selection across inactive workspaces without a GUI backend.
#include "gui/workspace_panel.h"
#include "imgui_internal.h"

#include <cstring>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class WorkspaceFixture {
public:
    WorkspaceFixture()
    {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = ImVec2(1200, 800);
        io.DeltaTime = 1.0f / 60;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }

    ~WorkspaceFixture() { ImGui::DestroyContext(); }

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
        ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking);
        const auto outerDock = ImGui::GetID("ApplicationDockSpace");
        innerDock = ImGui::GetID("CellScopeDockSpace");
        if (!ImGui::DockBuilderGetNode(outerDock)) {
            ImGui::DockBuilderAddNode(outerDock, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(outerDock, ImVec2(1200, 800));
            ImGui::DockBuilderDockWindow("CellScope", outerDock);
            ImGui::DockBuilderDockWindow("phreakShow", outerDock);
            ImGui::DockBuilderFinish(outerDock);
            ImGui::DockBuilderAddNode(innerDock, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(innerDock, ImVec2(1000, 700));
            for (const char* panel : {"UEs", "Traffic", "Calls"})
                ImGui::DockBuilderDockWindow(panel, innerDock);
            ImGui::DockBuilderFinish(innerDock);
        }
        ImGui::DockSpace(outerDock);
        ImGui::End();

        ImGui::SetNextWindowDockID(outerDock, ImGuiCond_Always);
        const bool visible = ImGui::Begin("CellScope");
        ImGui::DockSpace(innerDock, ImVec2(0, 0), visible ? 0 : ImGuiDockNodeFlags_KeepAliveOnly);
        ImGui::End();
        ImGui::SetNextWindowDockID(outerDock, ImGuiCond_Always);
        ImGui::Begin("phreakShow");
        ImGui::TextUnformatted("Other workspace");
        ImGui::End();

        if (visible) {
            for (const char* panel : {"UEs", "Traffic", "Calls"}) {
                beginWorkspacePanel(panel);
                ImGui::TextUnformatted(panel);
                ImGui::End();
            }
        }
        ImGui::Render();
        return visible;
    }

    void frames(int count) { while (count-- > 0) frame(); }

    void expectSelected(const char* name) const
    {
        const auto* dock = ImGui::DockBuilderGetNode(innerDock);
        require(dock && dock->VisibleWindow && std::strcmp(dock->VisibleWindow->Name, name) == 0,
                "workspace switch changed the selected panel");
    }

private:
    ImGuiID innerDock = 0;
};

int main()
{
    try {
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

            fixture.frame("CellScope");
            fixture.frames(5);
            fixture.expectSelected(panel);
        }
        std::cout << "Workspace tabs: UEs, Traffic and Calls retain selection across switches\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
