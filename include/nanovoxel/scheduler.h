#pragma once
// reserved
#include <kernel/kernel_def.h>
#include <miyuki.h>

namespace NanoVoxel {
	class Scheduler {
	public:
		virtual reset(const Miyuki::Point2i&) = 0;
		virtual void push(const PerRayData* prd, size_t N) = 0;
		virtual std::vector<PerRayData> popFront(size_t N) = 0;
		virtual ~Scheduler() = default;
	};
}