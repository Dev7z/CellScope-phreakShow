#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace phreakshow {
class BackgroundMusic {
public:
    BackgroundMusic();
    ~BackgroundMusic();
    BackgroundMusic(const BackgroundMusic&) = delete;
    BackgroundMusic& operator=(const BackgroundMusic&) = delete;
    bool setEnabled(bool enabled, const std::filesystem::path& path = {});
    bool enabled() const;
    const std::string& error() const { return error_; }
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string error_;
};
} // namespace phreakshow
