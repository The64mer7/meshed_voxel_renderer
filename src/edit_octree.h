#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <stack>
#include <functional>
#include <format>
#include <iostream>

#include "world_data.hpp"

struct aabb3d
{
	glm::vec3 min;
	glm::vec3 max;

	inline bool fully_contains(const aabb3d& aabb) const
	{
		if (aabb.min.x < min.x || aabb.max.x > max.x) return false;
		if (aabb.min.y < min.y || aabb.max.y > max.y) return false;
		if (aabb.min.z < min.z || aabb.max.z > max.z) return false;
		return true;
	}

	inline bool is_subset_of(const aabb3d& aabb) const
	{
		return aabb.fully_contains(*this);
	}

	inline bool intersects(const aabb3d& aabb) const
	{
		if (aabb.max.x < min.x || max.x < aabb.min.x) return false;
		if (aabb.max.y < min.y || max.y < aabb.min.y) return false;
		if (aabb.max.z < min.z || max.z < aabb.min.z) return false;
		return true;
	}

	inline bool intersects_triangle(
		const glm::vec3& v0,
		const glm::vec3& v1,
		const glm::vec3& v2) const
	{
		glm::vec3 c = (min + max) * 0.5f;
		glm::vec3 e = (max - min) * 0.5f;

		glm::vec3 v0l = v0 - c;
		glm::vec3 v1l = v1 - c;
		glm::vec3 v2l = v2 - c;

		glm::vec3 f0 = v1l - v0l;
		glm::vec3 f1 = v2l - v1l;
		glm::vec3 f2 = v0l - v2l;

		auto axis_test = [&](glm::vec3 axis) -> bool
			{
				float len2 = glm::dot(axis, axis);
				if (len2 < 1e-12f) return true;

				axis = glm::normalize(axis);

				auto proj = [&](const glm::vec3& v) {
					return glm::dot(v, axis);
					};

				float p0 = proj(v0l);
				float p1 = proj(v1l);
				float p2 = proj(v2l);

				float r =
					e.x * std::abs(axis.x) +
					e.y * std::abs(axis.y) +
					e.z * std::abs(axis.z);

				float min_p = std::min({ p0, p1, p2 });
				float max_p = std::max({ p0, p1, p2 });

				return !(min_p > r || max_p < -r);
			};

		if (!axis_test({ 1, 0, 0 })) return false;
		if (!axis_test({ 0, 1, 0 })) return false;
		if (!axis_test({ 0, 0, 1 })) return false;

		glm::vec3 tri_normal = glm::cross(f0, f1);
		if (!axis_test(tri_normal)) return false;

		glm::vec3 axes[9] = {
			glm::cross(f0, {1,0,0}), glm::cross(f0, {0,1,0}), glm::cross(f0, {0,0,1}),
			glm::cross(f1, {1,0,0}), glm::cross(f1, {0,1,0}), glm::cross(f1, {0,0,1}),
			glm::cross(f2, {1,0,0}), glm::cross(f2, {0,1,0}), glm::cross(f2, {0,0,1}),
		};

		for (auto& a : axes)
			if (!axis_test(a)) return false;

		return true;
	}

	inline bool contains_point(const glm::vec3& p) const
	{
		for (int i = 0; i < 3; i++)
		{
			if (p[i] < min[i] || p[i] > max[i])
				return false;
		}
		return true;
	}

	inline glm::vec3 center() const
	{
		return (min + max) * 0.5f;
	}

	inline glm::vec3 size() const
	{
		return max - min;
	}

	void extend(glm::vec3 p)
	{
		min = glm::min(min, p);
		max = glm::max(max, p);
	}

	aabb3d octree_child(uint32_t index) const
	{
		aabb3d child;
		glm::vec3 child_size = size() * 0.5f;
		child.min.x = min.x + child_size.x * ((index >> 0) & 1);
		child.min.y = min.y + child_size.y * ((index >> 1) & 1);
		child.min.z = min.z + child_size.z * ((index >> 2) & 1);
		child.max.x = child.min.x + child_size.x;
		child.max.y = child.min.y + child_size.y;
		child.max.z = child.min.z + child_size.z;

		return child;
	}

};

class OctreeStructure
{
public:
	virtual ~OctreeStructure() = default;

	virtual uint16_t get_voxel(const glm::vec3& p, float s) = 0;
	//virtual uint16_t voxel_intersect(const glm::vec3& p, float s) = 0;
	virtual void get_bounds(glm::vec3* min, glm::vec3* max) = 0;

	virtual bool is_destructive()
	{
		return false;
	}
};

class SphereStructure : public OctreeStructure
{
public:
	glm::vec3 position;
	float radius;
	uint8_t flags = 0;
	
	static constexpr uint8_t flag_none = 0;
	static constexpr uint8_t flag_destructive = 1;

	uint16_t get_voxel(const glm::vec3& p, float size) override
	{
		return glm::dot(p - position, p - position) <= radius * radius ? 1 : 0;
	}
	void get_bounds(glm::vec3* min, glm::vec3* max) override
	{
		*min = position - radius;
		*max = position + radius;
	}
	bool is_destructive() override
	{
		return flags == 1;
	}
};

class BoxStructure : public OctreeStructure
{
public:
	aabb3d aabb;
	uint16_t get_voxel(const glm::vec3& p, float size) override
	{
		return aabb.contains_point(p) ? 4 : 0;
	}
	void get_bounds(glm::vec3* min, glm::vec3* max) override
	{
		*min = aabb.min;
		*max = aabb.max;
	}
};

class OctahedronStructure : public OctreeStructure
{
public:
	float sdOctahedron(glm::vec3 p)
	{
		p = glm::abs(p - pos);
		return (p.x + p.y + p.z - s) * 0.57735027;
	}
	glm::vec3 pos;
	float s;

	uint16_t get_voxel(const glm::vec3& p, float size) override
	{
		return sdOctahedron(p) < 0.f ? 7 : 0;
	}

	void get_bounds(glm::vec3* min, glm::vec3* max) override
	{
		*min = pos - s;
		*max = pos + s;
	}
};

using structure_id = uint64_t;
using instance_id = uint64_t;
class WorldEdits
{
public:
	using OctreeNode = TreeNode<8, std::vector<structure_id>>;
	void init(aabb3d aabb, uint64_t max_split_factor, uint64_t initial_split_factor)
	{
		this->max_split_factor = max_split_factor;
		m_root_bounds = aabb;
		m_root = new OctreeNode;

		split_to_depth(m_root, initial_split_factor);
	}

	void cleanup()
	{
		m_root->cleanup();
	}

	structure_id create_structure(OctreeStructure* structure)
	{
		m_structures.push_back(structure);
		return m_structures.size() - 1;
	}

	template<typename instance_t>
	std::vector<instance_t>& _get_instances()
	{
		static std::vector<instance_t> instances;
		return instances;
	}

	bool place_structure(structure_id id, const glm::vec3& position)
	{
		aabb3d structure_aabb;
		auto* structure = get_structure(id);
		if (structure == nullptr)
			return false;
		
		structure->get_bounds(&structure_aabb.min, &structure_aabb.max);
		structure_aabb.min += position;
		structure_aabb.max += position;

		glm::vec3 p = structure_aabb.center();

		aabb3d node_bounds;
		glm::ivec3 morton;
		auto* node = find_smallest_node_for_each<0>(structure_aabb, &node_bounds, &morton,
			[id](OctreeNode* node) 
			{
				node->value.push_back(id);
			}
		);

		if (!node)
			return false;

		std::cout << std::format("placing structure in node (new size: {}) {} {} {}\n", node->value.size(), std::bitset<32>(morton.x).to_string(), std::bitset<32>(morton.y).to_string(), std::bitset<32>(morton.z).to_string());
		
		if (node->is_leaf() && node->value.size() >= max_structures_before_split)
		{
			printf("split triggered\n");
			split_node(node, node_bounds);
		}
		else
		{
			node->value.pop_back();
			add_node_structure(node, node_bounds, structure_aabb, id);
		}

		return true;
	}

	inline const std::vector<structure_id>* find_structures_in_region(const aabb3d& aabb)
	{
		OctreeNode* node = find_smallest_node<0>(aabb);
		return node ? &node->value : nullptr;
	}

	OctreeStructure* get_structure(structure_id id)
	{
		return (id < m_structures.size()) ? m_structures[id] : nullptr;
	}

	~WorldEdits()
	{
		for (int i = 0; i < m_structures.size(); i++)
			delete m_structures[i];
	}

	void for_each_leaf(std::function<void(ChunkKey)> fn_for_each)
	{
		struct StackItem
		{
			ChunkKey leaf;
			OctreeNode* node;
		};

		std::stack<StackItem> stack;
		StackItem root;
		root.leaf.raw = glm::ivec4(0);
		root.node = m_root;
		stack.push(root);

		while (!stack.empty())
		{
			StackItem item = stack.top();
			stack.pop();

			bool is_leaf = true;
			for (int i = 0; i < 8; i++)
			{
				if (item.node->children[i])
				{
					is_leaf = false;
					break;
				}
			}

			{
				fn_for_each(item.leaf);
			}

			for (int i = 0; i < 8; i++)
			{
				if (item.node->children[i])
				{
					StackItem child;
					child.leaf.x = (item.leaf.x << 1) | ((i >> 0) & 1);
					child.leaf.y = (item.leaf.y << 1) | ((i >> 1) & 1);
					child.leaf.z = (item.leaf.z << 1) | ((i >> 2) & 1);
					child.leaf.lod = item.leaf.lod + 1;
					child.node = item.node->children[i];

					stack.push(child);
				}
			}
		}
	}

	uint64_t max_structures_before_split = 4;

	void debug_print()
	{
		debug_log(m_root, 0);
	}

	void debug_log(OctreeNode* node, uint64_t depth)
	{
		if (!node)
			return;

		for (int j = 0; j < depth; j++) std::cout << " ";
		std::cout << node->value.size()<< ": ";
		for (int i = 0; i < node->value.size(); i++)
		{
			std::cout << node->value[i] << ' ';
		}
		std::cout << '\n';

		for (int i = 0; i < 8; i++)
			debug_log(node->children[i], depth + 1);
	}

private:

	void split_to_depth(OctreeNode* node, uint64_t depth)
	{
		if (!node || depth == 0)
			return;

		for (int i = 0; i < 8; i++)
		{
			if (!node->children[i])
				node->children[i] = new OctreeNode;
			split_to_depth(node->children[i], depth - 1);
		}
	}

	void split_node(OctreeNode* node, const aabb3d& node_bounds)
	{
		for (int i = 0; i < 8; i++)
		{
			assert(node->children[i] == nullptr);
			if (node->children[i] == nullptr)
				node->children[i] = new OctreeNode;

			aabb3d child_bounds = node_bounds.octree_child(i);

			for (structure_id id : node->value)
			{
				OctreeStructure* structure = get_structure(id);
				aabb3d structure_bounds;
				structure->get_bounds(&structure_bounds.min, &structure_bounds.max);

				if (structure_bounds.intersects(child_bounds))
					add_node_structure(node->children[i], node_bounds, structure_bounds, id);
			}
		}
	}

	void add_node_structure(OctreeNode* node, const aabb3d& node_bounds, const aabb3d& structure_bounds, structure_id id)
	{
		struct StackItem
		{
			OctreeNode* node;
			aabb3d bounds;
		};
		std::stack<StackItem> stack;
		stack.push({ node, node_bounds });

		while (!stack.empty())
		{
			StackItem item = stack.top();
			stack.pop();

			if (!item.node || !structure_bounds.intersects(item.bounds))
				continue;

			item.node->value.push_back(id);

			for (int i = 0; i < 8; i++)
			{
				if (node->children[i])
					stack.push({ item.node->children[i], item.bounds.octree_child(i) });
			} 
		}
	}

	template<int rank>
	OctreeNode* find_smallest_node(const aabb3d& aabb)
	{
		OctreeNode* node = m_root;
		aabb3d node_bounds = m_root_bounds;
		uint64_t depth = 0;

		constexpr int history_count = rank + 1;
		OctreeNode* previous_nodes[history_count];
		for (int i = 0; i < history_count; i++)
			previous_nodes[i] = m_root;

		int write_idx = 0;

		glm::vec3 p = aabb.center();
		while (depth < max_split_factor)
		{
			glm::vec3 curr_center = node_bounds.center();

			uint32_t child_idx = 0;
			for (int i = 0; i < 3; i++)
			{
				if (p[i] >= curr_center[i])
					child_idx |= (1 << i);
			}

			glm::vec3 child_size = node_bounds.size() * 0.5f;
			aabb3d child_bounds;

			glm::vec3 mask = { child_idx & 1, (child_idx >> 1) & 1, (child_idx >> 2) & 1 };
			child_bounds.min = node_bounds.min + mask * child_size;
			child_bounds.max = child_bounds.min + child_size;

			if (!child_bounds.fully_contains(aabb))
				break;
			if (node->children[child_idx] == nullptr)
				break;

			node = node->children[child_idx];
			node_bounds = child_bounds;

			previous_nodes[write_idx++] = node;
			write_idx %= history_count;

			depth++;
		}

		return previous_nodes[write_idx];
	}

	template<int rank>
	OctreeNode* find_smallest_node_for_each(const aabb3d& aabb, aabb3d* out_node_bounds, glm::ivec3* out_morton, std::function<void(OctreeNode*)> fn_for_each_descendant)
	{
		OctreeNode* node = m_root;
		aabb3d node_bounds = m_root_bounds;
		uint64_t depth = 0;

		constexpr int history_count = rank + 1;
		OctreeNode* previous_nodes[history_count];
		for (int i = 0; i < history_count; i++)
			previous_nodes[i] = m_root;

		int write_idx = 0;

		glm::vec3 p = aabb.center();
		*out_morton = glm::ivec3(0);
		while (depth < max_split_factor)
		{
			glm::vec3 curr_center = node_bounds.center();
			fn_for_each_descendant(node);
			previous_nodes[write_idx++] = node;
			write_idx %= history_count;

			uint32_t child_idx = 0;
			for (int i = 0; i < 3; i++)
			{
				if (p[i] >= curr_center[i])
					child_idx |= (1 << i);
			}

			glm::vec3 child_size = node_bounds.size() * 0.5f;
			aabb3d child_bounds;

			glm::ivec3 mask = { child_idx & 1, (child_idx >> 1) & 1, (child_idx >> 2) & 1 };
			child_bounds.min = node_bounds.min + glm::vec3(mask) * child_size;
			child_bounds.max = child_bounds.min + child_size;

			if (!child_bounds.fully_contains(aabb))
				break;
			if (node->children[child_idx] == nullptr)
				break;


			node = node->children[child_idx];
			node_bounds = child_bounds;
			*out_morton <<= 1;
			*out_morton |= mask;

			depth++;
		}
		*out_node_bounds = node_bounds;
		return previous_nodes[write_idx];
	}

	std::vector<OctreeStructure*> m_structures;
	uint64_t max_split_factor;
	OctreeNode* m_root;
	aabb3d m_root_bounds;
};