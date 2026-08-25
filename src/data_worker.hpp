#pragma once
#include "thread_safe_stack.hpp"
#include "thread_pool.hpp"

template<typename InT, typename OutT>
class DataWorker
{
public:
	void create(ThreadSafeQueue<InT>* in_queue, ThreadSafeQueue<OutT>* out_queue, std::function<void(ArenaAllocator* allocator) pfn_worker)
	{
		m_in_queue = in_queue;
		m_out_queue = out_queue;
		m_pfn_worker;
	}

	void operator()(ArenaAllocator* allocator)
	{

	}

private:
	ThreadSafeQueue<InT>* m_in_queue;
	ThreadSafeQueue<OutT>* m_out_queue;
	std::function<void(ArenaAllocator* allocator) m_pfn_worker;
};