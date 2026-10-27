// Feed narrow-rate IQ through the engine's real resampling/buffering path.
#include "lte/lte_engine.h"

#include <complex>
#include <iostream>
#include <stdexcept>
#include <vector>

int main()
{
    try {
        cellscope::lte::LteEngine engine;
        const std::vector<std::complex<float>> iq(4096, {0, 0});
        // These need 8x and 16x expansion for cell search. A fixed 2x output
        // allocation corrupts the heap before any cell can be acquired.
        for (double inputRate : {240000.0, 120000.0, 1920000.0}) {
            cellscope::lte::EngineConfig cfg;
            cfg.input_sample_rate_hz = inputRate;
            engine.start(cfg);
            engine.feed(iq.data(), iq.size());
            engine.stop();
            if (engine.running() || engine.state() != cellscope::lte::EngineState::Idle)
                throw std::runtime_error("engine did not stop cleanly after resampling");
        }
        std::cout << "LTE engine safely buffers 8x/16x resampling and restarts\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
