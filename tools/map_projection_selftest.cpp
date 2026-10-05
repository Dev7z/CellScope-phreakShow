#include "gui/map_projection.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace phreakshow;
    for (double lat : {-maxLatitude, -60.0, -10.0, 0.0, 47.3769, 60.0, maxLatitude})
        assert(std::abs(latitude(mercatorY(lat)) - lat) < 1e-9);
    assert(std::abs(latitude(0) - maxLatitude) < 1e-9);
    assert(std::abs(latitude(1) + maxLatitude) < 1e-9);
    assert(longitude(0.5) == 0);
    assert(longitude(0) == -180);
    assert(longitude(1) == -180);
    assert(longitude(-0.25) == 90);
    assert(longitude(1.25) == -90);
    assert(mercatorY(90) == mercatorY(maxLatitude));
    assert(mercatorY(-90) == mercatorY(-maxLatitude));
    assert(latitude(-1) == latitude(0));
    assert(latitude(2) == latitude(1));
    std::cout << "Map projection: poles, equator, antimeridian and round trips passed\n";
}
