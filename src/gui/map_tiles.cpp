#include "gui/map_tiles.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <GL/gl.h>
#include <zlib.h>
#include <curl/curl.h>
#include <cstdio>
#include <jpeglib.h>
#include <setjmp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace phreakshow {
namespace {
constexpr int tileSize = 256;
constexpr size_t maxEncodedBytes = 2 * 1024 * 1024;
using Bytes = std::vector<unsigned char>;
using Clock = std::chrono::steady_clock;

Bytes readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const auto length = file.tellg();
    if (length <= 0 || length > static_cast<std::streamoff>(maxEncodedBytes)) return {};
    Bytes bytes(static_cast<size_t>(length));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    return file ? bytes : Bytes{};
}

struct JpegError {
    jpeg_error_mgr base;
    jmp_buf jump;
};
void jpegFailure(j_common_ptr c) {
    auto* error = reinterpret_cast<JpegError*>(c->err);
    longjmp(error->jump, 1);
}
void jpegSilence(j_common_ptr) {}

Bytes decodeJpeg(const Bytes& bytes) {
    if (bytes.empty()) return {};
    // Allocate before setjmp, so libjpeg errors cannot bypass C++ destructors.
    Bytes pixels(tileSize * tileSize * 3);
    jpeg_decompress_struct decoder{};
    JpegError error{};
    decoder.err = jpeg_std_error(&error.base);
    error.base.error_exit = jpegFailure;
    error.base.output_message = jpegSilence;
    if (setjmp(error.jump)) {
        jpeg_destroy_decompress(&decoder);
        return {};
    }
    jpeg_create_decompress(&decoder);
    jpeg_mem_src(&decoder, bytes.data(), bytes.size());
    jpeg_read_header(&decoder, TRUE);
    if (decoder.image_width != tileSize || decoder.image_height != tileSize) {
        jpeg_destroy_decompress(&decoder);
        return {};
    }
    decoder.out_color_space = JCS_RGB;
    jpeg_start_decompress(&decoder);
    while (decoder.output_scanline < decoder.output_height) {
        JSAMPROW row = pixels.data() + decoder.output_scanline * tileSize * 3;
        jpeg_read_scanlines(&decoder, &row, 1);
    }
    jpeg_finish_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
    return pixels;
}

size_t receiveBytes(char* data, size_t size, size_t count, void* user) {
    auto& bytes = *static_cast<Bytes*>(user);
    const size_t length = size * count;
    if (length > maxEncodedBytes - bytes.size()) return 0;
    bytes.insert(bytes.end(), data, data + length);
    return length;
}
int cancelDownload(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<std::atomic<bool>*>(user)->load() ? 1 : 0;
}

GLuint upload(const Bytes& pixels) {
    GLint previous = 0, alignment = 4;
    GLuint texture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
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
    glBindTexture(GL_TEXTURE_2D, previous);
    return texture;
}
} // namespace

std::filesystem::path executableDirectory() {
#if defined(_WIN32)
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    return length && length < 32768 ? std::filesystem::path(path).parent_path() : std::filesystem::path{};
#else
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : exe.parent_path();
#endif
}

struct MapTiles::Impl {
    struct Texture {
        GLuint id = 0;
        int lastFrame = 0;
        Clock::time_point attempted{};
    };
    struct Download {
        std::string key;
        Bytes pixels;
        std::string error;
    };
    std::filesystem::path offlineRoot, satelliteRoot, mapRoot;
    std::map<std::string, Texture> textures;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::string> requests;
    std::set<std::string> pending;
    std::deque<Download> completed;
    std::thread worker;
    std::atomic<bool> stopping{false};
    std::array<std::string, 2> lastError;

    Impl() {
        const auto exeDir = executableDirectory();
        for (const auto& candidate : {exeDir / "assets/map_tiles", std::filesystem::path("assets/map_tiles"),
                                     std::filesystem::path(PHREAKSHOW_TILE_DIR)}) {
            if (std::filesystem::exists(candidate / "0/0/0.rgb.z")) {
                offlineRoot = candidate;
                break;
            }
        }
        satelliteRoot = exeDir / "assets/satellite_cache";
        mapRoot = exeDir / "assets/map_cache";
    }

    void run() {
        static const bool curlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
        CURL* curl = curlReady ? curl_easy_init() : nullptr;
        while (!stopping) {
            std::string key;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stopping || !requests.empty(); });
                if (stopping) break;
                key = requests.back();
                requests.pop_back();
            }
            Download result{key, {}, {}};
            const bool aerial = key.rfind("sat/", 0) == 0;
            const auto path = (aerial ? satelliteRoot : mapRoot) / (key.substr(4) + ".jpeg");
            Bytes encoded = readFile(path);
            result.pixels = decodeJpeg(encoded);
            if (result.pixels.empty() && curl) {
                encoded.clear();
                // Official XYZ endpoint: https://docs.geo.admin.ch/visualize-data/xyz.html
                const std::string layer = aerial ? "ch.swisstopo.swissimage" : "ch.swisstopo.pixelkarte-farbe";
                const std::string url = "https://wmts.geo.admin.ch/1.0.0/" + layer + "/default/current/3857/" + key.substr(4) + ".jpeg";
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "CellScope-phreakShow/1.0");
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receiveBytes);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &encoded);
                curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
                curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
                curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
                curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
                curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, cancelDownload);
                curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stopping);
                const CURLcode status = curl_easy_perform(curl);
                if (status == CURLE_OK) result.pixels = decodeJpeg(encoded);
                if (result.pixels.empty()) result.error = status == CURLE_OK ? "Invalid map tile" : curl_easy_strerror(status);
                else {
                    std::error_code ec;
                    std::filesystem::create_directories(path.parent_path(), ec);
                    if (!ec) {
                        const auto temporary = path.string() + ".tmp";
                        std::ofstream file(temporary, std::ios::binary);
                        file.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
                        const bool wrote = static_cast<bool>(file);
                        file.close();
                        if (wrote) std::filesystem::rename(temporary, path, ec);
                    }
                }
            } else if (result.pixels.empty()) result.error = "Could not initialize map download";
            {
                std::lock_guard<std::mutex> lock(mutex);
                // UI drains results each frame. Bound memory if it stops drawing.
                if (completed.size() >= 64) {
                    pending.erase(completed.front().key);
                    completed.pop_front();
                }
                completed.push_back(std::move(result));
            }
        }
        if (curl) curl_easy_cleanup(curl);
    }

    GLuint offline(int z, int x, int y) {
        const std::string key = std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y);
        const auto found = textures.find(key);
        if (found != textures.end()) {
            found->second.lastFrame = ImGui::GetFrameCount();
            return found->second.id;
        }
        GLuint id = 0;
        const Bytes encoded = readFile(offlineRoot / (key + ".rgb.z"));
        if (!offlineRoot.empty() && !encoded.empty()) {
            Bytes pixels(tileSize * tileSize * 3);
            uLongf length = pixels.size();
            if (uncompress(pixels.data(), &length, encoded.data(), encoded.size()) == Z_OK && length == pixels.size())
                id = upload(pixels);
        }
        textures.emplace(key, Texture{id, ImGui::GetFrameCount(), {}});
        return id;
    }

    GLuint online(int z, int x, int y, bool aerial) {
        const std::string key = std::string(aerial ? "sat/" : "map/") + std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y);
        const auto found = textures.find(key);
        if (found != textures.end()) {
            found->second.lastFrame = ImGui::GetFrameCount();
            if (found->second.id) return found->second.id;
            if (Clock::now() - found->second.attempted < std::chrono::seconds(30)) return 0;
            textures.erase(found);
        }
        std::lock_guard<std::mutex> lock(mutex);
        if (pending.size() < 64 && pending.insert(key).second) {
            requests.push_back(key);
            if (!worker.joinable()) worker = std::thread([this] { run(); });
            wake.notify_one();
        }
        return 0;
    }
};

MapTiles::MapTiles() : impl_(std::make_unique<Impl>()) {}
MapTiles::~MapTiles() { shutdown(); }

void MapTiles::beginFrame() {
    if (!impl_) return;
    std::deque<Impl::Download> results;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        results.swap(impl_->completed);
        for (const auto& result : results) impl_->pending.erase(result.key);
    }
    for (const auto& result : results) {
        auto& texture = impl_->textures[result.key];
        if (texture.id) glDeleteTextures(1, &texture.id);
        texture = {result.pixels.empty() ? 0 : upload(result.pixels), ImGui::GetFrameCount(), Clock::now()};
        impl_->lastError[result.key.rfind("sat/", 0) == 0 ? 1 : 0] = result.error;
    }
    // Retain textures referenced by the previous frame (including viewports).
    if (impl_->textures.size() <= 512) return;
    const int cutoff = ImGui::GetFrameCount() - 1;
    for (auto it = impl_->textures.begin(); it != impl_->textures.end() && impl_->textures.size() > 256;) {
        if (it->second.lastFrame < cutoff) {
            if (it->second.id) glDeleteTextures(1, &it->second.id);
            it = impl_->textures.erase(it);
        } else ++it;
    }
}

MapTile MapTiles::image(int zoom, int x, int y, bool satellite) {
    if (!impl_ || zoom < 0 || zoom > 20 || x < 0 || y < 0 || x >= (1 << zoom) || y >= (1 << zoom)) return {};
    if (zoom >= 13) {
        const GLuint id = impl_->online(zoom, x, y, satellite);
        if (id) return {id, {0, 0}, {1, 1}, satellite, true};
        // A cached parent can bridge the gap while exact-resolution tiles load.
        for (int sourceZoom = zoom - 1; sourceZoom >= 13; --sourceZoom) {
            const int factor = 1 << (zoom - sourceZoom);
            const std::string key = std::string(satellite ? "sat/" : "map/") +
                std::to_string(sourceZoom) + "/" + std::to_string(x / factor) + "/" + std::to_string(y / factor);
            const auto found = impl_->textures.find(key);
            if (found != impl_->textures.end() && found->second.id) {
                found->second.lastFrame = ImGui::GetFrameCount();
                const float u = static_cast<float>(x % factor) / factor, v = static_cast<float>(y % factor) / factor;
                return {found->second.id, {u, v}, {u + 1.0f / factor, v + 1.0f / factor}, satellite, false};
            }
        }
    }
    for (int sourceZoom = std::min(zoom, 12); sourceZoom >= 0; --sourceZoom) {
        const int factor = 1 << (zoom - sourceZoom);
        const GLuint id = impl_->offline(sourceZoom, x / factor, y / factor);
        if (id) {
            const float u = static_cast<float>(x % factor) / factor;
            const float v = static_cast<float>(y % factor) / factor;
            const float span = 1.0f / factor;
            return {id, {u, v}, {u + span, v + span}, false};
        }
    }
    return {};
}
bool MapTiles::hasOfflineMap() const { return impl_ && !impl_->offlineRoot.empty(); }
std::string MapTiles::onlineStatus(bool satellite) const {
    if (!impl_) return {};
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::string status;
    const std::string prefix = satellite ? "sat/" : "map/";
    const auto count = std::count_if(impl_->pending.begin(), impl_->pending.end(), [&](const auto& key) { return key.rfind(prefix, 0) == 0; });
    if (count) status = "Loading " + std::to_string(count) + (satellite ? " aerial tiles. " : " detailed map tiles. ");
    const auto& error = impl_->lastError[satellite ? 1 : 0];
    if (!error.empty()) status += "Tile unavailable: " + error + ". ";
    return status;
}
void MapTiles::retryOnline() {
    if (!impl_) return;
    for (auto it = impl_->textures.begin(); it != impl_->textures.end();) {
        if ((it->first.rfind("sat/", 0) == 0 || it->first.rfind("map/", 0) == 0) && !it->second.id) it = impl_->textures.erase(it);
        else ++it;
    }
    impl_->lastError = {};
}
void MapTiles::shutdown() {
    if (!impl_) return;
    impl_->stopping = true;
    impl_->wake.notify_one();
    if (impl_->worker.joinable()) impl_->worker.join();
    for (const auto& entry : impl_->textures)
        if (entry.second.id) glDeleteTextures(1, &entry.second.id);
    impl_.reset();
}
} // namespace phreakshow
