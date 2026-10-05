#include "gui/phreakshow.h"
#include "gui/map_projection.h"
#include "imgui.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <GL/gl.h>
#include <zlib.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace {
constexpr int tileSize = 256;
constexpr int maxZoom = 16;
constexpr int swissTileZoom = 12;
struct CachedTile {
    GLuint texture = 0;
    int lastFrame = 0;
};
struct TileImage {
    GLuint texture = 0;
    ImVec2 uv0{0, 0};
    ImVec2 uv1{1, 1};
    int sourceZoom = 0;
};
struct MapState {
    double x = 0.5 + 8.13 / 360.0;
    double y = phreakshow::mercatorY(46.8);
    int zoom = 8;
    std::map<std::string, CachedTile> textures;
    std::filesystem::path root;
    bool located = false;

    void locate() {
        if (located) return;
        located = true;
        // Executable-relative resources allow launching from an arbitrary cwd.
        std::error_code ec;
        const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
#if defined(_WIN32)
        wchar_t path[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
        const auto exeDir = length && length < 32768
            ? std::filesystem::path(path).parent_path() : std::filesystem::path{};
#else
        const auto exeDir = ec ? std::filesystem::path{} : exe.parent_path();
#endif
        for (const auto& candidate : {exeDir / "assets/map_tiles",
                                     std::filesystem::path("assets/map_tiles"),
                                     std::filesystem::path(PHREAKSHOW_TILE_DIR)}) {
            if (std::filesystem::exists(candidate / "0/0/0.rgb.z")) {
                root = candidate;
                return;
            }
        }
    }

    GLuint tile(int z, int tx, int ty) {
        const std::string key = std::to_string(z) + "/" + std::to_string(tx) + "/" + std::to_string(ty) + ".rgb.z";
        const auto found = textures.find(key);
        if (found != textures.end()) {
            found->second.lastFrame = ImGui::GetFrameCount();
            return found->second.texture;
        }
        GLuint texture = 0;
        std::ifstream file(root / key, std::ios::binary | std::ios::ate);
        if (file) {
            const auto size = file.tellg();
            // Bound reads and decoded size even if a local tile is corrupted.
            if (size > 0 && size <= 1024 * 1024) {
                std::vector<unsigned char> compressed(static_cast<size_t>(size));
                std::vector<unsigned char> pixels(tileSize * tileSize * 3);
                file.seekg(0);
                file.read(reinterpret_cast<char*>(compressed.data()), compressed.size());
                uLongf decoded = pixels.size();
                if (file && uncompress(pixels.data(), &decoded, compressed.data(), compressed.size()) == Z_OK && decoded == pixels.size()) {
                    GLint previousTexture = 0, alignment = 4;
                    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
                    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
                    glGenTextures(1, &texture);
                    glBindTexture(GL_TEXTURE_2D, texture);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tileSize, tileSize, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
                    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
                    glBindTexture(GL_TEXTURE_2D, previousTexture);
                }
            }
        }
        textures.emplace(key, CachedTile{texture, ImGui::GetFrameCount()});
        return texture;
    }

    TileImage image(int z, int tx, int ty) {
        // Missing regional tiles reuse the appropriate portion of their parent.
        // This also allows zooming beyond the native Swiss raster resolution.
        for (int sourceZoom = std::min(z, swissTileZoom); sourceZoom >= 0; --sourceZoom) {
            const int factor = 1 << (z - sourceZoom);
            const GLuint texture = tile(sourceZoom, tx / factor, ty / factor);
            if (texture) {
                const float u = static_cast<float>(tx % factor) / factor;
                const float v = static_cast<float>(ty % factor) / factor;
                const float span = 1.0f / factor;
                return {texture, ImVec2(u, v), ImVec2(u + span, v + span), sourceZoom};
            }
        }
        return {};
    }

    void trimCache() {
        // Never delete a texture referenced by the previous frame's draw data.
        // Keep GPU memory bounded as the user pans across the regional tiles.
        if (textures.size() <= 512) return;
        const int cutoff = ImGui::GetFrameCount() - 1;
        for (auto it = textures.begin(); it != textures.end() && textures.size() > 256;) {
            if (it->second.lastFrame < cutoff) {
                if (it->second.texture) glDeleteTextures(1, &it->second.texture);
                it = textures.erase(it);
            } else ++it;
        }
    }
};
MapState map;
}

void shutdownPhreakShow() {
    for (const auto& entry : map.textures)
        if (entry.second.texture) glDeleteTextures(1, &entry.second.texture);
    map.textures.clear();
}

void drawPhreakShow() {
    map.locate();
    map.trimCache();
    ImGui::TextUnformatted("Offline map  |  Drag to pan, scroll to zoom");
    auto zoomAt = [](int next, double offsetX, double offsetY) {
        next = std::clamp(next, 0, maxZoom);
        const double before = tileSize * (1 << map.zoom);
        const double after = tileSize * (1 << next);
        map.x += offsetX / before - offsetX / after;
        map.y += offsetY / before - offsetY / after;
        map.zoom = next;
    };
    if (ImGui::Button("+")) zoomAt(map.zoom + 1, 0, 0);
    ImGui::SameLine();
    if (ImGui::Button("-")) zoomAt(map.zoom - 1, 0, 0);
    ImGui::SameLine();
    if (ImGui::Button("Reset view")) {
        map.x = 0.5 + 10.0 / 360.0;
        map.y = phreakshow::mercatorY(35.0);
        map.zoom = 2;
    }
    ImGui::SameLine();
    if (ImGui::Button("Switzerland")) {
        map.x = 0.5 + 8.13 / 360.0;
        map.y = phreakshow::mercatorY(46.8);
        map.zoom = 8;
    }
    ImGui::SameLine();
    ImGui::Text("Zoom %d / %d", map.zoom, maxZoom);
    if (map.root.empty()) {
        ImGui::TextWrapped("Local map tiles are missing. Place assets/map_tiles beside the executable.");
        return;
    }
    // Reserve room for the coordinate label and attribution below the map.
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.y -= 2 * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    if (size.x < 1 || size.y < 1) return;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##offline_map", size, ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    const auto& io = ImGui::GetIO();
    const double offsetX = io.MousePos.x - origin.x - size.x * 0.5;
    const double offsetY = io.MousePos.y - origin.y - size.y * 0.5;
    if (hovered && io.MouseWheel != 0)
        zoomAt(map.zoom + (io.MouseWheel > 0 ? 1 : -1), offsetX, offsetY);
    const int count = 1 << map.zoom;
    const double world = tileSize * count;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
        map.x -= io.MouseDelta.x / world;
        map.y -= io.MouseDelta.y / world;
    }
    map.x = phreakshow::wrapX(map.x);
    const double halfHeight = std::min(0.5, size.y / (2.0 * world));
    map.y = std::clamp(map.y, halfHeight, 1.0 - halfHeight);
    const double left = map.x * world - size.x * 0.5;
    const double top = map.y * world - size.y * 0.5;
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    draw->PushClipRect(origin, end, true);
    draw->AddRectFilled(origin, end, IM_COL32(17, 27, 37, 255));
    for (int ty = std::max(0, static_cast<int>(std::floor(top / tileSize))); ty < count && ty * tileSize < top + size.y; ++ty) {
        for (int tx = static_cast<int>(std::floor(left / tileSize)); tx * tileSize < left + size.x; ++tx) {
            const int wrappedX = (tx % count + count) % count;
            const TileImage tile = map.image(map.zoom, wrappedX, ty);
            const ImVec2 a(origin.x + static_cast<float>(tx * tileSize - left), origin.y + static_cast<float>(ty * tileSize - top));
            const ImVec2 b(a.x + tileSize, a.y + tileSize);
            if (tile.texture) draw->AddImage(static_cast<ImTextureID>(tile.texture), a, b, tile.uv0, tile.uv1);
            else {
                draw->AddRect(a, b, IM_COL32(90, 105, 120, 255));
                draw->AddText(ImVec2(a.x + 10, a.y + 10), IM_COL32(220, 220, 220, 255), "Tile unavailable");
            }
        }
    }
    const double pointerY = (top + io.MousePos.y - origin.y) / world;
    const bool onMap = hovered && pointerY >= 0.0 && pointerY <= 1.0;
    if (onMap) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        draw->AddLine(ImVec2(io.MousePos.x - 8, io.MousePos.y), ImVec2(io.MousePos.x + 8, io.MousePos.y), IM_COL32(255, 210, 90, 255));
        draw->AddLine(ImVec2(io.MousePos.x, io.MousePos.y - 8), ImVec2(io.MousePos.x, io.MousePos.y + 8), IM_COL32(255, 210, 90, 255));
    }
    draw->PopClipRect();
    draw->AddRect(origin, end, IM_COL32(80, 100, 120, 255));
    if (onMap) {
        const double pointerX = (left + io.MousePos.x - origin.x) / world;
        ImGui::Text("Latitude: %.5f deg   Longitude: %.5f deg", phreakshow::latitude(pointerY), phreakshow::longitude(pointerX));
    } else ImGui::TextUnformatted("Latitude: --   Longitude: --  (point at the map)");
    ImGui::TextDisabled("Swiss map: \xC2\xA9 swisstopo, SMR200 2025 | World: Natural Earth | Offline | Swiss detail to zoom 12");
}
