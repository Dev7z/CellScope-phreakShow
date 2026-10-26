#include "core/app.h"
#include "core/main_funcs.h"
#include "util/log.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

// Retune the active (live) source to a new center and re-point the decoder
// manager there. Wipes all decoders -- callers restore what they need.
void retuneActive(App& app, double centerMHz)
{
    double hz = centerMHz * 1e6;
    app.centerFreqMHz = centerMHz;
    app.viewA.resetView = true;
    if (app.sourceMode == 0)
        app.sdr.setCenterFreq(hz);
    else if (app.sourceMode == 2)
        app.server.setCenterFreq(hz);
    else if (app.sourceMode == 3)
        app.hack.setCenterFreq(hz);
#ifdef HAS_AIRSPY
    else if (app.sourceMode == 5)
        app.airspy.setCenterFreq(hz);
#endif
#ifdef HAS_LIBRESDR
    else if (app.sourceMode == 6)
        app.libre.setCenterFreq(hz);
#endif
    app.decoders.removeAll();
    app.decoders.configure(app.active->sampleRate(), hz);
    // Rebuild the frequency axis now (processFft already ran this frame with the
    // old center) so drawSpectrum fits the view to the NEW band and the decoder
    // marker stays on screen after a big follow jump.
    if (app.viewA.curN > 0)
        updateFreqAxis(app.viewA, hz, app.active->sampleRate(), app.viewA.curN);
}

// Retune a live source to a new center while keeping the current decoders
// (re-added at their same absolute frequencies) and the user's spectrum view.
// Used for band browsing, where we sweep the radio as the view is panned.
void retunePreserving(App& app, double centerMHz)
{
    std::vector<std::pair<double, int>> keep;
    for (auto& s : app.decoders.status())
        keep.push_back({s.freqMHz, s.baud});

    double hz = centerMHz * 1e6;
    app.centerFreqMHz = centerMHz;
    if (app.sourceMode == 0 || app.sourceMode == 4)
        app.sdr.setCenterFreq(hz);
    else if (app.sourceMode == 2)
        app.server.setCenterFreq(hz);
    else if (app.sourceMode == 3)
        app.hack.setCenterFreq(hz);
#ifdef HAS_AIRSPY
    else if (app.sourceMode == 5)
        app.airspy.setCenterFreq(hz);
#endif
#ifdef HAS_LIBRESDR
    else if (app.sourceMode == 6)
        app.libre.setCenterFreq(hz);
#endif
    app.decoders.removeAll();
    app.decoders.configure(app.active->sampleRate(), hz);
    for (auto& k : keep)
        app.decoders.addDecoder(k.first * 1e6, k.second);
}

void setCenterFrequency(App& app, double centerMHz)
{
    if (!std::isfinite(centerMHz) || centerMHz <= 0) return;
#ifdef HAS_LTE
    const bool restartLte = app.lteRunning && app.active->running();
    if (restartLte) app.lteEngine.stop();
#endif
    app.centerFreqMHz = centerMHz;
    if (app.active->running()) {
        if (app.sourceMode == 1) app.wav.setCenterFreq(centerMHz * 1e6);
        else retunePreserving(app, centerMHz);
    }
    const double fs = app.active->sampleRate();
    auto& view = app.viewA;
    if (view.curN > 0) {
        updateFreqAxis(view, centerMHz * 1e6, fs, view.curN);
        view.viewXminMHz = view.freqMHz.front();
        view.viewXmaxMHz = view.freqMHz.back();
        std::fill(view.avg.begin(), view.avg.end(), app.dbMin);
        std::fill(view.inst.begin(), view.inst.end(), app.dbMin);
    }
    view.ring.clear();
    view.waterfall.clear();
    view.resetView = true;
    view.fftSkip = false;
#ifdef HAS_LTE
    if (restartLte) {
        app.lteWatched.clear();
        app.lteUeFilter[0] = '\0';
        app.lteUeCallsOnly = false;
        cellscope::lte::EngineConfig cfg;
        cfg.input_sample_rate_hz = fs;
        cfg.center_freq_mhz = centerMHz;
        app.lteEngine.start(cfg);
    }
#endif
}
