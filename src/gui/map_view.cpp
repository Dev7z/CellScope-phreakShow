#include "gui/phreakshow_state.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace phreakshow::detail {
namespace {
constexpr int tileSize = 256;
constexpr int maxZoom = 18;
constexpr ImU32 selectedColor = IM_COL32(255, 225, 70, 255);
constexpr ImU32 hoverColor = IM_COL32(210, 130, 20, 255);

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

void drawSearchRadius(ImDrawList* draw, const Canvas& canvas, const ShowState& s) {
    if (!s.userPositionSet || s.nearestMode != 1 || s.radiusKm <= 0 ||
        s.radiusKm >= pi * earthRadiusKm) return; // Larger searches cover the whole globe.
    const double pixelsPerKm = canvas.world / (2 * pi * earthRadiusKm *
        std::cos(std::clamp(s.userLatitude, -maxLatitude, maxLatitude) * pi / 180));
    const int steps = static_cast<int>(std::clamp(std::ceil(2 * pi * s.radiusKm * pixelsPerKm / 6),
                                                 64.0, 4096.0));
    std::vector<std::pair<ImVec2, ImVec2>> edges;
    auto previous = coordinateAtDistance(s.userLongitude, s.userLatitude, 0, s.radiusKm);
    double previousX = wrapX(0.5 + previous.first / 360);
    double previousY = mercatorY(previous.second) * canvas.world;
    for (int step = 1; step <= steps; ++step) {
        const auto point = coordinateAtDistance(s.userLongitude, s.userLatitude,
                                                step * 360.0 / steps, s.radiusKm);
        double x = wrapX(0.5 + point.first / 360);
        // Use the short segment across the antimeridian, then repeat it with
        // the tiles. Never draw a chord spanning the entire world.
        x += std::round(previousX - x);
        const double y = mercatorY(point.second) * canvas.world;
        if (std::max(previousY, y) >= canvas.top && std::min(previousY, y) <= canvas.top + canvas.size.y &&
            !(std::min(previous.second, point.second) > maxLatitude ||
              std::max(previous.second, point.second) < -maxLatitude)) {
            const double first = std::ceil((canvas.left - std::max(previousX, x) * canvas.world) / canvas.world);
            const double last = std::floor((canvas.left + canvas.size.x - std::min(previousX, x) * canvas.world) / canvas.world);
            for (double copy = first; copy <= last; ++copy) {
                edges.push_back({
                    ImVec2(canvas.origin.x + static_cast<float>((previousX + copy) * canvas.world - canvas.left),
                           canvas.origin.y + static_cast<float>(previousY - canvas.top)),
                    ImVec2(canvas.origin.x + static_cast<float>((x + copy) * canvas.world - canvas.left),
                           canvas.origin.y + static_cast<float>(y - canvas.top))});
            }
        }
        previous = point;
        previousX = x;
        previousY = y;
    }
    for (const auto& edge : edges) draw->AddLine(edge.first, edge.second, IM_COL32(15, 25, 30, 220), 4);
    for (const auto& edge : edges) draw->AddLine(edge.first, edge.second, IM_COL32(30, 230, 255, 230), 2);
}

void drawSector(ImDrawList* draw, const Antenna& antenna, ImVec2 center, float length, ImU32 highlight = 0) {
    const bool emphasized = highlight != 0;
    const ImU32 color = emphasized ? highlight : providerColor(antenna.provider);
    if (!std::isfinite(antenna.azimuth)) {
        // Missing direction remains selectable at the mast's center.
        draw->AddCircle(center, emphasized ? 9 : 6, color, 0, emphasized ? 3 : 1.5f);
        return;
    }
    const auto direction = antennaDirection(antenna.azimuth, length);
    const ImVec2 end(center.x + static_cast<float>(direction.first), center.y + static_cast<float>(direction.second));
    // A wider hover stroke remains visible beneath an identical selected ray.
    const bool hovering = highlight == hoverColor;
    draw->AddLine(center, end, IM_COL32(15, 20, 25, 255), hovering ? 11 : emphasized ? 7 : 4);
    draw->AddLine(center, end, color, hovering ? 8 : emphasized ? 4 : 2);
    draw->AddCircleFilled(end, hovering ? 7 : emphasized ? 4 : 2.5f, color);
}

// Keep each group compact without letting controls run into the next column.
void nextToPrevious(float right, const char* label, bool checkbox = false) {
    const auto& style = ImGui::GetStyle();
    const float width = ImGui::CalcTextSize(label).x + (checkbox
        ? ImGui::GetFrameHeight() + style.ItemInnerSpacing.x : 2 * style.FramePadding.x);
    if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right) ImGui::SameLine();
}

void navigationControls(ShowState& s) {
    ImGui::SeparatorText("Map controls");
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (buttonEllipsis("+")) s.zoom = std::min(maxZoom, s.zoom + 1);
    nextToPrevious(right, "-");
    if (buttonEllipsis("-")) s.zoom = std::max(0, s.zoom - 1);
    nextToPrevious(right, "World");
    if (buttonEllipsis("World")) s.center(10, 35, 2);
    nextToPrevious(right, "Switzerland");
    if (buttonEllipsis("Switzerland")) s.center(8.13, 46.8, 8);
    nextToPrevious(right, "Basel SBB");
    if (buttonEllipsis("Basel SBB")) s.center(7.589772, 47.547480, 13);
    nextToPrevious(right, "Zoom 18");
    ImGui::Text("Zoom %d", s.zoom);

    ImGui::TextAligned(0, -FLT_MIN, "Azimuth line length");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##azimuth_line_length", &s.sectorLength, 8, 240, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::BeginTable("navigation_toggles", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        checkboxEllipsis("Map navigation with hotkeys", &s.mapHotkeys);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "Active while the map is focused or hovered, except while editing a field.\n"
            "WASD: pan | 1: Swisscom | 2: Sunrise | 3: Salt\n"
            "Q/E: shorten/lengthen azimuth lines | +/-: zoom | X: aerial (zoom 13+)\n"
            "C: user point at map center | V: user point at mouse (over map)");
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(s.zoom < 13);
        checkboxEllipsis("Satellite / aerial", &s.satellite);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Available at zoom 13+. Basel aerial coverage is bundled for offline use.");
        ImGui::EndTable();
    }
}

void coordinateControls(ShowState& s) {
    ImGui::SeparatorText("Your coordinates");
    if (!ImGui::BeginTable("user_coordinates", 2, ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) return;
    ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthStretch, 45);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch, 55);
    ImGui::TableNextColumn();
    ImGui::TextAligned(0, -FLT_MIN, "Longitude");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputDouble("##longitude", &s.inputLongitude, 0, 0, "%.6f");
    ImGui::TextAligned(0, -FLT_MIN, "Latitude");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputDouble("##latitude", &s.inputLatitude, 0, 0, "%.6f");
    ImGui::TableNextColumn();
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (buttonEllipsis("Set user point")) s.setUserPosition(s.inputLongitude, s.inputLatitude);
    nextToPrevious(right, "Go to user");
    ImGui::BeginDisabled(!s.userPositionSet);
    if (buttonEllipsis("Go to user")) s.center(s.userLongitude, s.userLatitude, 13);
    ImGui::EndDisabled();
    if (s.invalidUserPosition) ImGui::TextWrapped("Longitude must be -180..180, latitude -90..90.");
    if (s.userPositionSet) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.1f, 1, 1, 1));
        ImGui::TextAligned(0, -FLT_MIN, "User: %.6f, %.6f", s.userLongitude, s.userLatitude);
        ImGui::PopStyleColor();
    }
    ImGui::EndTable();
}

// Keep native text editing, selection and caret behavior; only the idle display
// uses ellipsis. Render the text separately so no shortened path reaches the buffer.
void inputPath(const char* label, char* path, size_t capacity) {
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
    ImGui::InputText(label, path, capacity);
    ImGui::PopStyleColor();
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    const ImVec2 start(min.x + padding.x, min.y + padding.y);
    const ImVec2 end(max.x - padding.x, max.y - padding.y);
    auto* draw = ImGui::GetWindowDrawList();
    if (ImGui::IsItemActive()) {
        const auto* input = ImGui::GetInputTextState(ImGui::GetItemID());
        const ImVec4 clip(min.x, min.y, max.x, max.y);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
            ImVec2(start.x - (input ? input->Scroll.x : 0), start.y), color, path, nullptr, 0, &clip);
    } else {
        ImGui::RenderTextEllipsis(draw, start, end, end.x, path, path + std::strlen(path), nullptr);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", path);
    }
}

void databaseControls(ShowState& s) {
    ImGui::SeparatorText("Database");
    ImGui::SetNextItemWidth(-1);
    inputPath("##database_path", s.databasePath, sizeof(s.databasePath));
    if (buttonEllipsis("Load / reload database")) loadDatabase(s);
    if (buttonEllipsis("Retry map / aerial tiles")) s.tiles.retryOnline();
}

void toolbar(ShowState& s) {
    const float width = ImGui::GetContentRegionAvail().x, font = ImGui::GetFontSize();
    const int columns = width >= font * 42 ? 3 : width >= font * 28 ? 2 : 1;
    // Each responsive layout keeps its own saved splitter positions.
    ImGui::PushID(columns);
    if (ImGui::BeginTable("map_controls", columns, ImGuiTableFlags_SizingStretchProp |
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Navigation", ImGuiTableColumnFlags_WidthStretch, columns == 3 ? 46 : 55);
        if (columns > 1) ImGui::TableSetupColumn("Coordinates", ImGuiTableColumnFlags_WidthStretch, columns == 3 ? 28 : 45);
        if (columns == 3) ImGui::TableSetupColumn("Database", ImGuiTableColumnFlags_WidthStretch, 26);
        ImGui::TableNextColumn();
        navigationControls(s);
        if (columns > 1) ImGui::TableNextColumn();
        coordinateControls(s);
        if (columns == 3) ImGui::TableNextColumn();
        databaseControls(s);
        ImGui::EndTable();
    }
    ImGui::PopID();
    if (!s.database.error().empty()) ImGui::TextWrapped("Database: %s", s.database.error().c_str());
    if (!s.tiles.hasOfflineMap()) ImGui::TextWrapped("Offline tiles missing: place assets/map_tiles beside the executable.");
}

bool keyboardNavigation(ShowState& s, bool canvasHovered) {
    const auto& io = ImGui::GetIO();
    if (!s.mapHotkeys || io.WantTextInput || ImGui::IsAnyItemActive() || io.KeyCtrl || io.KeyAlt || io.KeySuper ||
        (!canvasHovered && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return false;
    if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) s.zoom = std::min(maxZoom, s.zoom + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) s.zoom = std::max(0, s.zoom - 1);
    if (s.zoom >= 13 && ImGui::IsKeyPressed(ImGuiKey_X, false)) s.satellite = !s.satellite;
    const float seconds = std::clamp(io.DeltaTime, 0.0f, 0.1f);
    const float lineDelta = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
    s.sectorLength = std::clamp(s.sectorLength + lineDelta * 100 * seconds, 8.0f, 240.0f);
    double dx = (ImGui::IsKeyDown(ImGuiKey_D) ? 1 : 0) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1 : 0);
    double dy = (ImGui::IsKeyDown(ImGuiKey_S) ? 1 : 0) - (ImGui::IsKeyDown(ImGuiKey_W) ? 1 : 0);
    const double length = std::hypot(dx, dy);
    if (length > 0) {
        const double step = 400 * seconds / (tileSize * (1 << s.zoom)) / length;
        s.mapX += dx * step;
        s.mapY += dy * step;
    }
    for (const auto& toggle : {std::pair{ImGuiKey_1, MapProvider::Swisscom},
                              std::pair{ImGuiKey_2, MapProvider::Sunrise},
                              std::pair{ImGuiKey_3, MapProvider::Salt}}) {
        if (ImGui::IsKeyPressed(toggle.first, false)) {
            auto& enabled = s.providers[static_cast<size_t>(toggle.second)];
            enabled = !enabled;
        }
    }
    return true;
}
} // namespace

void drawMapContent(ShowState& s) {
    ImVec2 size = ImGui::GetContentRegionAvail();
    const int footerLines = 4 + (s.onlyNearbyOnMap ? 1 : 0);
    size.y -= footerLines * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    if (size.x < 1 || size.y < 40) return;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##map_canvas", size, ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    const auto& io = ImGui::GetIO();
    const bool keys = keyboardNavigation(s, hovered);
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
    if (keys && ImGui::IsKeyPressed(ImGuiKey_C, false)) s.setUserPosition(longitude(s.mapX), latitude(s.mapY));
    if (keys && hovered && ImGui::IsKeyPressed(ImGuiKey_V, false))
        s.setUserPosition(longitude((canvas.left + io.MousePos.x - origin.x) / world),
                          latitude((canvas.top + io.MousePos.y - origin.y) / world));
    s.refreshNearby();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    draw->PushClipRect(origin, end, true);
    draw->AddRectFilled(origin, end, IM_COL32(17, 27, 37, 255));
    bool tileFallback = false;
    for (int ty = std::max(0, static_cast<int>(std::floor(canvas.top / tileSize)));
         ty < count && ty * tileSize < canvas.top + size.y; ++ty) {
        for (int tx = static_cast<int>(std::floor(canvas.left / tileSize)); tx * tileSize < canvas.left + size.x; ++tx) {
            const auto tile = s.tiles.image(s.zoom, (tx % count + count) % count, ty, s.satellite);
            const ImVec2 a(origin.x + static_cast<float>(tx * tileSize - canvas.left),
                           origin.y + static_cast<float>(ty * tileSize - canvas.top));
            const ImVec2 b(a.x + tileSize, a.y + tileSize);
            if (tile.texture) draw->AddImage(static_cast<ImTextureID>(tile.texture), a, b, tile.uv0, tile.uv1);
            else draw->AddRect(a, b, IM_COL32(65, 80, 95, 255));
            if (s.zoom >= 13 && !tile.detailed) tileFallback = true;
        }
    }

    drawSearchRadius(draw, canvas, s);
    const double margin = s.zoom >= 14 ? s.sectorLength + 10 : 10;
    const double westX = (canvas.left - margin) / world, eastX = (canvas.left + size.x + margin) / world;
    const bool wholeWorld = eastX - westX >= 1;
    const auto visible = s.database.visibleMasts(wholeWorld ? -180 : longitude(westX),
                        wholeWorld ? 180 : longitude(eastX), latitude((canvas.top + size.y + margin) / world),
                        latitude((canvas.top - margin) / world), s.providerMask());
    std::vector<std::pair<float, size_t>> hits;
    size_t visibleAntennas = 0;
    size_t visibleMasts = 0;
    const float mastRadius = s.zoom < 11 ? 2.0f : 4.0f;
    for (auto mastIndex : visible) {
        const auto& mast = s.database.masts()[mastIndex];
        unsigned mastProviders = 0;
        for (auto index : mast.antennas)
            if (s.antennaOnMap(index)) mastProviders |= providerBit(s.database.antennas()[index].provider);
        if (!(mastProviders & s.providerMask())) continue;
        ++visibleMasts;
        // A single point per exact coordinate, irrespective of the antenna count.
        MapProvider first = MapProvider::Unspecified;
        unsigned companies = 0;
        for (auto p : {MapProvider::Sunrise, MapProvider::Swisscom, MapProvider::Salt, MapProvider::Unspecified}) {
            if (mastProviders & s.providerMask() & providerBit(p)) {
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
                if (!(providerBit(antenna.provider) & s.providerMask()) || !s.antennaOnMap(index)) continue;
                if (s.zoom >= 14) drawSector(draw, antenna, center, s.sectorLength);
                if (!hovered) continue;
                float distance = centerDistance;
                if (s.zoom >= 14 && std::isfinite(antenna.azimuth)) {
                    const auto direction = antennaDirection(antenna.azimuth, s.sectorLength);
                    distance = segmentDistance(io.MousePos, center,
                        ImVec2(center.x + direction.first, center.y + direction.second));
                }
                if (distance <= (s.zoom >= 14 ? 6 : 9)) hits.emplace_back(distance, index);
            }
        });
        for (auto index : mast.antennas)
            if ((providerBit(s.database.antennas()[index].provider) & s.providerMask()) && s.antennaOnMap(index)) ++visibleAntennas;
    }
    if (!hits.empty()) {
        std::sort(hits.begin(), hits.end());
        s.hover(hits.front().second);
    } else if (hovered) s.hover(noAntenna);
    const auto highlightAntennas = [&](bool hovering, ImU32 color) {
        for (auto mastIndex : visible) {
            const auto& mast = s.database.masts()[mastIndex];
            std::vector<size_t> highlighted;
            for (auto index : mast.antennas) {
                if (!s.antennaOnMap(index) || !(providerBit(s.database.antennas()[index].provider) & s.providerMask())) continue;
                const bool selected = s.isAntennaSelected(index);
                if (hovering ? s.isAntennaHovered(index) && !selected : selected) highlighted.push_back(index);
            }
            if (highlighted.empty()) continue;
            canvas.forEachPosition(mast.longitude, mast.latitude, s.sectorLength + 14, [&](ImVec2 center) {
                if (s.zoom >= 14)
                    for (auto index : highlighted) drawSector(draw, s.database.antennas()[index], center, s.sectorLength, color);
                draw->AddCircle(center, hovering ? 13 : 9, color, 0, 2.5f);
            });
        }
    };
    highlightAntennas(true, hoverColor);
    // Selected rays remain visible where frequency groups share a direction.
    highlightAntennas(false, selectedColor);
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
        char label[96] = "You";
        if (s.nearestMode == 1)
            std::snprintf(label, sizeof(label), "You | Radius: %.3f km%s", s.radiusKm,
                          s.radiusKm >= pi * earthRadiusKm ? " (whole world)" : "");
        draw->AddText(ImVec2(center.x + 12, center.y - ImGui::GetFontSize() / 2), cyan, label);
    });
    const ImVec2 crosshair(origin.x + size.x / 2, origin.y + size.y / 2);
    const float arm = std::max(8.0f, ImGui::GetFontSize() * 0.7f);
    const ImU32 grey = IM_COL32(220, 220, 220, 255);
    draw->AddLine(ImVec2(crosshair.x - arm, crosshair.y), ImVec2(crosshair.x + arm, crosshair.y), IM_COL32(20, 25, 30, 255), 4);
    draw->AddLine(ImVec2(crosshair.x, crosshair.y - arm), ImVec2(crosshair.x, crosshair.y + arm), IM_COL32(20, 25, 30, 255), 4);
    draw->AddLine(ImVec2(crosshair.x - arm, crosshair.y), ImVec2(crosshair.x + arm, crosshair.y), grey, 2);
    draw->AddLine(ImVec2(crosshair.x, crosshair.y - arm), ImVec2(crosshair.x, crosshair.y + arm), grey, 2);
    draw->PopClipRect();
    draw->AddRect(origin, end, IM_COL32(80, 100, 120, 255));
    ImGui::TextAligned(0, -FLT_MIN, "Center: longitude %.6f   latitude %.6f", longitude(s.mapX), latitude(s.mapY));
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        const double pointerY = (canvas.top + io.MousePos.y - origin.y) / world;
        ImGui::TextAligned(0, -FLT_MIN, "Cursor: longitude %.6f   latitude %.6f", longitude((canvas.left + io.MousePos.x - origin.x) / world), latitude(pointerY));
    } else ImGui::TextAligned(0, -FLT_MIN, "Cursor: longitude --   latitude --");
    ImGui::TextAligned(0, -FLT_MIN, "%zu masts / %zu antennas | Selected: yellow | Hover: amber | User: cyan", visibleMasts, visibleAntennas);
    if (s.onlyNearbyOnMap) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextAligned(0, -FLT_MIN, "Map restricted to nearby table results.");
        ImGui::PopStyleColor();
    }
    if (tileFallback) {
        const auto status = s.tiles.onlineStatus(s.satellite);
        ImGui::TextAligned(0, -FLT_MIN, "%sLower-resolution map shown until detailed tiles are available.", status.c_str());
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextAligned(0, -FLT_MIN, "Map / aerial: (c) swisstopo | World: Natural Earth");
        ImGui::PopStyleColor();
    }

    if (clicked && !hits.empty()) {
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

namespace {
float mapFilterHeight(float width) {
    const auto& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight();
    float rowWidth = 0;
    int rows = 1;
    for (const char* label : {"Sunrise", "Swisscom", "Salt", "Include indoor"}) {
        const float itemWidth = height + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
        const float spacing = rowWidth > 0 ? style.ItemSpacing.x : 0;
        if (rowWidth > 0 && rowWidth + spacing + itemWidth > width) {
            ++rows;
            rowWidth = itemWidth;
        } else {
            rowWidth += spacing + itemWidth;
        }
    }
    return rows * height + (rows - 1) * style.ItemSpacing.y;
}

void verticalSplitter(const char* id, float& topHeight, float availableHeight, float minimumTop, float minimumBottom) {
    const float thickness = std::max(5.0f, ImGui::GetStyle().ItemSpacing.y);
    ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), thickness));
    const bool active = ImGui::IsItemActive();
    if (active || ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (active) topHeight = std::clamp(topHeight + ImGui::GetIO().MouseDelta.y,
        minimumTop, std::max(minimumTop, availableHeight - minimumBottom));
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
        ImGui::GetColorU32(active ? ImGuiCol_SeparatorActive : ImGui::IsItemHovered()
            ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
}
}

void drawMapCanvas(ShowState& s) {
    const auto& style = ImGui::GetStyle();
    const float line = ImGui::GetFrameHeightWithSpacing();
    const float available = ImGui::GetContentRegionAvail().y;
    const float filterHeight = mapFilterHeight(std::max(1.0f, ImGui::GetContentRegionAvail().x - 2 * style.WindowPadding.x));
    const float minimumSettings = line + filterHeight + style.ItemSpacing.y;
    const float minimumMap = line * 7; // reserve the canvas, footer, padding and divider
    if (s.mapSettingsHeight <= 0) s.mapSettingsHeight = line * 9;
    const float settingsHeight = std::clamp(s.mapSettingsHeight, std::min(minimumSettings, available * .4f),
        std::max(minimumSettings, available - minimumMap));
    s.mapSettingsHeight = settingsHeight;
    // Filters use only their actual checkbox rows; all remaining settings height
    // belongs to the scrollable controls above them, with no blank footer.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x, 0));
    const bool settingsVisible = ImGui::BeginChild("map_settings", ImVec2(0, settingsHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    if (settingsVisible) {
        const float controlsAvailable = ImGui::GetContentRegionAvail().y;
        const float controlsHeight = std::max(line, controlsAvailable - filterHeight - style.ItemSpacing.y);
        if (ImGui::BeginChild("map_controls_top", ImVec2(0, controlsHeight))) toolbar(s);
        ImGui::EndChild();
        if (ImGui::BeginChild("map_filters", ImVec2(0, filterHeight), ImGuiChildFlags_None,
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            for (auto p : {MapProvider::Sunrise, MapProvider::Swisscom, MapProvider::Salt}) {
                if (p != MapProvider::Sunrise) nextToPrevious(right, providerName(p), true);
                ImGui::PushStyleColor(ImGuiCol_Text, providerColor(p));
                checkboxEllipsis(providerName(p), &s.providers[static_cast<size_t>(p)]);
                ImGui::PopStyleColor();
            }
            nextToPrevious(right, "Include indoor", true);
            checkboxEllipsis("Include indoor", &s.includeIndoor);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    verticalSplitter("map_settings_splitter", s.mapSettingsHeight, available, minimumSettings, minimumMap);
    if (ImGui::BeginChild("map_view", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding)) drawMapContent(s);
    ImGui::EndChild();
}
} // namespace phreakshow::detail
