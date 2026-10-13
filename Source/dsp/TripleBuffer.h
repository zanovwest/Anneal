#pragma once

#include <atomic>

namespace anneal
{
    // Wait-free single-producer / single-consumer triple buffer.
    // The audio thread writes whole frames; the UI thread always sees the most recent
    // complete one and never blocks the writer.
    template <typename T>
    class TripleBuffer
    {
    public:
        T& beginWrite() noexcept { return buffers[back]; }

        void endWrite() noexcept
        {
            back = state.exchange (back | freshBit, std::memory_order_acq_rel) & indexMask;
        }

        // Returns the newest complete frame, or nullptr if nothing was published since the last read.
        const T* read() noexcept
        {
            if ((state.load (std::memory_order_acquire) & freshBit) == 0)
                return nullptr;

            front = state.exchange (front, std::memory_order_acq_rel) & indexMask;
            return &buffers[front];
        }

    private:
        static constexpr int freshBit = 4, indexMask = 3;
        T buffers[3] {};
        std::atomic<int> state { 1 };
        int back = 0, front = 2;

        static_assert (std::atomic<int>::is_always_lock_free);
    };
}
