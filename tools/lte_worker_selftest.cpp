// Hold a real decode consumer in flight while the pipeline is being stopped.
#include <array>
#include "Phy.h"
#include "SubframeInfoConsumer.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>

using namespace std::chrono_literals;

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct BlockingConsumer : SubframeInfoConsumer {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool released = false;
    std::atomic<bool> finished{false};

    void consumeDCICollection(const SubframeInfo&) override
    {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return released; });
        finished.store(true);
    }

    bool waitForWorker()
    {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, 5s, [&] { return entered; });
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        cv.notify_all();
    }

    ~BlockingConsumer() override { release(); }
};

int main()
{
    try {
        std::atomic<float> cfo{0};
        HARQ harq;
        harq.init_HARQ(0);
        UL_HARQ ulHarq;
        ULSchedule schedule(0, &ulHarq, false);
        MCSTracking tracking(1, 0, false, DL_MODE, 3, cfo);
        LTESniffer_pcap_writer captures;
        Phy phy(1, 1, "", "", false, 0.99, 5, &captures, &tracking, &harq, 1, 0, &schedule);
        auto consumer = std::make_shared<BlockingConsumer>();
        phy.getCommon().setDCIConsumer(consumer);
        srsran_cell_t cell{};
        cell.id = 1;
        cell.nof_prb = 6;
        cell.nof_ports = 1;
        cell.cp = SRSRAN_CP_NORM;
        cell.phich_resources = SRSRAN_PHICH_R_1;
        require(phy.setCell(cell), "could not configure worker cell");
        auto worker = phy.getAvail();
        std::memset(worker->getBuffers()[0], 0, 3 * SRSRAN_SF_LEN_PRB(100) * sizeof(cf_t));
        srsran_dl_sf_cfg_t sf{};
        sf.tti = 1;
        worker->prepare(1, 0, false, sf);
        phy.putPending(std::move(worker));
        if (!consumer->waitForWorker()) {
            consumer->release();
            throw std::runtime_error("decode worker did not reach consumer");
        }

        auto joined = std::async(std::launch::async, [&] { phy.joinPending(); });
        const bool waited = joined.wait_for(100ms) == std::future_status::timeout;
        consumer->release();
        joined.get();
        require(waited, "joinPending returned while a decode worker was still executing");
        require(consumer->finished.load(), "joinPending did not finish all callbacks");
        phy.joinPending(); // repeated joins and the Phy destructor must be safe

        ThreadSafeQueue<int> queue;
        queue.enqueue(std::make_shared<int>(1));
        auto empty = std::async(std::launch::async, [&] { return queue.waitEmpty(); });
        require(empty.wait_for(20ms) == std::future_status::timeout, "nonempty queue did not wait");
        queue.cancel();
        const bool woke = empty.wait_for(1s) == std::future_status::ready;
        if (!woke) queue.dequeueImmediate(); // unblock even when the regression fails
        require(!empty.get() && woke, "cancel did not wake a nonempty queue waiter");
        std::cout << "LTE worker shutdown waits for in-flight decoding and callbacks\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
