// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstring>
#include "common/types.h"

#if defined(__x86_64__) || defined(_M_X64)
#ifdef __AVX2__
#include <immintrin.h>
#define SIMD_AVX2_AVAILABLE
#elif defined(__SSE2__)
#include <emmintrin.h>
#define SIMD_SSE2_AVAILABLE
#endif
#endif

namespace Common {

/**
 * @brief SIMD-optimized memory copy for buffer operations
 * 
 * Uses AVX2 (256-bit) or SSE2 (128-bit) instructions when available for improved performance
 * on large memory copies. Falls back to standard memcpy for small sizes or unsupported platforms.
 * 
 * @param dst Destination pointer (must not overlap with src)
 * @param src Source pointer
 * @param size Number of bytes to copy
 */
inline void SimdMemcpy(void* dst, const void* src, size_t size) {
#ifdef SIMD_AVX2_AVAILABLE
    // For sizes >= 32 bytes, use AVX2 for optimal performance
    constexpr size_t AVX2_SIZE = 32;
    
    if (size >= AVX2_SIZE * 4) {
        u8* d = static_cast<u8*>(dst);
        const u8* s = static_cast<const u8*>(src);
        
        // Handle unaligned start
        const size_t align_offset = reinterpret_cast<uintptr_t>(d) & (AVX2_SIZE - 1);
        if (align_offset != 0) {
            const size_t align_size = AVX2_SIZE - align_offset;
            std::memcpy(d, s, align_size);
            d += align_size;
            s += align_size;
            size -= align_size;
        }
        
        // Main AVX2 loop - process 128 bytes (4 x 32-byte registers) at a time
        const size_t avx2_chunks = size / (AVX2_SIZE * 4);
        for (size_t i = 0; i < avx2_chunks; ++i) {
            const __m256i data0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
            const __m256i data1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + AVX2_SIZE));
            const __m256i data2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + AVX2_SIZE * 2));
            const __m256i data3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + AVX2_SIZE * 3));
            
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), data0);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + AVX2_SIZE), data1);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + AVX2_SIZE * 2), data2);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + AVX2_SIZE * 3), data3);
            
            s += AVX2_SIZE * 4;
            d += AVX2_SIZE * 4;
        }
        
        size -= avx2_chunks * AVX2_SIZE * 4;
        
        // Process remaining full AVX2 blocks
        while (size >= AVX2_SIZE) {
            const __m256i data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), data);
            s += AVX2_SIZE;
            d += AVX2_SIZE;
            size -= AVX2_SIZE;
        }
        
        // Copy remaining bytes
        if (size > 0) {
            std::memcpy(d, s, size);
        }
        return;
    }
#elif defined(SIMD_SSE2_AVAILABLE)
    // For SSE2, use 16-byte operations
    constexpr size_t SSE2_SIZE = 16;
    
    if (size >= SSE2_SIZE * 4) {
        u8* d = static_cast<u8*>(dst);
        const u8* s = static_cast<const u8*>(src);
        
        // Main SSE2 loop - process 64 bytes at a time
        const size_t sse2_chunks = size / (SSE2_SIZE * 4);
        for (size_t i = 0; i < sse2_chunks; ++i) {
            const __m128i data0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
            const __m128i data1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + SSE2_SIZE));
            const __m128i data2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + SSE2_SIZE * 2));
            const __m128i data3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + SSE2_SIZE * 3));
            
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d), data0);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d + SSE2_SIZE), data1);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d + SSE2_SIZE * 2), data2);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d + SSE2_SIZE * 3), data3);
            
            s += SSE2_SIZE * 4;
            d += SSE2_SIZE * 4;
        }
        
        size -= sse2_chunks * SSE2_SIZE * 4;
        
        // Process remaining full SSE2 blocks
        while (size >= SSE2_SIZE) {
            const __m128i data = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d), data);
            s += SSE2_SIZE;
            d += SSE2_SIZE;
            size -= SSE2_SIZE;
        }
        
        // Copy remaining bytes
        if (size > 0) {
            std::memcpy(d, s, size);
        }
        return;
    }
#endif
    
    // Fallback to standard memcpy for small sizes or unsupported platforms
    std::memcpy(dst, src, size);
}

/**
 * @brief SIMD-optimized memory comparison
 * 
 * Uses AVX2 or SSE2 instructions when available for improved performance
 * on large memory comparisons.
 * 
 * @param ptr1 First memory pointer
 * @param ptr2 Second memory pointer
 * @param size Number of bytes to compare
 * @return 0 if equal, non-zero otherwise (same semantics as memcmp)
 */
inline int SimdMemcmp(const void* ptr1, const void* ptr2, size_t size) {
#ifdef SIMD_AVX2_AVAILABLE
    constexpr size_t AVX2_SIZE = 32;
    
    if (size >= AVX2_SIZE * 4) {
        const u8* p1 = static_cast<const u8*>(ptr1);
        const u8* p2 = static_cast<const u8*>(ptr2);
        
        // Process 128 bytes at a time
        const size_t avx2_chunks = size / (AVX2_SIZE * 4);
        for (size_t i = 0; i < avx2_chunks; ++i) {
            const __m256i data1_0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p1));
            const __m256i data2_0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p2));
            const __m256i data1_1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p1 + AVX2_SIZE));
            const __m256i data2_1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p2 + AVX2_SIZE));
            const __m256i data1_2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p1 + AVX2_SIZE * 2));
            const __m256i data2_2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p2 + AVX2_SIZE * 2));
            const __m256i data1_3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p1 + AVX2_SIZE * 3));
            const __m256i data2_3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p2 + AVX2_SIZE * 3));
            
            const __m256i cmp0 = _mm256_cmpeq_epi8(data1_0, data2_0);
            const __m256i cmp1 = _mm256_cmpeq_epi8(data1_1, data2_1);
            const __m256i cmp2 = _mm256_cmpeq_epi8(data1_2, data2_2);
            const __m256i cmp3 = _mm256_cmpeq_epi8(data1_3, data2_3);
            
            const int mask0 = _mm256_movemask_epi8(cmp0);
            const int mask1 = _mm256_movemask_epi8(cmp1);
            const int mask2 = _mm256_movemask_epi8(cmp2);
            const int mask3 = _mm256_movemask_epi8(cmp3);
            
            // movemask returns -1 (0xFFFFFFFF) when all bytes are equal
            if ((mask0 & mask1 & mask2 & mask3) != static_cast<int>(0xFFFFFFFF)) {
                // Found a difference, use memcmp for exact location
                return std::memcmp(p1, p2, AVX2_SIZE * 4);
            }
            
            p1 += AVX2_SIZE * 4;
            p2 += AVX2_SIZE * 4;
        }
        
        size -= avx2_chunks * AVX2_SIZE * 4;
        
        // Check remaining bytes
        if (size > 0) {
            return std::memcmp(p1, p2, size);
        }
        return 0;
    }
#elif defined(SIMD_SSE2_AVAILABLE)
    constexpr size_t SSE2_SIZE = 16;
    
    if (size >= SSE2_SIZE * 4) {
        const u8* p1 = static_cast<const u8*>(ptr1);
        const u8* p2 = static_cast<const u8*>(ptr2);
        
        // Process 64 bytes at a time
        const size_t sse2_chunks = size / (SSE2_SIZE * 4);
        for (size_t i = 0; i < sse2_chunks; ++i) {
            const __m128i data1_0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p1));
            const __m128i data2_0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p2));
            const __m128i data1_1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p1 + SSE2_SIZE));
            const __m128i data2_1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p2 + SSE2_SIZE));
            const __m128i data1_2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p1 + SSE2_SIZE * 2));
            const __m128i data2_2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p2 + SSE2_SIZE * 2));
            const __m128i data1_3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p1 + SSE2_SIZE * 3));
            const __m128i data2_3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p2 + SSE2_SIZE * 3));
            
            const __m128i cmp0 = _mm_cmpeq_epi8(data1_0, data2_0);
            const __m128i cmp1 = _mm_cmpeq_epi8(data1_1, data2_1);
            const __m128i cmp2 = _mm_cmpeq_epi8(data1_2, data2_2);
            const __m128i cmp3 = _mm_cmpeq_epi8(data1_3, data2_3);
            
            const int mask0 = _mm_movemask_epi8(cmp0);
            const int mask1 = _mm_movemask_epi8(cmp1);
            const int mask2 = _mm_movemask_epi8(cmp2);
            const int mask3 = _mm_movemask_epi8(cmp3);
            
            if ((mask0 & mask1 & mask2 & mask3) != 0xFFFF) {
                // Found a difference
                return std::memcmp(p1, p2, SSE2_SIZE * 4);
            }
            
            p1 += SSE2_SIZE * 4;
            p2 += SSE2_SIZE * 4;
        }
        
        size -= sse2_chunks * SSE2_SIZE * 4;
        
        // Check remaining bytes
        if (size > 0) {
            return std::memcmp(p1, p2, size);
        }
        return 0;
    }
#endif
    
    // Fallback to standard memcmp
    return std::memcmp(ptr1, ptr2, size);
}

/**
 * @brief SIMD-optimized memory set to zero
 * 
 * Uses AVX2 or SSE2 instructions when available for improved performance.
 * 
 * @param dst Destination pointer
 * @param size Number of bytes to zero
 */
inline void SimdMemzero(void* dst, size_t size) {
#ifdef SIMD_AVX2_AVAILABLE
    constexpr size_t AVX2_SIZE = 32;
    
    if (size >= AVX2_SIZE * 4) {
        u8* d = static_cast<u8*>(dst);
        const __m256i zero = _mm256_setzero_si256();
        
        // Process 128 bytes at a time
        const size_t avx2_chunks = size / (AVX2_SIZE * 4);
        for (size_t i = 0; i < avx2_chunks; ++i) {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), zero);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + AVX2_SIZE), zero);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + AVX2_SIZE * 2), zero);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + AVX2_SIZE * 3), zero);
            
            d += AVX2_SIZE * 4;
        }
        
        size -= avx2_chunks * AVX2_SIZE * 4;
        
        // Process remaining full AVX2 blocks
        while (size >= AVX2_SIZE) {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), zero);
            d += AVX2_SIZE;
            size -= AVX2_SIZE;
        }
        
        // Zero remaining bytes
        if (size > 0) {
            std::memset(d, 0, size);
        }
        return;
    }
#elif defined(SIMD_SSE2_AVAILABLE)
    constexpr size_t SSE2_SIZE = 16;
    
    if (size >= SSE2_SIZE * 4) {
        u8* d = static_cast<u8*>(dst);
        const __m128i zero = _mm_setzero_si128();
        
        // Process 64 bytes at a time
        const size_t sse2_chunks = size / (SSE2_SIZE * 4);
        for (size_t i = 0; i < sse2_chunks; ++i) {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d), zero);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d + SSE2_SIZE), zero);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d + SSE2_SIZE * 2), zero);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d + SSE2_SIZE * 3), zero);
            
            d += SSE2_SIZE * 4;
        }
        
        size -= sse2_chunks * SSE2_SIZE * 4;
        
        // Process remaining full SSE2 blocks
        while (size >= SSE2_SIZE) {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d), zero);
            d += SSE2_SIZE;
            size -= SSE2_SIZE;
        }
        
        // Zero remaining bytes
        if (size > 0) {
            std::memset(d, 0, size);
        }
        return;
    }
#endif
    
    // Fallback to standard memset
    std::memset(dst, 0, size);
}

} // namespace Common
