#pragma once
#include <thread>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <queue>
#include <stack>
#include <condition_variable>

template<typename T>
class ThreadSafeQueue
{
public:
	void NotifyAll()
	{
		m_Cv.notify_all();
	}

	void Enqueue(const T& value)
	{
		{
			std::lock_guard lock(m_Mtx);
			m_Queue.push(value);
		}
		m_Cv.notify_one();
	}

	void Enqueue(const T* values, uint64_t n)
	{
		{
			std::lock_guard lock(m_Mtx);
			for (uint64_t i = 0; i < n; i++)
				m_Queue.push(values[i]);
		}
		m_Cv.notify_all();
	}

	bool TryDequeue(T& out)
	{
		std::lock_guard lock(m_Mtx);
		if (m_Queue.empty())
			return false;

		out = m_Queue.front();
		m_Queue.pop();

		return true;
	}

	bool TryDequeueNonBlocking(T& out)
	{
		if (m_Mtx.try_lock())
		{
			if (m_Queue.empty())
			{
				m_Mtx.unlock();
				return false;
			}
			out = m_Queue.front();
			m_Queue.pop();
			m_Mtx.unlock();
			return true;
		}
		return false;
	}

	bool TryFront(T& out)
	{
		std::lock_guard lock(m_Mtx);
		if (m_Queue.empty())
			return false;

		out = m_Queue.front();

		return true;
	}

	void WaitAndDequeue(T& out)
	{
		std::unique_lock lock(m_Mtx);
		m_Cv.wait(lock, [this] {return !m_Queue.empty(); });

		out = std::move(m_Queue.front());
		m_Queue.pop();
	}

	uint64_t WaitAndFlushN(T* out, uint64_t n)
	{
		std::unique_lock lock(m_Mtx);
		m_Cv.wait(lock, [this] {return !m_Queue.empty(); });

		size_t count = 0;
		for (uint64_t i = 0; i < n; i++)
		{
			if (m_Queue.empty())
				return count;
			out[i] = std::move(m_Queue.front());
			m_Queue.pop();
			count++;
		}
		return count;
	}

	void Clear()
	{
		std::unique_lock lock(m_Mtx);
		m_Queue = std::queue<T>();
	}

	size_t Size()
	{
		std::unique_lock lock(m_Mtx);
		size_t size = m_Queue.size();
		return size;
	}
private:

	std::condition_variable m_Cv;
	std::mutex m_Mtx;
	std::queue<T> m_Queue;
};

template <typename T, uint8_t N, bool accelerate>
class ThreadSafePriorityQueue
{
public:
	uint8_t GetMaxPriority()
	{
		return N;
	}

	void NotifyAll()
	{
		m_Cv.notify_all();
	}

	void Enqueue(const T& value, uint8_t priority)
	{
		{
			std::lock_guard lock(m_Mtx);
			m_Queues[priority].push(value);
			m_Priorities.push(priority);

			m_ElemCount++;
		}
		m_Cv.notify_one();
	}

	bool TryDequeue(T& out)
	{
		std::lock_guard lock(m_Mtx);

		auto& queue = NonEmptyQueue();
		if (queue.empty())
			return false;

		out = queue.front();
		queue.pop();
		m_Priorities.pop();
		m_ElemCount--;

		return true;
	}

	bool TryDequeueNonBlocking(T& out)
	{
		if (m_Mtx.try_lock())
		{
			auto& queue = NonEmptyQueue();

			if (queue.empty())
			{
				m_Mtx.unlock();
				return false;
			}
			out = queue.front();
			queue.pop();
			m_Priorities.pop();
			m_ElemCount--;

			m_Mtx.unlock();
			return true;
		}
		return false;
	}

	bool TryFront(T& out)
	{
		std::lock_guard lock(m_Mtx);
		auto& queue = NonEmptyQueue();

		if (queue.empty())
			return false;

		out = queue.front();

		return true;
	}

	void WaitAndDequeue(T& out)
	{
		std::unique_lock lock(m_Mtx);
		m_Cv.wait(lock, [this] {return m_ElemCount > 0; });
		auto& queue = NonEmptyQueue();

		out = queue.front();
		queue.pop();
		m_ElemCount--;
	}

	void Clear()
	{
		std::unique_lock lock(m_Mtx);
		for (uint8_t i = 0; i < N; i++)
			m_Queues[i] = std::queue<T>();
		m_ElemCount = 0;
		m_Priorities = std::queue<uint8_t>();
	}

	size_t Size()
	{
		std::unique_lock lock(m_Mtx);
		return m_ElemCount;
	}
private:
	std::queue<T>& NonEmptyQueue()
	{
		if constexpr (accelerate)
		{
			if (m_Priorities.empty())
				return m_Queues[0];
			uint8_t priority = m_Priorities.front(); m_Priorities.pop();
			return m_Queues[priority];
		}
		else
		{
			for (uint8_t i = 0; i < N; i++)
			{
				if (m_Queues[i].size() != 0)
				{
					return m_Queues[i];
				}
			}
		}
		return m_Queues[0];
	}

	uint64_t m_ElemCount = 0;
	std::condition_variable m_Cv;
	std::mutex m_Mtx;
	std::array<std::queue<T>, N> m_Queues;
	std::queue<uint8_t> m_Priorities;
};

template<typename T>
class ThreadSafeStack
{
public:
	void Push(const T& value)
	{
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_Stack.push(value);
		}
		m_Cv.notify_one();
	}

	void WaitAndPop(T& out)
	{
		std::unique_lock<std::mutex> lock(m_Mutex);
		m_Cv.wait(lock, [this] {return !m_Stack.empty(); });

		out = std::move(m_Stack.top());
		m_Stack.pop();
	}

	bool TryPop(T& out)
	{
		std::unique_lock<std::mutex> lock(m_Mutex);
		if (m_Stack.empty())
			return false;

		out = std::move(m_Stack.top());
		m_Stack.pop();

		return true;
	}

	bool IsEmpty()
	{
		std::lock_guard lock(m_Mutex);
		return m_Stack.empty();
	}

	size_t Size()
	{
		std::lock_guard lock(m_Mutex);
		return m_Stack.size();
	}

private:
	std::mutex m_Mutex;
	std::condition_variable m_Cv;
	std::stack<T> m_Stack;
};

