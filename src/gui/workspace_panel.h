#pragma once

#include "imgui_internal.h"

inline void configureWorkspaceWindows()
{
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    // Floating panels keep their own native window even when moved back over
    // the main window. Dragging onto a dock target can still dock them normally.
    io.ConfigViewportsNoAutoMerge = true;
    io.ConfigViewportsNoDecoration = false;
    io.ConfigViewportsNoTaskBarIcon = false;
    io.ConfigViewportsNoDefaultParent = true;
}

// A default applies only to a new workspace. Saved floating positions and user
// docking changes must survive subsequent frames and application restarts.
inline bool beginWorkspace(const char* name, ImGuiID defaultDock)
{
    ImGui::SetNextWindowDockID(defaultDock, ImGuiCond_FirstUseEver);
    return ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

// Panels can move between workspaces or into floating tab groups. Submit them
// when their actual dockspace is active, regardless of their original workspace.
// Hidden dockspaces are kept alive without reactivating their windows.
inline bool shouldDrawWorkspacePanel(const char* name, bool workspaceVisible)
{
    const auto* window = ImGui::FindWindowByName(name);
    if (!window) return workspaceVisible;
    auto* node = window->DockNode ? window->DockNode : ImGui::DockBuilderGetNode(window->DockId);
    if (!node) return window->DockId == 0 || workspaceVisible;
    const auto* root = ImGui::DockNodeGetRootNode(node);
    return !root->IsDockSpace() || root->LastFrameActive == ImGui::GetFrameCount();
}

// Returning to a workspace reactivates all its panels in submission order.
// Keep the selected dock tab instead of letting each appearing panel take focus.
inline bool beginWorkspacePanel(const char* name)
{
    return ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoFocusOnAppearing);
}
