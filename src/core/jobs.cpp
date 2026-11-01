#include "core/jobs.h"

#include "core/profile.h"

#include <TaskScheduler.h>

namespace zaro::jobs {
namespace {

enki::TaskScheduler* g_scheduler = nullptr;
uint32_t g_threads = 1;

struct RangeTask : enki::ITaskSet {
    RangeFn fn = nullptr;
    void* ctx = nullptr;
    void ExecuteRange(enki::TaskSetPartition range, uint32_t) override {
        ZARO_ZONE("job");
        fn(range.start, range.end, ctx);
    }
};

}  // namespace

void init(uint32_t threads) {
    shutdown();
    if (threads == 0) threads = enki::GetNumHardwareThreads();
    g_threads = threads < 1 ? 1 : threads;
    if (g_threads > 1) {
        g_scheduler = new enki::TaskScheduler();
        g_scheduler->Initialize(g_threads);
    }
}

void shutdown() {
    if (g_scheduler) {
        g_scheduler->WaitforAllAndShutdown();
        delete g_scheduler;
        g_scheduler = nullptr;
    }
    g_threads = 1;
}

uint32_t thread_count() { return g_threads; }

void parallel_for(uint32_t count, uint32_t min_range, RangeFn fn, void* ctx) {
    if (count == 0) return;
    if (!g_scheduler || count <= min_range) {
        fn(0, count, ctx);
        return;
    }
    RangeTask task;
    task.fn = fn;
    task.ctx = ctx;
    task.m_SetSize = count;
    task.m_MinRange = min_range;
    g_scheduler->AddTaskSetToPipe(&task);
    g_scheduler->WaitforTask(&task);
}

}  // namespace zaro::jobs
