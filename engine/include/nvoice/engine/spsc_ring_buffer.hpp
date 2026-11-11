// N Voice for Studio - Audio Engine (OBS-independent)
// Lock-free single-producer / single-consumer ring buffer.
//
// Used to hand audio between the audio thread and worker threads (e.g. the
// AI inference worker in Phase 4) without locks or allocation on the audio
// thread. Capacity is fixed at construction.
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace nvoice::engine {

template<typename T> class SpscRingBuffer {
	static_assert(std::is_trivially_copyable_v<T>, "SpscRingBuffer requires trivially copyable T");

public:
	// Holds up to `capacity` elements. Allocates once, here.
	explicit SpscRingBuffer(std::size_t capacity)
		: size_(capacity + 1),
		  buffer_(std::make_unique<T[]>(capacity + 1))
	{
	}

	std::size_t capacity() const noexcept { return size_ - 1; }

	// Consumer-side estimate; exact when called from the consumer.
	std::size_t availableToRead() const noexcept
	{
		const auto w = write_.load(std::memory_order_acquire);
		const auto r = read_.load(std::memory_order_relaxed);
		return w >= r ? w - r : size_ - r + w;
	}

	// Producer-side estimate; exact when called from the producer.
	std::size_t availableToWrite() const noexcept
	{
		const auto w = write_.load(std::memory_order_relaxed);
		const auto r = read_.load(std::memory_order_acquire);
		const std::size_t used = w >= r ? w - r : size_ - r + w;
		return capacity() - used;
	}

	// Producer. Writes up to `count` elements; returns how many were written.
	std::size_t push(const T *data, std::size_t count) noexcept
	{
		const auto w = write_.load(std::memory_order_relaxed);
		const auto r = read_.load(std::memory_order_acquire);
		const std::size_t used = w >= r ? w - r : size_ - r + w;
		const std::size_t n = count < capacity() - used ? count : capacity() - used;

		std::size_t idx = w;
		for (std::size_t i = 0; i < n; ++i) {
			buffer_[idx] = data[i];
			idx = idx + 1 == size_ ? 0 : idx + 1;
		}
		write_.store(idx, std::memory_order_release);
		return n;
	}

	// Consumer. Reads up to `count` elements; returns how many were read.
	std::size_t pop(T *out, std::size_t count) noexcept
	{
		const auto r = read_.load(std::memory_order_relaxed);
		const auto w = write_.load(std::memory_order_acquire);
		const std::size_t avail = w >= r ? w - r : size_ - r + w;
		const std::size_t n = count < avail ? count : avail;

		std::size_t idx = r;
		for (std::size_t i = 0; i < n; ++i) {
			out[i] = buffer_[idx];
			idx = idx + 1 == size_ ? 0 : idx + 1;
		}
		read_.store(idx, std::memory_order_release);
		return n;
	}

private:
	const std::size_t size_;
	std::unique_ptr<T[]> buffer_;
	alignas(64) std::atomic<std::size_t> write_{0};
	alignas(64) std::atomic<std::size_t> read_{0};
};

} // namespace nvoice::engine
