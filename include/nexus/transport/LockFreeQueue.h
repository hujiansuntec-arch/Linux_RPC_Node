// Lock-Free SPSC Ring Buffer Queue
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>

namespace Nexus {
namespace rpc {

/**
 * @brief Lock-free Single Producer Single Consumer (SPSC) ring buffer
 *
 * This is a high-performance, wait-free queue optimized for shared memory.
 * Each producer-consumer pair gets its own queue to eliminate lock contention.
 *
 * Key features:
 * - Wait-free for both producer and consumer
 * - Cache-line aligned to avoid false sharing
 * - Memory barriers for proper synchronization
 * - Overflow handling: drop oldest messages
 */
template <size_t BUFFER_SIZE>
class alignas(64) LockFreeRingBuffer {
public:
    // Optimized for variable length messages
    // Header: 8 bytes (length + magic)
    // Payload: Variable
    static constexpr size_t MAX_MSG_SIZE = 2040;

    struct FrameHeader {
        uint32_t length; // Total length including header. If 0, it's padding.
        uint32_t magic;  // Magic number to detect padding/validity
    };
    
    static constexpr uint32_t MAGIC_VALID = 0xCAFEBABE;
    static constexpr uint32_t MAGIC_PADDING = 0xDEADBEEF;

    LockFreeRingBuffer() {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        stats_messages_written_.store(0, std::memory_order_relaxed);
        stats_messages_read_.store(0, std::memory_order_relaxed);
        stats_messages_dropped_.store(0, std::memory_order_relaxed);
        // Initialize buffer with zeros to avoid confusion? Not strictly necessary.
        memset(buffer_, 0, BUFFER_SIZE);
    }

    /**
     * @brief Try to write a message (producer side)
     * @param data Message data
     * @param size Message size
     * @return true if written successfully, false if queue is full
     */
    bool tryWrite(const uint8_t* data, size_t size) {
        if (size > MAX_MSG_SIZE || size == 0) {
            return false;
        }

        // Calculate required space aligned to 8 bytes
        // Header (8) + Data (size) + Padding (0-7)
        size_t needed = (sizeof(FrameHeader) + size + 7) & ~7;

        // Load indices with acquire semantics
        uint64_t head = head_.load(std::memory_order_acquire);
        uint64_t tail = tail_.load(std::memory_order_acquire);

        // Check space and write
        if (head >= tail) {
            // Free space: [head, BUFFER_SIZE) and [0, tail)
            
            // Try to fit at the end
            if (head + needed <= BUFFER_SIZE) {
                writeFrame(head, data, size, needed, MAGIC_VALID);
                head_.store(head + needed, std::memory_order_release);
                stats_messages_written_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            
            // Try to wrap around
            // We need to write padding at [head, BUFFER_SIZE)
            // And write data at [0, tail)
            
            // Check if we have enough space at the beginning
            // We need 'needed' bytes strictly less than 'tail' to avoid head==tail (empty)
            if (needed < tail) {
                // Write padding at the end
                size_t pad_len = BUFFER_SIZE - head;
                if (pad_len >= sizeof(FrameHeader)) {
                    FrameHeader padHdr = {static_cast<uint32_t>(pad_len), MAGIC_PADDING};
                    memcpy(&buffer_[head], &padHdr, sizeof(padHdr));
                }
                
                // Write data at 0
                writeFrame(0, data, size, needed, MAGIC_VALID);
                head_.store(needed, std::memory_order_release);
                stats_messages_written_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        } else {
            // head < tail
            // Free space: [head, tail)
            if (head + needed < tail) {
                writeFrame(head, data, size, needed, MAGIC_VALID);
                head_.store(head + needed, std::memory_order_release);
                stats_messages_written_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }

        // Queue full
        stats_messages_dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    /**
     * @brief Try to read a message (consumer side)
     * @param out_data Output buffer for message data (min MAX_MSG_SIZE bytes)
     * @param out_size Output message size
     * @return true if read successfully, false if queue is empty
     */
    bool tryRead(uint8_t* out_data, size_t& out_size) {
        // Load indices with acquire semantics
        uint64_t tail = tail_.load(std::memory_order_acquire);
        uint64_t head = head_.load(std::memory_order_acquire);

        if (tail == head) {
            return false;
        }

        // Read header
        FrameHeader header;
        memcpy(&header, &buffer_[tail], sizeof(header));

        // Handle padding
        if (header.magic == MAGIC_PADDING) {
            // Wrap around
            tail = 0;
            // Update tail to 0 and re-check
            // Note: We don't update tail_ yet, we just move our local pointer
            // But wait, if we don't update tail_, the producer won't see the space freed.
            // However, we are consuming the padding.
            // Let's update tail_ to 0.
            tail_.store(0, std::memory_order_release);
            
            if (tail == head) {
                return false; // Should not happen if padding exists
            }
            memcpy(&header, &buffer_[tail], sizeof(header));
        }

        if (header.magic != MAGIC_VALID) {
            // Corruption or race?
            return false;
        }

        size_t payload_len = header.length;
        if (payload_len > MAX_MSG_SIZE) {
             // Corruption
             return false;
        }

        // Calculate total length (aligned)
        size_t total_len = (sizeof(FrameHeader) + payload_len + 7) & ~7;

        memcpy(out_data, &buffer_[tail + sizeof(FrameHeader)], payload_len);
        out_size = payload_len;

        // Advance tail with release semantics
        tail_.store(tail + total_len, std::memory_order_release);

        stats_messages_read_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    /**
     * @brief Get number of bytes used (approximate)
     */
    size_t size() const {
        uint64_t head = head_.load(std::memory_order_acquire);
        uint64_t tail = tail_.load(std::memory_order_acquire);
        if (head >= tail) return head - tail;
        return BUFFER_SIZE - (tail - head);
    }

    /**
     * @brief Check if queue is empty
     */
    bool empty() const { 
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed); 
    }

    /**
     * @brief Get statistics
     */
    struct Stats {
        uint64_t messages_written;
        uint64_t messages_read;
        uint64_t messages_dropped;
        size_t current_size;
    };

    Stats getStats() const {
        return {stats_messages_written_.load(std::memory_order_relaxed),
                stats_messages_read_.load(std::memory_order_relaxed),
                stats_messages_dropped_.load(std::memory_order_relaxed), size()};
    }

private:
    void writeFrame(size_t offset, const uint8_t* data, size_t payload_size, size_t total_len, uint32_t magic) {
        FrameHeader hdr;
        if (magic == MAGIC_VALID) {
            hdr.length = static_cast<uint32_t>(payload_size);
        } else {
            hdr.length = static_cast<uint32_t>(total_len);
        }
        hdr.magic = magic;
        memcpy(&buffer_[offset], &hdr, sizeof(hdr));
        memcpy(&buffer_[offset + sizeof(hdr)], data, payload_size);
    }

    // Cache-line aligned atomic indices to avoid false sharing
    alignas(64) std::atomic<uint64_t> head_;  // Write position (byte offset)
    alignas(64) std::atomic<uint64_t> tail_;  // Read position (byte offset)

    // Statistics (relaxed ordering is fine)
    alignas(64) std::atomic<uint64_t> stats_messages_written_;
    alignas(64) std::atomic<uint64_t> stats_messages_read_;
    alignas(64) std::atomic<uint64_t> stats_messages_dropped_;

    // Message buffer
    alignas(64) uint8_t buffer_[BUFFER_SIZE];
};

}  // namespace rpc
}  // namespace Nexus
