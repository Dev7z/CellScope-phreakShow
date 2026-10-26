#include "gui/antenna_database.h"
#include "gui/map_projection.h"
#include <sqlite3.h>
#include <algorithm>
#include <cmath>

namespace phreakshow {
unsigned providerBit(MapProvider p) { return 1u << static_cast<unsigned>(p); }
const char* providerName(MapProvider p) {
    switch (p) {
    case MapProvider::Sunrise: return "Sunrise";
    case MapProvider::Swisscom: return "Swisscom";
    case MapProvider::Salt: return "Salt";
    default: return "Other";
    }
}

double distanceKm(double lon1, double lat1, double lon2, double lat2) {
    const double a = lat1 * pi / 180, b = lat2 * pi / 180;
    const double dlat = (lat2 - lat1) * pi / 180, dlon = (lon2 - lon1) * pi / 180;
    const double h = std::pow(std::sin(dlat / 2), 2) +
                     std::cos(a) * std::cos(b) * std::pow(std::sin(dlon / 2), 2);
    return earthRadiusKm * 2 * std::asin(std::sqrt(std::clamp(h, 0.0, 1.0)));
}

std::pair<double, double> antennaDirection(double azimuth, double length) {
    const double angle = azimuth * pi / 180;
    return {std::sin(angle) * length, -std::cos(angle) * length};
}

AntennaDatabase::~AntennaDatabase() { close(); }
void AntennaDatabase::close() {
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
    antennas_.clear();
    masts_.clear();
    skippedRows_ = 0;
    path_.clear();
}

bool AntennaDatabase::open(const std::filesystem::path& path) {
    close();
    error_.clear();
    path_ = path;
    if (sqlite3_open_v2(path.u8string().c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        error_ = db_ ? sqlite3_errmsg(db_) : "Could not open database";
        close();
        return false;
    }
    sqlite3_busy_timeout(db_, 1000);
    sqlite3_stmt* statement = nullptr;
    const char* sql = R"SQL(SELECT "PK", longitude, latitude, "property.operator",
        "property.frequency_mhz", "property.azimuth_deg", "property.pci", "property.site_id",
        "property.installation_type"
        FROM switzerland ORDER BY longitude, latitude, "PK")SQL";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error_ = sqlite3_errmsg(db_);
        close();
        return false;
    }
    int result;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        const double lon = sqlite3_column_double(statement, 1);
        const double lat = sqlite3_column_double(statement, 2);
        if (sqlite3_column_type(statement, 1) == SQLITE_NULL ||
            sqlite3_column_type(statement, 2) == SQLITE_NULL ||
            !validMapCoordinates(lon, lat)) {
            ++skippedRows_;
            continue;
        }
        if (masts_.empty() || masts_.back().longitude != lon || masts_.back().latitude != lat)
            masts_.push_back({lon, lat, 0, {}});
        const auto* value = sqlite3_column_text(statement, 3);
        const std::string name = value ? reinterpret_cast<const char*>(value) : "";
        MapProvider provider = MapProvider::Unspecified;
        for (auto p : {MapProvider::Sunrise, MapProvider::Swisscom, MapProvider::Salt})
            if (name == providerName(p)) provider = p;
        Antenna antenna;
        antenna.id = sqlite3_column_int64(statement, 0);
        antenna.mast = masts_.size() - 1;
        antenna.provider = provider;
        antenna.frequencyMHz = sqlite3_column_double(statement, 4);
        if (!std::isfinite(antenna.frequencyMHz) || antenna.frequencyMHz < 0) antenna.frequencyMHz = 0;
        if (sqlite3_column_type(statement, 5) != SQLITE_NULL) {
            const double az = sqlite3_column_double(statement, 5);
            if (std::isfinite(az)) antenna.azimuth = std::fmod(std::fmod(az, 360) + 360, 360);
        }
        if (sqlite3_column_type(statement, 6) != SQLITE_NULL) antenna.pci = sqlite3_column_int64(statement, 6);
        if (sqlite3_column_type(statement, 7) != SQLITE_NULL) antenna.siteId = sqlite3_column_int64(statement, 7);
        const auto* installation = sqlite3_column_text(statement, 8);
        antenna.indoor = installation && sqlite3_stricmp(reinterpret_cast<const char*>(installation), "Indoor") == 0;
        masts_.back().providers |= providerBit(provider);
        masts_.back().antennas.push_back(antennas_.size());
        antennas_.push_back(antenna);
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) {
        error_ = sqlite3_errmsg(db_);
        close();
        return false;
    }
    return true;
}

std::vector<size_t> AntennaDatabase::visibleMasts(double west, double east, double south,
                                                double north, unsigned providers) const {
    std::vector<size_t> visible;
    // Bounds are expressed in degrees; west > east denotes a date-line crossing.
    const bool wholeWorld = east - west >= 360;
    for (size_t i = 0; i < masts_.size(); ++i) {
        const auto& m = masts_[i];
        const bool inLongitude = wholeWorld || (west <= east
            ? m.longitude >= west && m.longitude <= east
            : m.longitude >= west || m.longitude <= east);
        if ((m.providers & providers) && inLongitude && m.latitude >= south && m.latitude <= north)
            visible.push_back(i);
    }
    return visible;
}

std::vector<NearbyAntenna> AntennaDatabase::nearest(double longitude, double latitude,
                                                  unsigned providers, size_t k, double radiusKm,
                                                  bool countCoordinatesOnce, bool includeIndoor) const {
    std::vector<NearbyAntenna> rows;
    if (!validMapCoordinates(longitude, latitude) || std::isnan(radiusKm) || radiusKm < 0) return rows;
    const bool grouped = countCoordinatesOnce && k != 0;
    for (const auto& mast : masts_) {
        if (!(mast.providers & providers)) continue;
        const double distance = distanceKm(longitude, latitude, mast.longitude, mast.latitude);
        if (distance > radiusKm) continue;
        for (size_t index : mast.antennas) {
            if (!(providerBit(antennas_[index].provider) & providers) ||
                (!includeIndoor && antennas_[index].indoor)) continue;
            rows.push_back({index, distance});
            // The first matching PK represents this coordinate for ranking.
            if (grouped) break;
        }
    }
    auto less = [&](const auto& a, const auto& b) {
        if (a.distanceKm != b.distanceKm) return a.distanceKm < b.distanceKm;
        return antennas_[a.antenna].id < antennas_[b.antenna].id;
    };
    if (k && rows.size() > k) {
        std::partial_sort(rows.begin(), rows.begin() + k, rows.end(), less);
        rows.resize(k);
    } else std::sort(rows.begin(), rows.end(), less);
    if (grouped) {
        std::vector<NearbyAntenna> expanded;
        for (const auto& row : rows)
            for (size_t index : masts_[antennas_[row.antenna].mast].antennas)
                if ((providerBit(antennas_[index].provider) & providers) &&
                    (includeIndoor || !antennas_[index].indoor))
                    expanded.push_back({index, row.distanceKm});
        return expanded;
    }
    return rows;
}

std::vector<std::pair<std::string, std::string>> AntennaDatabase::details(size_t antenna) {
    std::vector<std::pair<std::string, std::string>> fields;
    if (!db_ || antenna >= antennas_.size()) return fields;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT * FROM switzerland WHERE \"PK\" = ?", -1, &statement, nullptr) != SQLITE_OK) {
        error_ = sqlite3_errmsg(db_);
        return fields;
    }
    sqlite3_bind_int64(statement, 1, antennas_[antenna].id);
    const int result = sqlite3_step(statement);
    if (result == SQLITE_ROW) {
        for (int i = 0; i < sqlite3_column_count(statement); ++i) {
            const auto* value = sqlite3_column_text(statement, i);
            fields.emplace_back(sqlite3_column_name(statement, i),
                                value ? reinterpret_cast<const char*>(value) : "(not provided)");
        }
    } else if (result != SQLITE_DONE) error_ = sqlite3_errmsg(db_);
    sqlite3_finalize(statement);
    return fields;
}
} // namespace phreakshow
