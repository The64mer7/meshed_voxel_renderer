#pragma once
#include <iostream>
#include <thread>
#include <mutex>
#include <functional>
#include <queue>

class ThreadPool
{
public:
	~ThreadPool();

	void init(uint64_t thread_count);

	void submit(std::function<void()> cmd);

	void shutdown();

private:
	static void worker_exec(ThreadPool* tp);
	bool m_is_running = true;
	std::vector<std::thread> m_workers;
	std::queue<std::function<void()>> m_cmds;
	std::mutex m_cmd_mtx;
	std::condition_variable m_cmd_cv;
};
