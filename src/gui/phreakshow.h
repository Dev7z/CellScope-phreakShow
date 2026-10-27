#pragma once

#include <cmath>
#include <vector>

namespace phreakshow {
enum class MapProvider { Unspecified, Sunrise, Swisscom, Salt };

struct MapPoint {
    double longitude;
    double latitude;
    MapProvider provider = MapProvider::Unspecified;
};

// Shared marker list: other functions may add, update, remove or clear points.
// Access this list on the UI thread, including calls to addMapPoint.
extern std::vector<MapPoint> mapPoints;

// Queue a point for drawing each frame, in WGS84 degrees (longitude first).
// Example: addMapPoint(8.393766, 46.827407, MapProvider::Swisscom).
// Returns false for non-finite or out-of-range coordinates.
bool addMapPoint(double longitude, double latitude,
                 MapProvider provider = MapProvider::Unspecified);
inline bool validMapCoordinates(double longitude, double latitude) {
    return std::isfinite(longitude) && std::isfinite(latitude) &&
           longitude >= -180.0 && longitude <= 180.0 &&
           latitude >= -90.0 && latitude <= 90.0;
}
} // namespace phreakshow

// Draw the contents of the phreakShow application tab.
struct App;
void drawPhreakShow(App& app, unsigned int defaultDock);

// Release map textures while the OpenGL context is still current.
void shutdownPhreakShow();
