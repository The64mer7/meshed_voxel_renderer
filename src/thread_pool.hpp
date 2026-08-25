#pragma once
#include <iostream>
#include <thread>
#include <mutex>
#include <functional>
#include <queue>

#include "arena_allocator.hpp"

class ThreadPool
{
public:
	~ThreadPool();

	void init(uint64_t thread_count, uint64_t allocator_initial_capacity);

	void submit(std::function<void(ArenaAllocator* allocator)> cmd);

	void shutdown();

	size_t get_worker_count();
	size_t get_task_count();

private:
	static void worker_exec(ThreadPool* tp, uint64_t id);
	bool m_is_running = true;

	std::vector<std::thread> m_workers;
	std::vector<ArenaAllocator> m_allocators;

	std::queue<std::function<void(ArenaAllocator* allocator)>> m_cmds;
	std::mutex m_cmd_mtx;
	std::condition_variable m_cmd_cv;
};
