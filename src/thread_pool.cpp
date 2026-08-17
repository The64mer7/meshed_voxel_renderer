#include "thread_pool.hpp"

ThreadPool::~ThreadPool()
{
	shutdown();
}

void ThreadPool::init(uint64_t thread_count)
{
	shutdown();

	m_is_running = true;

	for (uint64_t i = 0; i < thread_count; i++)
		m_workers.emplace_back(worker_exec, this);
}

void ThreadPool::submit(std::function<void()> cmd)
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

void ThreadPool::worker_exec(ThreadPool* tp)
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

		cmd();
	}
}
