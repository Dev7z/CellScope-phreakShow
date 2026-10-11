#include "gui/phreakshow_state.h"
#include <algorithm>
#include <cstdio>

namespace phreakshow::detail {
namespace {
constexpr int tileSize = 256;
constexpr int maxZoom = 18;
constexpr float sectorLength = 52;

ImU32 providerColor(MapProvider provider) {
    switch (provider) {
    case MapProvider::Sunrise: return IM_COL32(245, 75, 100, 255);
    case MapProvider::Swisscom: return IM_COL32(65, 145, 255, 255);
    case MapProvider::Salt: return IM_COL32(100, 215, 125, 255);
    default: return IM_COL32(210, 210, 210, 255);
    }
}

float segmentDistance(ImVec2 point, ImVec2 a, ImVec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float length = dx * dx + dy * dy;
    const float t = length > 0 ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length, 0.0f, 1.0f) : 0;
    return std::hypot(point.x - a.x - t * dx, point.y - a.y - t * dy);
}

struct Canvas {
    ImVec2 origin, size;
    double left, top, world;
    // Repeat with the map tiles at the date line or when the viewport exceeds
    // the world width. The margin includes direction lines from nearby masts.
    template<class F> void forEachPosition(double longitude, double latitude, float margin, F draw) const {
        if (!validMapCoordinates(longitude, latitude)) return;
        const double y = mercatorY(latitude) * world - top;
        if (y < -margin || y > size.y + margin) return;
        const double x = wrapX(0.5 + longitude / 360) * world;
        const double first = std::ceil((left - margin - x) / world);
        const double last = std::floor((left + size.x + margin - x) / world);
        for (double copy = first; copy <= last; copy += 1)
            draw(ImVec2(origin.x + static_cast<float>(x + copy * world - left), origin.y + static_cast<float>(y)));
    }
};

void drawSector(ImDrawList* draw, const Antenna& antenna, ImVec2 center, bool selected) {
    const ImU32 color = selected ? IM_COL32(255, 225, 70, 255) : providerColor(antenna.provider);
    if (!std::isfinite(antenna.azimuth)) {
        // Missing direction remains selectable at the mast's center.
        draw->AddCircle(center, selected ? 9 : 6, color, 0, selected ? 3 : 1.5f);
        return;
    }
    const auto direction = antennaDirection(antenna.azimuth, sectorLength);
    const ImVec2 end(center.x + static_cast<float>(direction.first), center.y + static_cast<float>(direction.second));
    draw->AddLine(center, end, IM_COL32(15, 20, 25, 255), selected ? 7 : 4);
    draw->AddLine(center, end, color, selected ? 4 : 2);
    draw->AddCircleFilled(end, selected ? 4 : 2.5f, color);
}

void toolbar(ShowState& s) {
    if (ImGui::Button("+")) s.zoom = std::min(maxZoom, s.zoom + 1);
    ImGui::SameLine();
    if (ImGui::Button("-")) s.zoom = std::max(0, s.zoom - 1);
    ImGui::SameLine();
    if (ImGui::Button("World")) s.center(10, 35, 2);
    ImGui::SameLine();
    if (ImGui::Button("Switzerland")) s.center(8.13, 46.8, 8);
    ImGui::SameLine();
    // Basel SBB station (GeoNames 47.547480 N, 7.589772 E).
    if (ImGui::Button("Basel SBB")) s.center(7.589772, 47.547480, 13);
    ImGui::SameLine();
    ImGui::Text("Zoom %d", s.zoom);

    for (auto p : {MapProvider::Sunrise, MapProvider::Swisscom, MapProvider::Salt}) {
        if (p != MapProvider::Sunrise) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, providerColor(p));
        if (ImGui::Checkbox(providerName(p), &s.providers[static_cast<size_t>(p)])) s.nearestDirty = true;
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(s.zoom < 13);
    ImGui::Checkbox("Satellite / aerial", &s.satellite);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Available at zoom 13+. SWISSIMAGE aerial photography, cached locally.");

    if (ImGui::CollapsingHeader("Your coordinates / database")) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
        ImGui::InputDouble("Longitude", &s.inputLongitude, 0, 0, "%.6f");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
        ImGui::InputDouble("Latitude", &s.inputLatitude, 0, 0, "%.6f");
        if (ImGui::Button("Set user point")) {
            s.invalidUserPosition = !validMapCoordinates(s.inputLongitude, s.inputLatitude);
            if (!s.invalidUserPosition) {
                s.userLongitude = s.inputLongitude;
                s.userLatitude = s.inputLatitude;
                s.userPositionSet = true;
                s.nearestDirty = true;
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!s.userPositionSet);
        if (ImGui::Button("Go to user (zoom 13)")) s.center(s.userLongitude, s.userLatitude, 13);
        ImGui::EndDisabled();
        if (s.invalidUserPosition) ImGui::TextColored(ImVec4(1, .4f, .3f, 1),
            "Longitude must be -180..180, latitude -90..90.");
        if (s.userPositionSet) ImGui::TextColored(ImVec4(.1f, 1, 1, 1), "User: %.6f, %.6f", s.userLongitude, s.userLatitude);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##database_path", s.databasePath, sizeof(s.databasePath));
        if (ImGui::Button("Load / reload database")) {
            s.selected = noAntenna;
            s.selectedFields.clear();
            s.pickCandidates.clear();
            s.nearby.clear();
            s.nearestDirty = true;
            s.tuneStatus.clear();
            s.database.open(std::filesystem::u8path(s.databasePath));
        }
        ImGui::SameLine();
        if (ImGui::Button("Retry aerial tiles")) s.tiles.retrySatellite();
    }
    if (!s.database.error().empty()) ImGui::TextWrapped("Database: %s", s.database.error().c_str());
    if (!s.tiles.hasOfflineMap()) ImGui::TextWrapped("Offline tiles missing: place assets/map_tiles beside the executable.");
    ImGui::TextDisabled("Drag to pan, scroll to zoom, click to select. Zoom 14+: fixed-length directions.");
}
} // namespace

void drawMapCanvas(ShowState& s) {
    toolbar(s);
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.y -= 3 * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    if (size.x < 1 || size.y < 40) return;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##map_canvas", size, ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    const auto& io = ImGui::GetIO();
    const double offsetX = io.MousePos.x - origin.x - size.x / 2;
    const double offsetY = io.MousePos.y - origin.y - size.y / 2;
    if (hovered && io.MouseWheel != 0) {
        const int next = std::clamp(s.zoom + (io.MouseWheel > 0 ? 1 : -1), 0, maxZoom);
        const double before = tileSize * (1 << s.zoom), after = tileSize * (1 << next);
        s.mapX += offsetX / before - offsetX / after;
        s.mapY += offsetY / before - offsetY / after;
        s.zoom = next;
    }
    const int count = 1 << s.zoom;
    const double world = tileSize * count;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) s.dragged = false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        s.dragged = true;
        s.mapX -= io.MouseDelta.x / world;
        s.mapY -= io.MouseDelta.y / world;
    }
    const bool clicked = hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !s.dragged;
    s.mapX = wrapX(s.mapX);
    const double halfHeight = std::min(0.5, size.y / (2 * world));
    s.mapY = std::clamp(s.mapY, halfHeight, 1 - halfHeight);
    const Canvas canvas{origin, size, s.mapX * world - size.x / 2, s.mapY * world - size.y / 2, world};
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    draw->PushClipRect(origin, end, true);
    draw->AddRectFilled(origin, end, IM_COL32(17, 27, 37, 255));
    bool aerialFallback = false;
    for (int ty = std::max(0, static_cast<int>(std::floor(canvas.top / tileSize)));
         ty < count && ty * tileSize < canvas.top + size.y; ++ty) {
        for (int tx = static_cast<int>(std::floor(canvas.left / tileSize)); tx * tileSize < canvas.left + size.x; ++tx) {
            const auto tile = s.tiles.image(s.zoom, (tx % count + count) % count, ty, s.satellite);
            const ImVec2 a(origin.x + static_cast<float>(tx * tileSize - canvas.left),
                           origin.y + static_cast<float>(ty * tileSize - canvas.top));
            const ImVec2 b(a.x + tileSize, a.y + tileSize);
            if (tile.texture) draw->AddImage(static_cast<ImTextureID>(tile.texture), a, b, tile.uv0, tile.uv1);
            else draw->AddRect(a, b, IM_COL32(65, 80, 95, 255));
            if (s.satellite && s.zoom >= 13 && !tile.satellite) aerialFallback = true;
        }
    }

    const double margin = s.zoom >= 14 ? sectorLength + 8 : 10;
    const double westX = (canvas.left - margin) / world, eastX = (canvas.left + size.x + margin) / world;
    const bool wholeWorld = eastX - westX >= 1;
    const auto visible = s.database.visibleMasts(wholeWorld ? -180 : longitude(westX),
                        wholeWorld ? 180 : longitude(eastX), latitude((canvas.top + size.y + margin) / world),
                        latitude((canvas.top - margin) / world), s.providerMask());
    std::vector<std::pair<float, size_t>> hits;
    size_t visibleAntennas = 0;
    const float mastRadius = s.zoom < 11 ? 2.0f : 4.0f;
    for (auto mastIndex : visible) {
        const auto& mast = s.database.masts()[mastIndex];
        // A single point per exact coordinate, irrespective of the antenna count.
        MapProvider first = MapProvider::Unspecified;
        unsigned companies = 0;
        for (auto p : {MapProvider::Sunrise, MapProvider::Swisscom, MapProvider::Salt, MapProvider::Unspecified}) {
            if (mast.providers & s.providerMask() & providerBit(p)) {
                if (!companies) first = p;
                ++companies;
            }
        }
        const ImU32 mastColor = companies > 1 ? IM_COL32(225, 225, 235, 255) : providerColor(first);
        canvas.forEachPosition(mast.longitude, mast.latitude, static_cast<float>(margin), [&](ImVec2 center) {
            draw->AddCircleFilled(center, mastRadius, mastColor);
            draw->AddCircle(center, mastRadius, IM_COL32(20, 25, 30, 255), 0, 1.0f);
            const float centerDistance = std::hypot(io.MousePos.x - center.x, io.MousePos.y - center.y);
            for (auto index : mast.antennas) {
                const auto& antenna = s.database.antennas()[index];
                if (!(providerBit(antenna.provider) & s.providerMask())) continue;
                if (s.zoom >= 14) drawSector(draw, antenna, center, false);
                if (!clicked) continue;
                float distance = centerDistance;
                if (s.zoom >= 14 && std::isfinite(antenna.azimuth)) {
                    const auto direction = antennaDirection(antenna.azimuth, sectorLength);
                    distance = segmentDistance(io.MousePos, center,
                        ImVec2(center.x + direction.first, center.y + direction.second));
                }
                if (distance <= (s.zoom >= 14 ? 6 : 9)) hits.emplace_back(distance, index);
            }
        });
        for (auto index : mast.antennas)
            if (providerBit(s.database.antennas()[index].provider) & s.providerMask()) ++visibleAntennas;
    }
    // Selection is drawn last so co-located, identical azimuths cannot obscure it.
    if (s.selected != noAntenna) {
        const auto& selected = s.database.antennas()[s.selected];
        const auto& mast = s.database.masts()[selected.mast];
        if (providerBit(selected.provider) & s.providerMask())
            canvas.forEachPosition(mast.longitude, mast.latitude, sectorLength + 10, [&](ImVec2 center) {
                if (s.zoom >= 14) drawSector(draw, selected, center, true);
                draw->AddCircle(center, 9, IM_COL32(255, 225, 70, 255), 0, 2.5f);
            });
    }
    for (const auto& point : mapPoints) {
        if (point.provider != MapProvider::Unspecified && !(providerBit(point.provider) & s.providerMask())) continue;
        canvas.forEachPosition(point.longitude, point.latitude, 6, [&](ImVec2 center) {
            draw->AddCircleFilled(center, 5, point.provider == MapProvider::Unspecified
                ? IM_COL32(255, 210, 90, 255) : providerColor(point.provider));
            draw->AddCircle(center, 5, IM_COL32(20, 25, 30, 255), 0, 1.5f);
        });
    }
    if (s.userPositionSet) canvas.forEachPosition(s.userLongitude, s.userLatitude, 10, [&](ImVec2 center) {
        const ImU32 cyan = IM_COL32(30, 255, 255, 255);
        draw->AddCircleFilled(center, 5, cyan);
        draw->AddCircle(center, 9, IM_COL32(15, 20, 25, 255), 0, 5);
        draw->AddCircle(center, 9, cyan, 0, 2);
        draw->AddText(ImVec2(center.x + 12, center.y - ImGui::GetFontSize() / 2), cyan, "You");
    });
    draw->PopClipRect();
    draw->AddRect(origin, end, IM_COL32(80, 100, 120, 255));
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        const double pointerY = (canvas.top + io.MousePos.y - origin.y) / world;
        ImGui::Text("Longitude: %.6f   Latitude: %.6f", longitude((canvas.left + io.MousePos.x - origin.x) / world), latitude(pointerY));
    } else ImGui::TextUnformatted("Longitude: --   Latitude: --");
    ImGui::Text("%zu visible masts / %zu antennas | Selected: yellow | User: cyan", visible.size(), visibleAntennas);
    if (aerialFallback) {
        const auto status = s.tiles.satelliteStatus();
        ImGui::TextWrapped("%sOffline map shown where aerial tiles are unavailable.", status.c_str());
    } else ImGui::TextDisabled("Map / aerial: (c) swisstopo | World: Natural Earth");

    if (!hits.empty()) {
        std::sort(hits.begin(), hits.end());
        const float closest = hits.front().first;
        s.pickCandidates.clear();
        for (const auto& hit : hits)
            if (hit.first <= closest + 1.5f && std::find(s.pickCandidates.begin(), s.pickCandidates.end(), hit.second) == s.pickCandidates.end())
                s.pickCandidates.push_back(hit.second);
        s.select(s.pickCandidates.front());
        if (s.pickCandidates.size() > 1) ImGui::OpenPopup("Choose antenna");
    }
    if (ImGui::BeginPopup("Choose antenna")) {
        ImGui::TextUnformatted("Overlapping antennas: select a row");
        std::vector<NearbyAntenna> rows;
        for (auto index : s.pickCandidates) rows.push_back({index, 0});
        drawAntennaRows(s, "overlap_antennas", rows, false, ImGui::GetTextLineHeightWithSpacing() * 10);
        if (ImGui::Button("Done")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
} // namespace phreakshow::detail
