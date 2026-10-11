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

void drawAntennaRows(ShowState& s, const char* id, const std::vector<NearbyAntenna>& rows,
                    bool showDistance, float height) {
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
            ImGui::TableNextColumn();
            if (ImGui::Selectable(providerName(a.provider), s.selected == index,
                                  ImGuiSelectableFlags_SpanAllColumns)) s.select(index);
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
    s.selectedFields.clear();
    s.pickCandidates.clear();
    s.nearby.clear();
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
    ImGui::Text("Antenna %lld | %s", static_cast<long long>(a.id), providerName(a.provider));
    if (ImGui::Button("Show selected on map")) s.center(mast.longitude, mast.latitude, 14);
    ImGui::Text("At this location: %zu antennas", mast.antennas.size());
    ImGui::TextDisabled("Selecting a hidden company enables its checkbox.");
    std::vector<NearbyAntenna> colocated;
    for (auto index : mast.antennas) colocated.push_back({index, 0});
    drawAntennaRows(s, "colocated", colocated, false, ImGui::GetTextLineHeightWithSpacing() * 7);
    ImGui::SeparatorText("All database fields");
    if (ImGui::BeginTable("antenna_fields", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 15);
        ImGui::TableSetupColumn("Value");
        for (const auto& field : s.selectedFields) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", field.first.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", field.second.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void drawNearby(ShowState& s, App& app) {
    if (!ImGui::Begin("Nearby antennas / tuning###PhreakShowNearby")) { ImGui::End(); return; }
    ImGui::Text("CellScope center: %.4f MHz", app.centerFreqMHz);
    const auto* antenna = s.selected == noAntenna ? nullptr : &s.database.antennas()[s.selected];
    if (antenna) ImGui::Text("Selected: %s, %.4f MHz, PCI %lld", providerName(antenna->provider),
                             antenna->frequencyMHz, static_cast<long long>(antenna->pci));
    ImGui::BeginDisabled(!antenna || antenna->frequencyMHz <= 0);
    if (ImGui::Button("Set CellScope center to selected antenna")) {
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
    if (!s.userPositionSet) {
        ImGui::TextWrapped("Enter longitude and latitude in Map, then click Set user point.");
        ImGui::End();
        return;
    }
    ImGui::Text("Longitude %.6f | Latitude %.6f", s.userLongitude, s.userLatitude);
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
    if (s.nearestDirty) {
        s.nearby = s.database.nearest(s.userLongitude, s.userLatitude, s.providerMask(),
                    s.nearestMode == 0 ? s.nearestCount : 0,
                    s.nearestMode == 0 ? std::numeric_limits<double>::infinity() : s.radiusKm);
        s.nearestDirty = false;
    }
    ImGui::Text("%zu antennas | company checkboxes apply", s.nearby.size());
    if (s.nearby.empty()) ImGui::TextUnformatted("No antennas match this search.");
    else drawAntennaRows(s, "nearest_antennas", s.nearby, true);
    ImGui::End();
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
        ImGui::DockBuilderDockWindow("Nearby antennas / tuning###PhreakShowNearby", nearby);
        ImGui::DockBuilderFinish(dock);
    }
    ImGui::DockSpace(dock, ImVec2(0, 0), visible ? 0 : ImGuiDockNodeFlags_KeepAliveOnly);
    ImGui::End();
    if (visible || isFloatingPanel("Map###PhreakShowMap")) {
        if (ImGui::Begin("Map###PhreakShowMap")) drawMapCanvas(s);
        ImGui::End();
    }
    if (visible || isFloatingPanel("Antenna details###PhreakShowDetails")) drawDetails(s);
    if (visible || isFloatingPanel("Nearby antennas / tuning###PhreakShowNearby")) drawNearby(s, app);
}

void shutdownPhreakShow() {
    phreakshow::detail::state().tiles.shutdown();
    phreakshow::detail::state().database.close();
}
