#pragma once

#include "imgui.h"
#include <filesystem>
#include <memory>
#include <string>

namespace phreakshow {
// Both layers use the same Web Mercator projection and 256 px tile grid.
struct MapTile {
    unsigned int texture = 0;
    ImVec2 uv0{0, 0}, uv1{1, 1};
    bool satellite = false;
    bool detailed = false;
};

std::filesystem::path executableDirectory();

// Offline maps + detailed swisstopo map/SWISSIMAGE tiles. Network/decode work runs on
// one worker; texture creation/destruction stays on the OpenGL/UI thread.
class MapTiles {
public:
    MapTiles();
    ~MapTiles();
    MapTiles(const MapTiles&) = delete;
    MapTiles& operator=(const MapTiles&) = delete;
    void beginFrame();
    MapTile image(int zoom, int x, int y, bool satellite);
    bool hasOfflineMap() const;
    std::string onlineStatus(bool satellite) const;
    void retryOnline();
    void shutdown(); // Call before destroying the current OpenGL context.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace phreakshow
