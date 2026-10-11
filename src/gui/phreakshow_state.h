#pragma once

// Internal state shared by the map canvas and its two companion panels.
#include "gui/antenna_database.h"
#include "gui/map_tiles.h"
#include "gui/map_projection.h"
#include <array>

namespace phreakshow::detail {
struct ShowState {
    AntennaDatabase database;
    MapTiles tiles;
    bool databaseLocated = false;
    char databasePath[1024] = "switzerland.db";
    std::array<bool, 4> providers{false, true, true, true};
    double mapX = 0.5 + 8.13 / 360.0;
    double mapY = mercatorY(46.8);
    int zoom = 8;
    bool satellite = false;
    bool dragged = false;
    double inputLongitude = 7.589772, inputLatitude = 47.547480;
    double userLongitude = 0, userLatitude = 0;
    bool userPositionSet = false;
    bool invalidUserPosition = false;
    size_t selected = noAntenna;
    std::vector<std::pair<std::string, std::string>> selectedFields;
    std::vector<size_t> pickCandidates;
    int nearestMode = 0; // 0 = k nearest; 1 = all within radius.
    int nearestCount = 50;
    double radiusKm = 2;
    bool nearestDirty = true;
    std::vector<NearbyAntenna> nearby;
    std::string tuneStatus;

    unsigned providerMask() const {
        unsigned mask = 0;
        for (size_t i = 0; i < providers.size(); ++i)
            if (providers[i]) mask |= 1u << i;
        return mask;
    }
    void select(size_t index) {
        if (index >= database.antennas().size()) return;
        selected = index;
        const auto provider = database.antennas()[index].provider;
        if (!providers[static_cast<size_t>(provider)]) {
            providers[static_cast<size_t>(provider)] = true;
            nearestDirty = true;
        }
        selectedFields = database.details(index);
        tuneStatus.clear();
    }
    void center(double longitude, double latitude, int targetZoom) {
        mapX = wrapX(0.5 + longitude / 360);
        mapY = mercatorY(latitude);
        zoom = targetZoom;
    }
};

ShowState& state();
void drawMapCanvas(ShowState& state);
// Shared antenna table used by co-located, nearby, and overlap-picking lists.
void drawAntennaRows(ShowState& state, const char* id,
                    const std::vector<NearbyAntenna>& rows, bool showDistance,
                    float height = 0);
} // namespace phreakshow::detail
