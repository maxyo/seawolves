#include "ocean_worker.hpp"

namespace storm::sea::ocean
{
// The worker's own thread joins its helpers in the cascades' loop: two helpers make three threads for three cascades.
Worker::Worker() : helpers_(std::min(ParallelFor::defaultHelpers(), 2u)), thread_([this] { Loop(); })
{
}

Worker::~Worker()
{
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void Worker::Start(std::function<void()> job)
{
    {
        std::lock_guard lock(mutex_);
        job_ = std::move(job);
        busy_ = true;
    }
    wake_.notify_one();
}

void Worker::Wait()
{
    std::unique_lock lock(mutex_);
    done_.wait(lock, [this] { return !busy_; });
}

bool Worker::Busy()
{
    std::lock_guard lock(mutex_);
    return busy_;
}

void Worker::Loop()
{
    std::unique_lock lock(mutex_);
    for (;;)
    {
        wake_.wait(lock, [this] { return stop_ || job_; });
        if (stop_)
            return;
        std::function<void()> job = std::move(job_);
        job_ = nullptr;
        lock.unlock();
        job();
        lock.lock();
        busy_ = false;
        done_.notify_all();
    }
}
} // namespace storm::sea::ocean
