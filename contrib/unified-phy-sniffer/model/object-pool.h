/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Unified PHY Sniffer
 *
 * Lock-free object pool for allocation-free packet processing
 */

#ifndef OBJECT_POOL_H
#define OBJECT_POOL_H

#include <array>
#include <atomic>
#include <cstdint>
#include <new>

namespace ns3
{

/**
 * \brief Lock-free object pool for allocation-free context management
 *
 * This pool pre-allocates objects and provides O(1) acquire/release
 * operations without heap allocation. Uses a bitmap for tracking free slots.
 *
 * \tparam T The type of objects to pool
 * \tparam PoolSize Number of pre-allocated objects (default 64)
 */
template <typename T, size_t PoolSize = 64>
class ObjectPool
{
  public:
    ObjectPool()
        : m_freeMask(~0ULL)
    {
        static_assert(PoolSize <= 64, "Pool size must be <= 64 for bitmap");
        // Initialize all objects in pool
        for (size_t i = 0; i < PoolSize; ++i)
        {
            new (&m_pool[i]) T();
        }
    }

    ~ObjectPool()
    {
        for (size_t i = 0; i < PoolSize; ++i)
        {
            m_pool[i].~T();
        }
    }

    /**
     * \brief Acquire an object from the pool
     * \return Pointer to acquired object, or newly heap-allocated if pool exhausted
     */
    T* Acquire()
    {
        uint64_t mask = m_freeMask.load(std::memory_order_relaxed);
        while (mask != 0)
        {
            // Find first set bit (first free slot)
            int idx = __builtin_ctzll(mask);
            uint64_t newMask = mask & ~(1ULL << idx);

            if (m_freeMask.compare_exchange_weak(mask,
                                                  newMask,
                                                  std::memory_order_acquire,
                                                  std::memory_order_relaxed))
            {
                T* obj = &m_pool[idx];
                obj->Reset(); // Reset object state
                return obj;
            }
            // CAS failed, mask was updated by another thread, retry
        }

        // Pool exhausted, fall back to heap allocation
        return new T();
    }

    /**
     * \brief Release an object back to the pool
     * \param obj Pointer to object to release
     */
    void Release(T* obj)
    {
        // Check if object belongs to our pool
        std::ptrdiff_t idx = obj - &m_pool[0];
        if (idx >= 0 && static_cast<size_t>(idx) < PoolSize)
        {
            // Return to pool
            m_freeMask.fetch_or(1ULL << idx, std::memory_order_release);
        }
        else
        {
            // Was heap-allocated, delete it
            delete obj;
        }
    }

    /**
     * \brief Get number of available objects in pool
     * \return Count of free slots
     */
    size_t Available() const
    {
        return __builtin_popcountll(m_freeMask.load(std::memory_order_relaxed));
    }

  private:
    alignas(64) std::array<T, PoolSize> m_pool; //!< Pre-allocated object storage
    std::atomic<uint64_t> m_freeMask;           //!< Bitmap: 1 = free, 0 = in use
};

} // namespace ns3

#endif /* OBJECT_POOL_H */
