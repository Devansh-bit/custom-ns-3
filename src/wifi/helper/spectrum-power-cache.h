/*
 * Copyright (c) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef SPECTRUM_POWER_CACHE_H
#define SPECTRUM_POWER_CACHE_H

#include "ns3/nstime.h"
#include "ns3/spectrum-value.h"
#include "ns3/wifi-units.h"

#include <list>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ns3
{

/**
 * Cache for GetBandPowerW calculations to avoid redundant spectrum power summations.
 * Caches power calculations with a time-to-live (TTL) to handle dynamic scenarios.
 */
class SpectrumPowerCache
{
  public:
    /**
     * Cache key structure combining spectrum value pointer and band segments.
     * Timestamp removed to improve cache hit rate - TTL validation now done separately.
     * Phase 3B: Added cached hash for performance optimization.
     */
    struct CacheKey
    {
        Ptr<SpectrumValue> psd;                              ///< Power spectral density
        std::vector<std::pair<uint32_t, uint32_t>> segments; ///< Band segment indices
        mutable std::size_t cachedHash;                      ///< Cached hash value (computed lazily)
        mutable bool hashComputed;                           ///< Whether hash has been computed

        /**
         * Constructor - hash computation deferred until needed (lazy evaluation)
         */
        CacheKey(Ptr<SpectrumValue> p, const std::vector<std::pair<uint32_t, uint32_t>>& s)
            : psd(p),
              segments(s),
              cachedHash(0),
              hashComputed(false)
        {
        }

    public:

        /**
         * Equality operator for hash map lookups (inline for performance)
         * \param other the other key to compare
         * \return true if keys are equal
         */
        inline bool operator==(const CacheKey& other) const
        {
            return psd == other.psd && segments == other.segments;
        }
    };

    /**
     * Hash functor for CacheKey to enable std::unordered_map
     * Phase 3B: Lazy hash computation - compute once on first use, cache for reuse
     */
    struct CacheKeyHash
    {
        inline std::size_t operator()(const CacheKey& key) const
        {
            // Return cached hash if already computed
            if (key.hashComputed)
            {
                return key.cachedHash;
            }

            // Compute hash only once (on first use)
            // Hash the pointer address
            std::size_t h1 = std::hash<void*>{}(key.psd.operator->());

            // Hash the segments vector using boost-style hash_combine
            std::size_t h2 = 0;
            for (const auto& seg : key.segments)
            {
                // Optimized hash combine (similar to boost::hash_combine)
                h2 ^= std::hash<uint32_t>{}(seg.first) + 0x9e3779b9 + (h2 << 6) + (h2 >> 2);
                h2 ^= std::hash<uint32_t>{}(seg.second) + 0x9e3779b9 + (h2 << 6) + (h2 >> 2);
            }

            // Combine the two hashes and cache for future use
            key.cachedHash = h1 ^ (h2 << 1);
            key.hashComputed = true;

            return key.cachedHash;
        }
    };

    /**
     * Constructor with optimized default parameters
     */
    SpectrumPowerCache()
        : m_ttl(MilliSeconds(1)),  // Increased from 100μs to 1ms for better hit rate
          m_maxSize(10000),         // Increased from 1000 to 10000 for multi-AP scenarios
          m_hits(0),
          m_misses(0),
          m_evictions(0)
    {
    }

    /**
     * Get cached power value if it exists and is still valid
     * \param key the cache key
     * \return the cached power value, or std::nullopt if not found/expired
     */
    std::optional<Watt_u> Get(const CacheKey& key)
    {
        auto it = m_cache.find(key);
        if (it != m_cache.end())
        {
            Time age = Simulator::Now() - it->second.insertTime;
            if (age <= m_ttl)
            {
                // Move to front of LRU list (most recently used)
                m_lruList.splice(m_lruList.begin(), m_lruList, it->second.lruIt);
                m_hits++;
                return it->second.power;
            }
            else
            {
                // Expired entry - remove it
                m_lruList.erase(it->second.lruIt);
                m_cache.erase(it);
            }
        }
        m_misses++;
        return std::nullopt;
    }

    /**
     * Store power value in cache with O(1) LRU eviction
     * \param key the cache key
     * \param power the power value to cache
     */
    void Put(const CacheKey& key, Watt_u power)
    {
        // Check if key already exists (update case)
        auto it = m_cache.find(key);
        if (it != m_cache.end())
        {
            // Update existing entry and move to front of LRU
            it->second.power = power;
            it->second.insertTime = Simulator::Now();
            m_lruList.splice(m_lruList.begin(), m_lruList, it->second.lruIt);
            return;
        }

        // Evict least recently used entry if cache is full (O(1) operation)
        if (m_cache.size() >= m_maxSize)
        {
            // Remove from back of LRU list (least recently used)
            CacheKey lruKey = m_lruList.back();
            m_lruList.pop_back();
            m_cache.erase(lruKey);
            m_evictions++;
        }

        // Insert new entry at front of LRU list (most recently used)
        m_lruList.push_front(key);
        auto lruIt = m_lruList.begin();
        m_cache[key] = {power, Simulator::Now(), lruIt};
    }

    /**
     * Get cache hit rate
     * \return hit rate as a fraction (0.0 to 1.0)
     */
    double GetHitRate() const
    {
        uint64_t total = m_hits + m_misses;
        return total > 0 ? static_cast<double>(m_hits) / total : 0.0;
    }

    /**
     * Get total number of cache hits
     * \return number of hits
     */
    uint64_t GetHits() const
    {
        return m_hits;
    }

    /**
     * Get total number of cache misses
     * \return number of misses
     */
    uint64_t GetMisses() const
    {
        return m_misses;
    }

    /**
     * Get total number of cache evictions
     * \return number of evictions
     */
    uint64_t GetEvictions() const
    {
        return m_evictions;
    }

    /**
     * Get current cache size
     * \return number of entries currently in cache
     */
    size_t GetSize() const
    {
        return m_cache.size();
    }

    /**
     * Clear the cache and reset statistics
     */
    void Clear()
    {
        m_cache.clear();
        m_lruList.clear();
        m_hits = 0;
        m_misses = 0;
        m_evictions = 0;
    }

    /**
     * Set cache time-to-live
     * \param ttl the time-to-live for cache entries
     */
    void SetTTL(Time ttl)
    {
        m_ttl = ttl;
    }

    /**
     * Set maximum cache size
     * \param size the maximum number of entries
     */
    void SetMaxSize(size_t size)
    {
        m_maxSize = size;
    }

  private:
    /**
     * Cache entry structure with LRU tracking
     */
    struct CacheEntry
    {
        Watt_u power;                              ///< Cached power value
        Time insertTime;                           ///< Time when entry was inserted
        std::list<CacheKey>::iterator lruIt;       ///< Iterator to position in LRU list
    };

    // Hash map for O(1) lookups (replaces red-black tree std::map)
    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> m_cache;

    // LRU list for O(1) eviction (front = most recent, back = least recent)
    std::list<CacheKey> m_lruList;

    Time m_ttl;           ///< Time-to-live for cache entries
    size_t m_maxSize;     ///< Maximum cache size
    uint64_t m_hits;      ///< Number of cache hits
    uint64_t m_misses;    ///< Number of cache misses
    uint64_t m_evictions; ///< Number of cache evictions
};

} // namespace ns3

#endif /* SPECTRUM_POWER_CACHE_H */
