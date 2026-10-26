#pragma once

// Internal state shared by the map canvas and its companion panels.
#include "gui/antenna_database.h"
#include "gui/map_tiles.h"
#include "gui/map_projection.h"
#include "audio/background_music.h"
#include <array>
#include <map>
#include <optional>

namespace phreakshow::detail {
// Official address index: Spiegelgasse 1, 4051 Basel (swisstopo).
constexpr double defaultUserLongitude = 7.587123394012451;
constexpr double defaultUserLatitude = 47.56017303466797;
struct FrequencyGroup {
    double frequencyMHz;
    std::vector<size_t> antennas;
};
struct ShowState {
    bool intercomActive = true;
    BackgroundMusic backgroundMusic;
    AntennaDatabase database;
    MapTiles tiles;
    bool databaseLocated = false;
    char databasePath[1024] = "switzerland.db";
    std::array<bool, 4> providers{false, true, true, true};
    std::array<bool, 4> nearbyProviders{false, true, true, true};
    double mapX = 0.5 + 8.13 / 360.0;
    double mapY = mercatorY(46.8);
    int zoom = 8;
    bool satellite = false;
    float sectorLength = 52;
    bool mapHotkeys = false;
    bool dragged = false;
    double inputLongitude = defaultUserLongitude, inputLatitude = defaultUserLatitude;
    double userLongitude = defaultUserLongitude, userLatitude = defaultUserLatitude;
    bool userPositionSet = true;
    bool invalidUserPosition = false;
    size_t selected = noAntenna;
    std::optional<double> selectedFrequency;
    size_t hovered = noAntenna;
    std::optional<double> hoveredFrequency;
    int hoveredFrame = -1;
    std::vector<std::pair<std::string, std::string>> selectedFields;
    std::vector<size_t> pickCandidates;
    int nearestMode = 0; // 0 = k nearest; 1 = all within radius.
    int nearestCount = 50;
    bool countCoordinatesOnce = false;
    double radiusKm = 2;
    bool nearestDirty = true;
    bool onlyNearbyOnMap = false;
    std::vector<NearbyAntenna> nearby;
    std::vector<FrequencyGroup> frequencies;
    std::vector<bool> nearbyOnMap;
    std::vector<size_t> nearbyCoordinateGroups;
    size_t nearbyCoordinateCount = 0;
    std::string tuneStatus;

    unsigned providerMask() const {
        unsigned mask = 0;
        for (size_t i = 0; i < providers.size(); ++i)
            if (providers[i]) mask |= 1u << i;
        return mask;
    }
    unsigned nearbyProviderMask() const {
        unsigned mask = 0;
        for (size_t i = 0; i < nearbyProviders.size(); ++i)
            if (nearbyProviders[i]) mask |= 1u << i;
        return mask;
    }
    void hover(size_t index) {
        hovered = index;
        hoveredFrequency.reset();
        hoveredFrame = ImGui::GetFrameCount();
    }
    void hoverFrequency(double frequency) {
        hovered = noAntenna;
        hoveredFrequency = frequency;
        hoveredFrame = ImGui::GetFrameCount();
    }
    size_t hoveredAntenna() const {
        // Tables may be submitted after the map. Retain their hover for one
        // frame, but clear it as soon as the mouse leaves the originating row.
        return hoveredFrame >= ImGui::GetFrameCount() - 1 ? hovered : noAntenna;
    }
    bool antennaOnMap(size_t index) const {
        return !onlyNearbyOnMap || isNearby(index);
    }
    bool isNearby(size_t index) const {
        return userPositionSet && index < nearbyOnMap.size() && nearbyOnMap[index];
    }
    bool isAntennaSelected(size_t index) const {
        return selectedFrequency ? isNearby(index) && database.antennas()[index].frequencyMHz == *selectedFrequency : selected == index;
    }
    bool isAntennaHovered(size_t index) const {
        if (hoveredFrame < ImGui::GetFrameCount() - 1) return false;
        return hoveredFrequency ? isNearby(index) && database.antennas()[index].frequencyMHz == *hoveredFrequency : hovered == index;
    }
    void refreshNearby() {
        if (!nearestDirty) return;
        nearby = userPositionSet ? database.nearest(userLongitude, userLatitude, nearbyProviderMask(),
            nearestMode == 0 ? nearestCount : 0,
            nearestMode == 0 ? std::numeric_limits<double>::infinity() : radiusKm,
            nearestMode == 0 && countCoordinatesOnce) : std::vector<NearbyAntenna>{};
        nearbyOnMap.assign(database.antennas().size(), false);
        nearbyCoordinateGroups.assign(database.masts().size(), noAntenna);
        nearbyCoordinateCount = 0;
        std::map<double, std::vector<size_t>> groups;
        for (const auto& row : nearby) {
            nearbyOnMap[row.antenna] = true;
            auto& group = nearbyCoordinateGroups[database.antennas()[row.antenna].mast];
            if (group == noAntenna) group = nearbyCoordinateCount++;
            groups[database.antennas()[row.antenna].frequencyMHz].push_back(row.antenna);
        }
        frequencies.clear();
        for (auto& group : groups) frequencies.push_back({group.first, std::move(group.second)});
        if (selectedFrequency && !groups.count(*selectedFrequency)) selectedFrequency.reset();
        nearestDirty = false;
    }
    void setUserPosition(double longitude, double latitude) {
        invalidUserPosition = !validMapCoordinates(longitude, latitude);
        if (invalidUserPosition) return;
        inputLongitude = userLongitude = longitude;
        inputLatitude = userLatitude = latitude;
        userPositionSet = true;
        nearestDirty = true;
    }
    void select(size_t index) {
        if (index >= database.antennas().size()) return;
        selected = index;
        selectedFrequency.reset();
        const auto provider = database.antennas()[index].provider;
        if (!providers[static_cast<size_t>(provider)]) {
            providers[static_cast<size_t>(provider)] = true;
        }
        selectedFields = database.details(index);
        tuneStatus.clear();
    }
    void selectFrequency(double frequency) {
        for (const auto& group : frequencies) {
            if (group.frequencyMHz != frequency || group.antennas.empty()) continue;
            select(group.antennas.front());
            selectedFrequency = frequency;
            for (auto index : group.antennas)
                providers[static_cast<size_t>(database.antennas()[index].provider)] = true;
            break;
        }
    }
    void center(double longitude, double latitude, int targetZoom) {
        mapX = wrapX(0.5 + longitude / 360);
        mapY = mercatorY(latitude);
        zoom = targetZoom;
    }
};

ShowState& state();
void loadDatabase(ShowState& state);
void drawMapCanvas(ShowState& state);
// Preserve widget behavior while fitting labels into narrow panes.
bool buttonEllipsis(const char* label);
bool checkboxEllipsis(const char* label, bool* value);
// Shared antenna table used by co-located, nearby, and overlap-picking lists.
void drawAntennaRows(ShowState& state, const char* id,
                    const std::vector<NearbyAntenna>& rows, bool showDistance,
                    float height = 0, bool colorByCoordinates = false);
} // namespace phreakshow::detail
