#pragma once

#include <algorithm>
#include <cmath>
#include <utility>

namespace phreakshow {
constexpr double pi = 3.14159265358979323846;
constexpr double earthRadiusKm = 6371.0088;
constexpr double maxLatitude = 85.0511287798066;
inline double wrapX(double x) { return x - std::floor(x); }
inline double longitude(double x) { return wrapX(x) * 360.0 - 180.0; }
inline double latitude(double y) {
    return std::atan(std::sinh(pi * (1.0 - 2.0 * std::clamp(y, 0.0, 1.0)))) * 180.0 / pi;
}
inline double mercatorY(double lat) {
    const double radians = std::clamp(lat, -maxLatitude, maxLatitude) * pi / 180.0;
    return (1.0 - std::asinh(std::tan(radians)) / pi) * 0.5;
}
// Great-circle destination, using the same sphere as the nearby search.
// Bearing is clockwise from north; returned coordinates are longitude first.
inline std::pair<double, double> coordinateAtDistance(double lon, double lat,
                                                     double bearing, double km) {
    const double angle = km / earthRadiusKm, azimuth = bearing * pi / 180;
    const double start = lat * pi / 180;
    const double end = std::asin(std::clamp(std::sin(start) * std::cos(angle) +
        std::cos(start) * std::sin(angle) * std::cos(azimuth), -1.0, 1.0));
    const double delta = std::atan2(std::sin(azimuth) * std::sin(angle) * std::cos(start),
                                   std::cos(angle) - std::sin(start) * std::sin(end));
    return {longitude(0.5 + lon / 360 + delta / (2 * pi)), end * 180 / pi};
}
} // namespace phreakshow
