#pragma once

#include "imgui.h"

// Returning to a workspace reactivates all its panels in submission order.
// Keep the selected dock tab instead of letting each appearing panel take focus.
inline bool beginWorkspacePanel(const char* name)
{
    return ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoFocusOnAppearing);
}
