// Exercise capture persistence without an SDR or LTE reception.
#include <array>
#include "PcapWriter.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Fixture {
    fs::path directory = fs::temp_directory_path() /
        ("cellscope-pcap-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() { fs::create_directory(directory); }
    ~Fixture() { std::error_code ec; fs::remove_all(directory, ec); }
};

static void checkCapture(const fs::path& path, unsigned expectedPackets,
                         const std::vector<uint8_t>& payload)
{
    std::ifstream input(path, std::ios::binary);
    pcap_hdr_t header{};
    require(bool(input.read(reinterpret_cast<char*>(&header), sizeof(header))), "missing PCAP header");
    require(header.magic_number == 0xa1b2c3d4 && header.version_major == 2 &&
            header.version_minor == 4 && header.network == MAC_LTE_DLT, "invalid MAC-LTE capture header");
    unsigned packets = 0;
    while (input.peek() != std::char_traits<char>::eof()) {
        pcaprec_hdr_t record{};
        require(bool(input.read(reinterpret_cast<char*>(&record), sizeof(record))), "incomplete packet header");
        require(record.incl_len == record.orig_len && record.incl_len > payload.size() &&
                record.incl_len <= header.snaplen, "invalid packet length");
        std::vector<uint8_t> data(record.incl_len);
        require(bool(input.read(reinterpret_cast<char*>(data.data()), data.size())), "incomplete packet data");
        require(std::equal(payload.rbegin(), payload.rend(), data.rbegin()), "packet payload changed");
        ++packets;
    }
    require(packets == expectedPackets, "wrong saved packet count");
}

int main()
{
    try {
        Fixture fixture;
        const auto dl = fixture.directory / "dl.pcap";
        const auto api = fixture.directory / "api.pcap";
        std::vector<uint8_t> payload{0x01, 0x23, 0x45, 0x67};
        LTESniffer_pcap_writer writer;
        writer.close();
        writer.open(dl.string(), api.string());
        checkCapture(dl, 0, payload);
        checkCapture(api, 0, payload);

        writer.write_dl_crnti(payload.data(), payload.size(), 0x1234, true, 123, false);
        writer.write_dl_crnti_api(payload.data(), payload.size(), 0x1234, true, 123, false);
        auto counts = writer.flush();
        require(counts.dl_packets == 1 && counts.api_packets == 1 && !counts.io_error,
                "capture counters do not match writes");
        // Both files must be readable while the writer is still open.
        checkCapture(dl, 1, payload);
        checkCapture(api, 1, payload);

        writer.enable(false);
        writer.write_dl_crnti(payload.data(), payload.size(), 0x1234, true, 124, false);
        writer.write_dl_crnti_api(payload.data(), payload.size(), 0x1234, true, 124, false);
        counts = writer.flush();
        require(counts.dl_packets == 1 && counts.api_packets == 1, "disabled writer saved packets");
        writer.enable(true);
        writer.write_dl_crnti_api(payload.data(), payload.size(), 0x1234, true, 125, false);
        writer.close();
        writer.close();
        checkCapture(api, 2, payload); // close() must flush the API file too.

        writer.open(dl.string(), api.string());
        counts = writer.flush();
        require(counts.dl_packets == 0 && counts.api_packets == 0, "new capture retained old counters");
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&] {
                for (int n = 0; n < 16; ++n) {
                    writer.write_dl_crnti(payload.data(), payload.size(), 0x1234, true, n, false);
                    writer.write_dl_crnti_api(payload.data(), payload.size(), 0x1234, true, n, false);
                    writer.flush();
                }
            });
        }
        for (auto& thread : threads) thread.join();
        writer.close();
        checkCapture(dl, 64, payload);
        checkCapture(api, 64, payload);

        {
            LTESniffer_pcap_writer scoped;
            scoped.open(dl.string(), api.string());
            scoped.write_dl_crnti_api(payload.data(), payload.size(), 0x1234, true, 126, false);
        }
        checkCapture(api, 1, payload); // Scope exit also closes both files.

        writer.open(dl.string(), (fixture.directory / "missing" / "api.pcap").string());
        require(writer.flush().io_error, "capture open failure was not reported");
        writer.write_dl_crnti(payload.data(), payload.size(), 0x1234, true, 127, false);
        writer.write_dl_crnti_api(payload.data(), payload.size(), 0x1234, true, 127, false);
        writer.close();
        checkCapture(dl, 1, payload);

        std::cout << "LTE PCAP persistence checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
