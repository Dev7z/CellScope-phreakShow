#pragma once

#include <algorithm>
#include <cmath>

namespace phreakshow {
constexpr double pi = 3.14159265358979323846;
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
} // namespace phreakshow
