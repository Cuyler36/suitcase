// A worker thread that runs the callbacks of the stubbed asynchronous NAND calls, in request order.
// Each runs under the interrupt lock, as the Wii runs them in interrupt context: code that checks state
// a callback chain sets (RVLFaceLib's RFLGetAsyncStatus) cannot see it half done.
#include "interrupt_lock.h"
#include "nand_async.h"
#include "shutdown.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace cf::sdk {
namespace {

struct Request {
    NANDCallback callback;
    NANDCommandBlock* block;
    s32 result;
};

class Worker {
public:
    Worker() : mThread([this] { Run(); }) {}

    void Push(const Request& request) {
        {
            std::lock_guard lock{mMutex};
            mQueue.push_back(request);
        }
        mCond.notify_one();
    }

    // Ends the thread (shutdown.h); requests not yet run are dropped
    void Stop() {
        {
            std::lock_guard lock{mMutex};
            mStop = true;
        }
        mCond.notify_one();
        if (mThread.joinable()) {
            mThread.join();
        }
    }

private:
    void Run() {
        while (true) {
            Request request;
            {
                std::unique_lock lock{mMutex};
                mCond.wait(lock, [this] { return mStop || !mQueue.empty(); });
                if (mStop) {
                    return;
                }
                request = mQueue.front();
                mQueue.pop_front();
            }
            InterruptLockScope interrupts;
            request.callback(request.result, request.block);
        }
    }

    std::mutex mMutex;
    std::condition_variable mCond;
    std::deque<Request> mQueue;
    bool mStop = false;
    std::thread mThread; // last: started once the rest is constructed
};

Worker& GetWorker() {
    static Worker worker;
    return worker;
}

} // namespace

void ShutdownNandWorker() {
    GetWorker().Stop();
}

s32 NandCompleteAsync(NANDCallback callback, NANDCommandBlock* block, s32 result) {
    if (callback != nullptr) {
        GetWorker().Push({callback, block, result});
    }
    return NAND_RESULT_OK;
}

} // namespace cf::sdk
