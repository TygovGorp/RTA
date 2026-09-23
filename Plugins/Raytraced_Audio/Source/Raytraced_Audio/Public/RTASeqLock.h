#pragma once

#include "CoreMinimal.h"
#include <atomic>

template <typename T>
class TRTASeqLock
{
	static_assert(std::is_trivially_copyable_v<T>,
		"TRTASeqLock payloads are memcpy'd under a racing writer and must be trivially copyable.");

public:
	TRTASeqLock() : Sequence(0), Payload{} {}

	explicit TRTASeqLock(const T& Initial) : Sequence(0), Payload(Initial) {}

	void Write(const T& NewValue)
	{
		const uint32 Start = Sequence.load(std::memory_order_relaxed);
		Sequence.store(Start + 1, std::memory_order_relaxed);   // -> odd: write in progress
		std::atomic_thread_fence(std::memory_order_release);

		Payload = NewValue;

		std::atomic_thread_fence(std::memory_order_release);
		Sequence.store(Start + 2, std::memory_order_relaxed);   // -> even: write complete
	}

	bool TryRead(T& OutValue, int32 MaxAttempts = 4) const
	{
		for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
		{
			const uint32 Before = Sequence.load(std::memory_order_relaxed);
			if ((Before & 1u) != 0u)
			{
				continue;   // writer is inside the critical section
			}

			std::atomic_thread_fence(std::memory_order_acquire);
			T Candidate = Payload;
			std::atomic_thread_fence(std::memory_order_acquire);

			if (Sequence.load(std::memory_order_relaxed) == Before)
			{
				OutValue = Candidate;
				return true;
			}
		}
		return false;
	}

private:
	std::atomic<uint32> Sequence;
	T Payload;
};
