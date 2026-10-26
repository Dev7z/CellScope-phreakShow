#include "gui/phreakshow.h"
#include "gui/phreakshow_state.h"
#include "core/main_funcs.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstdio>

namespace phreakshow {
std::vector<MapPoint> mapPoints;
bool addMapPoint(double longitude, double latitude, MapProvider provider) {
    if (!validMapCoordinates(longitude, latitude)) return false;
    mapPoints.push_back({longitude, latitude, provider});
    return true;
}

namespace detail {
ShowState& state() {
    static ShowState value;
    return value;
}

bool buttonEllipsis(const char* label) {
    const auto& style = ImGui::GetStyle();
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    if (ImGui::CalcTextSize(label, nullptr, true).x + 2 * style.FramePadding.x <= width)
        return ImGui::Button(label);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
    const bool pressed = ImGui::Button(label, ImVec2(width, 0));
    ImGui::PopStyleColor();
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),
        ImVec2(min.x + style.FramePadding.x, min.y + style.FramePadding.y),
        ImVec2(max.x - style.FramePadding.x, max.y), max.x - style.FramePadding.x, label, nullptr, nullptr);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s", label);
    return pressed;
}

bool checkboxEllipsis(const char* label, bool* value) {
    const auto& style = ImGui::GetStyle();
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const float labelOffset = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x;
    if (labelOffset + ImGui::CalcTextSize(label, nullptr, true).x <= ImGui::GetContentRegionAvail().x)
        return ImGui::Checkbox(label, value);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
    const bool changed = ImGui::Checkbox(label, value);
    ImGui::PopStyleColor();
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),
        ImVec2(min.x + labelOffset, min.y + style.FramePadding.y),
        ImVec2(right, max.y), right, label, nullptr, nullptr);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s", label);
    return changed;
}

void drawAntennaRows(ShowState& s, const char* id, const std::vector<NearbyAntenna>& rows,
                    bool showDistance, float height, bool colorByCoordinates) {
    const int columns = showDistance ? 7 : 6;
    const float scale = ImGui::GetFontSize() / 13.0f;
    if (!ImGui::BeginTable(id, columns, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable,
                          ImVec2(0, height), (showDistance ? 535.0f : 475.0f) * scale)) return;
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("Company", ImGuiTableColumnFlags_WidthFixed, 80 * scale);
    ImGui::TableSetupColumn("MHz", ImGuiTableColumnFlags_WidthFixed, 75 * scale);
    ImGui::TableSetupColumn("PCI", ImGuiTableColumnFlags_WidthFixed, 42 * scale);
    ImGui::TableSetupColumn("Site ID", ImGuiTableColumnFlags_WidthFixed, 60 * scale);
    ImGui::TableSetupColumn("Azimuth", ImGuiTableColumnFlags_WidthFixed, 60 * scale);
    if (showDistance) ImGui::TableSetupColumn("km", ImGuiTableColumnFlags_WidthFixed, 60 * scale);
    ImGui::TableSetupColumn("Antenna ID", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto index = rows[row].antenna;
            const auto& a = s.database.antennas()[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            if (colorByCoordinates) {
                // A dark palette with 262,144 distinct shades, rather than a
                // short repeating stripe pattern. Co-located rows share a key.
                const size_t shade = s.nearbyCoordinateGroups[a.mast] * 4051u;
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    IM_COL32(16 + (shade & 63), 24 + ((shade >> 6) & 63),
                             96 + ((shade >> 12) & 63), 255));
            }
            ImGui::TableNextColumn();
            const bool selected = s.isAntennaSelected(index);
            ImGui::PushStyleColor(ImGuiCol_Header, colorByCoordinates ? 0 : selected ? IM_COL32(190, 150, 25, 180) : IM_COL32(140, 85, 10, 180));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, colorByCoordinates ? 0 : selected ? IM_COL32(210, 170, 30, 210) : IM_COL32(140, 85, 10, 210));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, colorByCoordinates ? 0 : IM_COL32(230, 190, 45, 230));
            if (ImGui::Selectable(providerName(a.provider), selected || s.isAntennaHovered(index),
                                  ImGuiSelectableFlags_SpanAllColumns)) s.select(index);
            ImGui::PopStyleColor(3);
            const bool rowHovered = ImGui::IsItemHovered();
            if (rowHovered) s.hover(index);
            // Preserve each coordinate's background when highlighting a row.
            if (colorByCoordinates && (s.isAntennaSelected(index) || s.isAntennaHovered(index)))
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                    s.isAntennaSelected(index) ? IM_COL32(255, 225, 70, 255) : IM_COL32(210, 130, 20, 255));
            else if (!colorByCoordinates && (s.isAntennaSelected(index) || rowHovered))
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, s.isAntennaSelected(index)
                    ? IM_COL32(190, 150, 25, 180) : IM_COL32(140, 85, 10, 180));
            ImGui::TableNextColumn();
            if (a.frequencyMHz > 0) ImGui::Text("%.4f", a.frequencyMHz);
            else ImGui::TextUnformatted("--");
            ImGui::TableNextColumn();
            if (a.pci >= 0) ImGui::Text("%lld", static_cast<long long>(a.pci));
            else ImGui::TextUnformatted("--");
            ImGui::TableNextColumn();
            if (a.siteId >= 0) ImGui::Text("%lld", static_cast<long long>(a.siteId));
            else ImGui::TextUnformatted("--");
            ImGui::TableNextColumn();
            if (std::isfinite(a.azimuth)) ImGui::Text("%.0f deg", a.azimuth);
            else ImGui::TextUnformatted("--");
            if (showDistance) {
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", rows[row].distanceKm);
            }
            ImGui::TableNextColumn();
            ImGui::Text("%lld", static_cast<long long>(a.id));
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

void loadDatabase(ShowState& s) {
    s.selected = noAntenna;
    s.selectedFrequency.reset();
    s.hovered = noAntenna;
    s.hoveredFrequency.reset();
    s.hoveredFrame = -1;
    s.selectedFields.clear();
    s.pickCandidates.clear();
    s.nearby.clear();
    s.frequencies.clear();
    s.nearbyOnMap.clear();
    s.nearbyCoordinateGroups.clear();
    s.nearbyCoordinateCount = 0;
    s.nearestDirty = true;
    s.tuneStatus.clear();
    s.database.open(std::filesystem::u8path(s.databasePath));
}

void locateDatabase(ShowState& s) {
    if (s.databaseLocated) return;
    s.databaseLocated = true;
    for (const auto& path : {executableDirectory() / "switzerland.db",
                             std::filesystem::path("switzerland.db"),
                             std::filesystem::path(PHREAKSHOW_DATABASE_PATH)}) {
        if (std::filesystem::exists(path)) {
            std::snprintf(s.databasePath, sizeof(s.databasePath), "%s", path.u8string().c_str());
            break;
        }
    }
    loadDatabase(s);
}

void drawDetails(ShowState& s) {
    if (!ImGui::Begin("Antenna details###PhreakShowDetails")) { ImGui::End(); return; }
    if (s.selected == noAntenna) {
        ImGui::TextWrapped("Click a mast, antenna direction line, or a row in Nearby antennas.");
        ImGui::End();
        return;
    }
    const auto& a = s.database.antennas()[s.selected];
    const auto& mast = s.database.masts()[a.mast];
    ImGui::TextAligned(0, -FLT_MIN, "Antenna %lld | %s", static_cast<long long>(a.id), providerName(a.provider));
    if (buttonEllipsis("Show selected on map")) s.center(mast.longitude, mast.latitude, 14);
    ImGui::SeparatorText("All database fields");
    const auto fieldValue = [&](const char* name) -> const char* {
        for (const auto& field : s.selectedFields)
            if (field.first == name) return field.second.c_str();
        return "--";
    };
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
    ImGui::TextAligned(0, -FLT_MIN, "%s %s, %s | PCI: %s, %s MHz",
        fieldValue("property.operator"), fieldValue("property.station"),
        fieldValue("property.technology"), fieldValue("property.pci"), fieldValue("property.frequency_mhz"));
    ImGui::PopFont();
    if (ImGui::BeginTable("antenna_fields", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 15);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const auto& field : s.selectedFields) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextAligned(0, -FLT_MIN, "%s", field.first.c_str());
            ImGui::TableNextColumn();
            ImGui::TextAligned(0, -FLT_MIN, "%s", field.second.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void drawColocated(ShowState& s) {
    if (!ImGui::Begin("Antennas at same coordinates###PhreakShowColocated")) { ImGui::End(); return; }
    if (s.selected == noAntenna) {
        ImGui::TextWrapped("Select an antenna to see the other antennas at its coordinates.");
    } else {
        const auto& mast = s.database.masts()[s.database.antennas()[s.selected].mast];
        ImGui::TextAligned(0, -FLT_MIN, "Longitude %.6f | Latitude %.6f", mast.longitude, mast.latitude);
        ImGui::Text("At this location: %zu antennas", mast.antennas.size());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextAligned(0, -FLT_MIN, "Selecting a hidden company enables its map checkbox.");
        ImGui::PopStyleColor();
        std::vector<NearbyAntenna> colocated;
        for (auto index : mast.antennas) colocated.push_back({index, 0});
        drawAntennaRows(s, "colocated", colocated, false);
    }
    ImGui::End();
}

void drawFrequencyRows(ShowState& s) {
    if (!ImGui::BeginTable("nearby_frequencies", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) return;
    ImGui::TableSetupColumn("Frequency", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Antennas", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(s.frequencies.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto& group = s.frequencies[row];
            const bool selected = s.selectedFrequency ? *s.selectedFrequency == group.frequencyMHz :
                s.isNearby(s.selected) && s.database.antennas()[s.selected].frequencyMHz == group.frequencyMHz;
            const bool hovered = s.hoveredFrequency && *s.hoveredFrequency == group.frequencyMHz &&
                s.hoveredFrame >= ImGui::GetFrameCount() - 1;
            ImGui::PushID(row);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char label[64];
            if (group.frequencyMHz > 0) std::snprintf(label, sizeof(label), "%.10g MHz", group.frequencyMHz);
            else std::snprintf(label, sizeof(label), "Unknown frequency");
            ImGui::PushStyleColor(ImGuiCol_Header, selected ? IM_COL32(190, 150, 25, 180) : IM_COL32(140, 85, 10, 180));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, selected ? IM_COL32(210, 170, 30, 210) : IM_COL32(140, 85, 10, 210));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(230, 190, 45, 230));
            if (ImGui::Selectable(label, selected || hovered, ImGuiSelectableFlags_SpanAllColumns))
                s.selectFrequency(group.frequencyMHz);
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) s.hoverFrequency(group.frequencyMHz);
            ImGui::TableNextColumn();
            ImGui::Text("%zu", group.antennas.size());
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

void drawNearby(ShowState& s, App& app) {
    if (!ImGui::Begin("Nearby antennas / tuning###PhreakShowNearby")) { ImGui::End(); return; }
    ImGui::TextAligned(0, -FLT_MIN, "CellScope center: %.4f MHz", app.centerFreqMHz);
    const auto* antenna = s.selected == noAntenna ? nullptr : &s.database.antennas()[s.selected];
    if (s.selectedFrequency) ImGui::TextAligned(0, -FLT_MIN, "Selected frequency: %.4f MHz", *s.selectedFrequency);
    else if (antenna) ImGui::TextAligned(0, -FLT_MIN, "Selected: %s, %.4f MHz, PCI %lld", providerName(antenna->provider),
                             antenna->frequencyMHz, static_cast<long long>(antenna->pci));
    ImGui::BeginDisabled(!antenna || antenna->frequencyMHz <= 0);
    if (buttonEllipsis("Set CellScope center to selected antenna")) {
        if (app.active->running()) {
            retunePreserving(app, antenna->frequencyMHz);
            if (app.sourceMode == 1) app.wav.setCenterFreq(antenna->frequencyMHz * 1e6);
        } else app.centerFreqMHz = antenna->frequencyMHz;
        app.viewA.resetView = true;
        s.tuneStatus = "CellScope center set to " + std::to_string(antenna->frequencyMHz) + " MHz.";
        if (app.sourceMode == 1) s.tuneStatus += " WAV frequency label updated.";
    }
    ImGui::EndDisabled();
    if (!s.tuneStatus.empty()) ImGui::TextWrapped("%s", s.tuneStatus.c_str());
    ImGui::SeparatorText("From your coordinates");
    for (auto p : {MapProvider::Sunrise, MapProvider::Swisscom, MapProvider::Salt}) {
        if (p != MapProvider::Sunrise) ImGui::SameLine();
        if (ImGui::Checkbox(providerName(p), &s.nearbyProviders[static_cast<size_t>(p)])) s.nearestDirty = true;
    }
    if (!s.userPositionSet) {
        ImGui::TextWrapped("Enter longitude and latitude in Map, then click Set user point.");
        ImGui::End();
        return;
    }
    ImGui::TextAligned(0, -FLT_MIN, "Longitude %.6f | Latitude %.6f", s.userLongitude, s.userLatitude);
    checkboxEllipsis("Only show these antennas on map", &s.onlyNearbyOnMap);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 15);
    if (ImGui::Combo("Search", &s.nearestMode, "K nearest antennas\0All within radius\0")) s.nearestDirty = true;
    if (s.nearestMode == 0) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        if (ImGui::InputInt("K", &s.nearestCount)) {
            s.nearestCount = std::clamp(s.nearestCount, 1, std::max(1, static_cast<int>(s.database.antennas().size())));
            s.nearestDirty = true;
        }
    } else {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        if (ImGui::InputDouble("Radius (km)", &s.radiusKm, 0.1, 1, "%.3f")) {
            if (!std::isfinite(s.radiusKm)) s.radiusKm = 2;
            s.radiusKm = std::clamp(s.radiusKm, 0.0, 40075.0);
            s.nearestDirty = true;
        }
    }
    ImGui::BeginDisabled(s.nearestMode != 0);
    if (checkboxEllipsis("Count antennas at the same coordinates as one", &s.countCoordinatesOnce))
        s.nearestDirty = true;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("K counts distinct coordinates. Every antenna matching the company filters\n"
                          "at those coordinates stays in the table, with one background shade per location.\n"
                          "This option does not apply to radius searches.");
    s.refreshNearby();
    ImGui::Text("%zu antennas | %zu coordinates | %zu frequencies",
                s.nearby.size(), s.nearbyCoordinateCount, s.frequencies.size());
    if (ImGui::BeginTabBar("nearby_views")) {
        if (ImGui::BeginTabItem("By antennas")) {
            if (s.nearby.empty()) ImGui::TextUnformatted("No antennas match this search.");
            else drawAntennaRows(s, "nearest_antennas", s.nearby, true, 0,
                                 s.nearestMode == 0 && s.countCoordinatesOnce);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("By frequency")) {
            if (s.frequencies.empty()) ImGui::TextUnformatted("No frequencies match this search.");
            else drawFrequencyRows(s);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void drawSettings(ShowState& s) {
    bool enabled = s.backgroundMusic.enabled();
    if (ImGui::Checkbox("BG music", &enabled)) {
        std::filesystem::path musicPath;
        if (enabled) {
            constexpr const char* filename = "iknowyouregettingfasterbuticatchthemall.mp3";
            for (const auto& path : {executableDirectory() / filename,
                                     std::filesystem::path(filename),
                                     std::filesystem::path(PHREAKSHOW_MUSIC_PATH)}) {
                if (std::filesystem::exists(path)) {
                    musicPath = path;
                    break;
                }
            }
        }
        s.backgroundMusic.setEnabled(enabled, musicPath);
    }
    if (!s.backgroundMusic.error().empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", s.backgroundMusic.error().c_str());
}
} // namespace detail
} // namespace phreakshow

void drawPhreakShow(App& app) {
    using namespace phreakshow::detail;
    auto& s = state();
    locateDatabase(s);
    s.tiles.beginFrame();
    const bool visible = ImGui::Begin("phreakShow###PhreakShowWorkspace", nullptr,
                                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImGuiID dock = ImGui::GetID("PhreakShowDockSpace");
    if (!ImGui::DockBuilderGetNode(dock)) {
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, ImGui::GetContentRegionAvail());
        ImGuiID map = dock, right;
        ImGui::DockBuilderSplitNode(map, ImGuiDir_Right, 0.38f, &right, &map);
        ImGuiID details, nearby;
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.48f, &nearby, &details);
        ImGui::DockBuilderDockWindow("Map###PhreakShowMap", map);
        ImGui::DockBuilderDockWindow("Antenna details###PhreakShowDetails", details);
        ImGui::DockBuilderDockWindow("Antennas at same coordinates###PhreakShowColocated", details);
        ImGui::DockBuilderDockWindow("Nearby antennas / tuning###PhreakShowNearby", nearby);
        ImGui::DockBuilderFinish(dock);
    }
    // Add the new panel to layouts saved before it existed.
    if (!ImGui::FindWindowSettingsByID(ImHashStr("###PhreakShowColocated")) &&
        !ImGui::FindWindowByName("Antennas at same coordinates###PhreakShowColocated")) {
        const auto* details = ImGui::FindWindowSettingsByID(ImHashStr("###PhreakShowDetails"));
        ImGui::DockBuilderDockWindow("Antennas at same coordinates###PhreakShowColocated", details && details->DockId ? details->DockId : dock);
    }
    bool intercomVisible = false;
    if (visible && ImGui::BeginTabBar("PhreakShowTabs")) {
        if (ImGui::BeginTabItem("Intercom")) {
            s.intercomActive = intercomVisible = true;
            ImGui::DockSpace(dock, ImVec2(0, 0));
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("settings")) {
            s.intercomActive = false;
            drawSettings(s);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (!intercomVisible) ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_KeepAliveOnly);
    ImGui::End();
    if (s.intercomActive && (intercomVisible || isFloatingPanel("Map###PhreakShowMap"))) {
        if (ImGui::Begin("Map###PhreakShowMap")) drawMapCanvas(s);
        ImGui::End();
    }
    if (s.intercomActive && (intercomVisible || isFloatingPanel("Antenna details###PhreakShowDetails"))) drawDetails(s);
    if (s.intercomActive && (intercomVisible || isFloatingPanel("Antennas at same coordinates###PhreakShowColocated"))) drawColocated(s);
    if (s.intercomActive && (intercomVisible || isFloatingPanel("Nearby antennas / tuning###PhreakShowNearby"))) drawNearby(s, app);
}

void shutdownPhreakShow() {
    phreakshow::detail::state().backgroundMusic.setEnabled(false);
    phreakshow::detail::state().tiles.shutdown();
    phreakshow::detail::state().database.close();
}
