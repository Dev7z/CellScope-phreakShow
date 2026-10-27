#pragma once

#include "imgui_internal.h"
#include <array>

namespace phreakshow::detail {
enum class Panel { Map, MapControls, Details, Colocated, NearbySearch, NearbyAntennas, NearbyFrequencies, Settings, Count };
constexpr size_t panelCount = static_cast<size_t>(Panel::Count);
constexpr std::array<const char*, panelCount> panelNames{
    "Map###PhreakShowMap",
    "Map controls###PhreakShowMapControls",
    "Antenna details###PhreakShowDetails",
    "Antennas at same coordinates###PhreakShowColocated",
    "Nearby search / tuning###PhreakShowNearby",
    "Nearby antennas###PhreakShowNearbyAntennas",
    "Nearby frequencies###PhreakShowNearbyFrequencies",
    "PhreakShow settings###PhreakShowSettings"
};

inline const char* panelName(Panel panel) { return panelNames[static_cast<size_t>(panel)]; }

inline bool hasPanelLayout(Panel panel)
{
    const char* name = panelName(panel);
    return ImGui::FindWindowByName(name) || ImGui::FindWindowSettingsByID(ImHashStr(name));
}

inline ImGuiID panelDock(Panel panel, ImGuiID fallback)
{
    const char* name = panelName(panel);
    if (const auto* window = ImGui::FindWindowByName(name))
        if (window->DockId) return window->DockId;
    if (const auto* settings = ImGui::FindWindowSettingsByID(ImHashStr(name)))
        if (settings->DockId) return settings->DockId;
    return fallback;
}

// Called inside the workspace, before submitting any of its panels. Existing
// layouts keep their positions; only panels introduced since that save are added.
inline void preparePhreakShowLayout(ImGuiID dock, bool reset = false)
{
    if (reset) ImGui::DockBuilderRemoveNode(dock);
    if (!ImGui::DockBuilderGetNode(dock)) {
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        const ImVec2 available = ImGui::GetContentRegionAvail();
        ImGui::DockBuilderSetNodeSize(dock, ImVec2(ImMax(available.x, 640.0f), ImMax(available.y, 480.0f)));
        ImGuiID left, right, controls, map, details, search, results;
        ImGui::DockBuilderSplitNode(dock, ImGuiDir_Right, 0.38f, &right, &left);
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.30f, &controls, &map);
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.60f, &search, &details);
        ImGui::DockBuilderSplitNode(search, ImGuiDir_Down, 0.48f, &results, &search);
        const std::array<ImGuiID, panelCount> destinations{
            map, controls, details, details, search, results, results, details
        };
        for (size_t i = 0; i < panelCount; ++i)
            if (reset || !hasPanelLayout(static_cast<Panel>(i)))
                ImGui::DockBuilderDockWindow(panelNames[i], destinations[i]);
        ImGui::DockBuilderFinish(dock);
        return;
    }

    // New panels join a related tab group in older layouts. Never redock an
    // existing detached panel while initializing or restoring another panel.
    const std::array<Panel, panelCount> companions{
        Panel::Map, Panel::Map, Panel::Details, Panel::Details,
        Panel::NearbySearch, Panel::NearbySearch, Panel::NearbyAntennas, Panel::Details
    };
    bool changed = false;
    for (size_t i = 0; i < panelCount; ++i) {
        if (hasPanelLayout(static_cast<Panel>(i))) continue;
        ImGuiID destination = panelDock(companions[i], dock);
        auto* node = ImGui::DockBuilderGetNode(destination);
        // Keep new panels in this workspace if their companion was detached.
        if (!node || ImGui::DockNodeGetRootNode(node)->ID != dock) destination = dock;
        ImGui::DockBuilderDockWindow(panelNames[i], destination);
        changed = true;
    }
    if (changed) ImGui::DockBuilderFinish(dock);
}
} // namespace phreakshow::detail
