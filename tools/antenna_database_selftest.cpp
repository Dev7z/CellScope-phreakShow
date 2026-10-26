#include "gui/antenna_database.h"
#include "gui/map_projection.h"
#include <sqlite3.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <set>

using namespace phreakshow;

int main(int argc, char** argv) {
    const auto fixture = std::filesystem::temp_directory_path() /
        ("phreakshow-antennas-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
    sqlite3* db = nullptr;
    assert(sqlite3_open(fixture.string().c_str(), &db) == SQLITE_OK);
    const char* sql = R"SQL(
        CREATE TABLE switzerland (PK INTEGER PRIMARY KEY, longitude REAL, latitude REAL,
            "property.operator" TEXT, "property.frequency_mhz" INTEGER,
            "property.azimuth_deg" INTEGER, "property.pci" INTEGER, "property.site_id" INTEGER,
            "property.station" TEXT, "property.installation_type" TEXT);
        INSERT INTO switzerland VALUES
            (1, 8, 47, 'Sunrise', 791.2, 160, 389, 12766, 'A', 'Outdoor'),
            (2, 8, 47, 'Sunrise', 796, 230, 328, 12766, 'B', 'Indoor'),
            (3, 8, 47, 'Swisscom', 2135, 0, 18, 50, 'C', NULL),
            (4, 8.01, 47, 'Salt', 3649.98, NULL, NULL, NULL, 'D', 'indoor'),
            (5, 9, 48, 'Salt', 1800, 450, 8, 88, 'E', 'Outdoor'),
            (6, NULL, 47, 'Sunrise', 900, 0, 9, 90, 'Invalid', 'Outdoor'),
            (7, 8, 95, 'Swisscom', 900, 0, 9, 90, 'Invalid', 'Outdoor');
    )SQL";
    assert(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
    {
        AntennaDatabase antennas;
        assert(antennas.open(fixture));
        assert(antennas.antennas().size() == 5);
        assert(antennas.masts().size() == 3);
        assert(antennas.skippedRows() == 2);
        assert(antennas.masts()[0].antennas.size() == 3);
        assert(antennas.antennas()[0].frequencyMHz == 791.2);
        assert(antennas.antennas()[3].frequencyMHz == 3649.98);
        assert(std::isnan(antennas.antennas()[3].azimuth));
        assert(antennas.antennas()[3].pci == -1);
        assert(antennas.antennas()[4].azimuth == 90);
        const unsigned sunrise = providerBit(MapProvider::Sunrise);
        const unsigned salt = providerBit(MapProvider::Salt);
        const unsigned all = sunrise | salt | providerBit(MapProvider::Swisscom);
        assert(antennas.visibleMasts(7.9, 8.1, 46.9, 47.1, sunrise).size() == 1);
        assert(antennas.visibleMasts(7.9, 8.1, 46.9, 47.1, all).size() == 2);
        assert(antennas.visibleMasts(7.9, 8.1, 46.9, 47.1, 0).empty());
        assert(antennas.visibleMasts(170, -170, -90, 90, all).empty());
        assert(antennas.visibleMasts(-180, 180, -90, 90, all).size() == 3);
        assert(antennas.nearest(8, 47, all, 0, 0).size() == 3);
        assert(antennas.nearest(8, 47, all, 0, 1).size() == 4);
        assert(antennas.nearest(8, 47, sunrise, 100).size() == 2);
        assert(antennas.nearest(8, 47, all, 2)[1].antenna == 1);
        assert(antennas.nearest(8, 47, salt, 1)[0].antenna == 3);
        assert(antennas.nearest(8, 47, 0, 0).empty());
        assert(antennas.nearest(0, 91, all, 0).empty());
        const double unlimited = std::numeric_limits<double>::infinity();
        const auto firstCoordinate = antennas.nearest(8, 47, all, 1, unlimited, true);
        assert(firstCoordinate.size() == 3);
        for (const auto& row : firstCoordinate) {
            assert(antennas.antennas()[row.antenna].mast == 0);
            assert(row.distanceKm == 0);
        }
        const auto twoCoordinates = antennas.nearest(8, 47, all, 2, unlimited, true);
        assert(twoCoordinates.size() == 4);
        std::set<size_t> coordinates;
        for (const auto& row : twoCoordinates) coordinates.insert(antennas.antennas()[row.antenna].mast);
        assert(coordinates.size() == 2);
        // Filtering happens before counting coordinates and expanding rows.
        const auto sunriseGroup = antennas.nearest(8, 47, sunrise, 1, unlimited, true);
        assert(sunriseGroup.size() == 2);
        const auto saltGroups = antennas.nearest(8, 47, salt, 2, unlimited, true);
        assert(saltGroups.size() == 2 && saltGroups[0].antenna == 3 && saltGroups[1].antenna == 4);
        assert(antennas.nearest(8, 47, all, 100, unlimited, true).size() == 5);
        assert(antennas.nearest(8, 47, 0, 1, unlimited, true).empty());
        // Radius queries ignore coordinate counting, including a zero radius.
        assert(antennas.nearest(8, 47, all, 0, 0, true).size() == 3);
        assert(antennas.nearest(8, 47, all, 0, 1, true).size() == 4);
        assert(antennas.nearest(8, 47, all, 2, 0, true).size() == 3);
        // Indoor filtering happens before K ranking and coordinate expansion.
        assert(!antennas.antennas()[0].indoor && antennas.antennas()[1].indoor);
        assert(!antennas.antennas()[2].indoor && antennas.antennas()[3].indoor);
        const auto outdoor = antennas.nearest(8, 47, all, 0, unlimited, false, false);
        assert(outdoor.size() == 3);
        for (const auto& row : outdoor) assert(!antennas.antennas()[row.antenna].indoor);
        assert(antennas.nearest(8, 47, all, 0, 1, false, false).size() == 2);
        const auto outdoorSalt = antennas.nearest(8, 47, salt, 1, unlimited, true, false);
        assert(outdoorSalt.size() == 1 && outdoorSalt[0].antenna == 4);
        const auto outdoorGroups = antennas.nearest(8, 47, all, 2, unlimited, true, false);
        assert(outdoorGroups.size() == 3 && outdoorGroups.back().antenna == 4);
        assert(antennas.nearest(8, 47, all, 0, 0, true, false).size() == 2);
        const auto fields = antennas.details(0);
        assert(fields.size() == 10);
        assert(fields[0] == std::make_pair(std::string("PK"), std::string("1")));
        assert(fields[8].second == "A");
        assert(antennas.details(noAntenna).empty());
        assert(!antennas.open(fixture.string() + ".missing"));
        assert(antennas.antennas().empty());
        assert(!antennas.error().empty());
        assert(!std::filesystem::exists(fixture.string() + ".missing"));
    }
    std::filesystem::remove(fixture);
    assert(distanceKm(8, 47, 8, 47) == 0);
    assert(std::abs(distanceKm(0, 0, 1, 0) - 111.19508) < 0.001);
    assert(std::abs(distanceKm(179.9, 0, -179.9, 0) - 22.239) < 0.001);
    // Every radius-outline point must lie on the search's distance boundary,
    // including at the antimeridian and near the poles.
    for (const auto& center : {std::make_pair(7.587123, 47.560173),
                               std::make_pair(179.99, 0.0), std::make_pair(30.0, 84.9)}) {
        for (double radius : {0.0, 0.25, 2.0, 500.0, 15000.0}) {
            for (int bearing = 0; bearing < 360; bearing += 15) {
                const auto point = coordinateAtDistance(center.first, center.second, bearing, radius);
                assert(validMapCoordinates(point.first, point.second));
                assert(std::abs(distanceKm(center.first, center.second, point.first, point.second) - radius) < 1e-7);
            }
        }
    }
    for (double az : {0.0, 90.0, 180.0, 270.0}) {
        const auto direction = antennaDirection(az, 52);
        assert(std::abs(std::hypot(direction.first, direction.second) - 52) < 1e-9);
    }
    assert(antennaDirection(0, 52).second == -52);
    assert(std::abs(antennaDirection(90, 52).first - 52) < 1e-9);
    assert(std::abs(antennaDirection(180, 52).second - 52) < 1e-9);
    assert(std::abs(antennaDirection(270, 52).first + 52) < 1e-9);
    if (argc > 1) {
        const auto before = std::filesystem::last_write_time(argv[1]);
        AntennaDatabase real;
        const auto start = std::chrono::steady_clock::now();
        assert(real.open(argv[1]));
        assert(real.antennas().size() == 244823);
        assert(real.masts().size() == 20259);
        assert(real.skippedRows() == 0);
        const unsigned allProviders = providerBit(MapProvider::Sunrise) | providerBit(MapProvider::Swisscom) |
                                      providerBit(MapProvider::Salt) | providerBit(MapProvider::Unspecified);
        const auto allOutdoors = real.nearest(7.587123, 47.560173, allProviders, 0,
            std::numeric_limits<double>::infinity(), false, false);
        assert(allOutdoors.size() == 219046);
        const auto rows = real.nearest(8.393766, 46.827407,
            providerBit(MapProvider::Sunrise), 1);
        assert(rows.size() == 1 && rows[0].distanceKm == 0);
        assert(real.details(rows[0].antenna).size() == 21);
        const auto groups = real.nearest(8.393766, 46.827407,
            providerBit(MapProvider::Sunrise), 50, std::numeric_limits<double>::infinity(), true);
        std::set<size_t> groupMasts;
        for (const auto& row : groups) groupMasts.insert(real.antennas()[row.antenna].mast);
        assert(groupMasts.size() == 50 && groups.size() > 50);
        for (auto mast : groupMasts) {
            size_t expected = 0, actual = 0;
            for (auto antenna : real.masts()[mast].antennas)
                if (real.antennas()[antenna].provider == MapProvider::Sunrise) ++expected;
            for (const auto& row : groups) if (real.antennas()[row.antenna].mast == mast) ++actual;
            assert(expected == actual);
        }
        assert(std::filesystem::last_write_time(argv[1]) == before);
        std::cout << "Real database: " << real.antennas().size() << " antennas, "
                  << real.masts().size() << " unique masts loaded in "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
    }
    std::cout << "Antenna database: grouping, filters, nearest/radius, fractional MHz, NULLs, directions and read-only errors passed\n";
}
