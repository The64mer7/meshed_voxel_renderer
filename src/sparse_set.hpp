#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>


template <class T>
void swap_and_pop(std::vector<T>& vector, size_t index)
{
	vector[index] = vector.back();
	vector.pop_back();
}

template <class Key, class Value>
class SparseSet
{
public:

	bool insert(const Key& key, const Value& value)
	{
		auto it = m_to_index.find(key);

		if (it != m_to_index.end())
			return false;

		m_to_index[key] = m_keys.size();

		m_keys.push_back(key);
		m_values.push_back(value);

		return true;
	}

	bool emplace(const Key& key)
	{
		auto it = m_to_index.find(key);

		if (it != m_to_index.end())
			return false;

		m_to_index[key] = m_keys.size();

		m_keys.push_back(key);
		m_values.emplace_back();

		return true;
	}

	Value* get_or_emplace(const Key& key)
	{
		auto it = m_to_index.find(key);

		if (it != m_to_index.end())
		{
			return &m_values[it->second];
		}

		size_t new_index = m_keys.size();
		m_to_index[key] = new_index;

		m_keys.push_back(key);
		m_values.emplace_back();

		return &m_values[new_index];
	}

	Value* get(const Key& key)
	{
		auto it = m_to_index.find(key);

		if (it == m_to_index.end())
			return nullptr;

		return &m_values[it->second];
	}

	const Value* get(const Key& key) const
	{
		auto it = m_to_index.find(key);

		if (it == m_to_index.end())
			return nullptr;

		return &m_values[it->second];
	}

	bool remove(const Key& key)
	{
		auto it = m_to_index.find(key);

		if (it == m_to_index.end())
			return false;

		size_t to_remove_index = it->second;
		size_t last_index = m_keys.size() - 1;

		if (to_remove_index != last_index)
		{
			const Key& last_key = m_keys[last_index];

			m_keys[to_remove_index] = std::move(m_keys[last_index]);
			m_values[to_remove_index] = std::move(m_values[last_index]);

			m_to_index[last_key] = to_remove_index;
		}

		m_keys.pop_back();
		m_values.pop_back();

		m_to_index.erase(it);

		return true;
	}

	const std::vector<Key>& get_keys() const
	{
		return m_keys;
	}

	const std::vector<Value>& get_values() const
	{
		return m_values;
	}

private:

	std::unordered_map<Key, size_t> m_to_index;
	std::vector<Key> m_keys;
	std::vector<Value> m_values;
};
