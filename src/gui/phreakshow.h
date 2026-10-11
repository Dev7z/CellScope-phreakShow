#pragma once

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
} // namespace phreakshow

// Draw the contents of the phreakShow application tab.
void drawPhreakShow();

// Release map textures while the OpenGL context is still current.
void shutdownPhreakShow();
