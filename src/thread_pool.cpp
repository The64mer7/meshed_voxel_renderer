#include "thread_pool.hpp"

ThreadPool::~ThreadPool()
{
	shutdown();
}

void ThreadPool::init(uint64_t thread_count, uint64_t allocator_initial_capacity)
{
	shutdown();

	m_is_running = true;

	for (uint64_t i = 0; i < thread_count; i++)
	{
		m_workers.emplace_back(worker_exec, this, i);
		m_allocators.emplace_back();
		m_allocators[i].init(allocator_initial_capacity);
	}
}

void ThreadPool::submit(std::function<void(ArenaAllocator* allocator)> cmd)
{
	{
		std::lock_guard lock(m_cmd_mtx);
		if (!m_is_running)
			return;
		m_cmds.push(std::move(cmd));
	}
	m_cmd_cv.notify_one();
}

void ThreadPool::shutdown()
{
	{
		std::lock_guard lock(m_cmd_mtx);

		if (!m_is_running)
			return;

		m_is_running = false;
	}

	m_cmd_cv.notify_all();
	for (auto& t : m_workers)
	{
		if (t.joinable())
			t.join();
	}
	m_workers.clear();
}

size_t ThreadPool::get_worker_count()
{
	return m_workers.size();
}

void ThreadPool::worker_exec(ThreadPool* tp, uint64_t id)
{
	while (true)
	{
		std::unique_lock lock(tp->m_cmd_mtx);

		tp->m_cmd_cv.wait(lock,
			[tp]()
			{
				return !tp->m_cmds.empty() || !tp->m_is_running;
			}
		);

		if (!tp->m_is_running && tp->m_cmds.empty())
			break;

		auto cmd = std::move(tp->m_cmds.front());
		tp->m_cmds.pop();
		lock.unlock();
		ArenaAllocator* allocator = &tp->m_allocators[id];

		allocator->reset();
		cmd(allocator);
	}
}
