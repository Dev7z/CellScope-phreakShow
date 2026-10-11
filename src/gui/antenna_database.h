#pragma once

#include "gui/phreakshow.h"
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;

namespace phreakshow {
constexpr size_t noAntenna = std::numeric_limits<size_t>::max();

struct Antenna {
    int64_t id = 0;
    size_t mast = 0;
    MapProvider provider = MapProvider::Unspecified;
    double frequencyMHz = 0; // SQLite INTEGER columns can contain fractional MHz.
    double azimuth = std::numeric_limits<double>::quiet_NaN();
    int64_t pci = -1;
    int64_t siteId = -1;
};

struct Mast {
    double longitude = 0, latitude = 0;
    unsigned providers = 0;
    std::vector<size_t> antennas;
};

struct NearbyAntenna {
    size_t antenna;
    double distanceKm;
};

unsigned providerBit(MapProvider provider);
const char* providerName(MapProvider provider);
double distanceKm(double lon1, double lat1, double lon2, double lat2);
// Azimuth is clockwise from north; screen Y grows downwards.
std::pair<double, double> antennaDirection(double azimuth, double length);

// Read-only database. Compact records are loaded once; full fields are fetched
// only for the selected antenna. The original file is never modified.
class AntennaDatabase {
public:
    ~AntennaDatabase();
    AntennaDatabase() = default;
    AntennaDatabase(const AntennaDatabase&) = delete;
    AntennaDatabase& operator=(const AntennaDatabase&) = delete;
    bool open(const std::filesystem::path& path);
    void close();
    const std::vector<Antenna>& antennas() const { return antennas_; }
    const std::vector<Mast>& masts() const { return masts_; }
    const std::string& error() const { return error_; }
    const std::filesystem::path& path() const { return path_; }
    size_t skippedRows() const { return skippedRows_; }
    std::vector<size_t> visibleMasts(double west, double east, double south,
                                   double north, unsigned providers) const;
    // k=0 returns all matches within radiusKm, including co-located records.
    std::vector<NearbyAntenna> nearest(double longitude, double latitude,
                                     unsigned providers, size_t k,
                                     double radiusKm = std::numeric_limits<double>::infinity()) const;
    std::vector<std::pair<std::string, std::string>> details(size_t antenna);
private:
    sqlite3* db_ = nullptr;
    std::filesystem::path path_;
    std::string error_;
    std::vector<Antenna> antennas_;
    std::vector<Mast> masts_;
    size_t skippedRows_ = 0;
};
} // namespace phreakshow
