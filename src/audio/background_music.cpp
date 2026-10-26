#include "audio/background_music.h"
#include "miniaudio.h"
#include <cstdio>

namespace phreakshow {
struct BackgroundMusic::Impl {
    ma_engine engine{};
    ma_sound sound{};
    bool engineReady = false;
    bool soundReady = false;
    ~Impl() {
        if (soundReady) ma_sound_uninit(&sound);
        if (engineReady) ma_engine_uninit(&engine);
    }
};

BackgroundMusic::BackgroundMusic() = default;
BackgroundMusic::~BackgroundMusic() = default;
bool BackgroundMusic::enabled() const { return impl_ != nullptr; }

bool BackgroundMusic::setEnabled(bool enabled, const std::filesystem::path& path) {
    error_.clear();
    if (!enabled) {
        impl_.reset();
        return true;
    }
    if (impl_) return true;
    const auto fail = [&](const char* operation, ma_result result) {
        error_ = std::string(operation) + ": " + ma_result_description(result);
        std::fprintf(stderr, "BG music: %s\n", error_.c_str());
        return false;
    };
    if (path.empty()) return fail("Music file not found", MA_DOES_NOT_EXIST);

    auto music = std::make_unique<Impl>();
    auto config = ma_engine_config_init();
    config.noAutoStart = MA_TRUE;
    auto result = ma_engine_init(&config, &music->engine);
    if (result != MA_SUCCESS) return fail("Cannot open audio output", result);
    music->engineReady = true;
    const ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_LOOPING | MA_SOUND_FLAG_NO_SPATIALIZATION;
#ifdef _WIN32
    result = ma_sound_init_from_file_w(&music->engine, path.c_str(), flags, nullptr, nullptr, &music->sound);
#else
    result = ma_sound_init_from_file(&music->engine, path.c_str(), flags, nullptr, nullptr, &music->sound);
#endif
    if (result != MA_SUCCESS) return fail("Cannot load music", result);
    music->soundReady = true;
    result = ma_sound_start(&music->sound);
    if (result != MA_SUCCESS) return fail("Cannot start music", result);
    result = ma_engine_start(&music->engine);
    if (result != MA_SUCCESS) return fail("Cannot start audio output", result);
    impl_ = std::move(music);
    return true;
}
} // namespace phreakshow
