#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <stack>
#include <functional>
#include <format>
#include <iostream>

#include "vox_parser.h"
#include "world_data.hpp"
#include "vox_loader.h"

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

static void print_binary_file(const std::string& file_path) {
	std::ifstream file_handle(file_path, std::ios::binary | std::ios::ate);

	if (!file_handle.is_open()) {
		std::cerr << "Error: Could not open file " << file_path << "\n";
		return;
	}

	std::streamsize file_size = file_handle.tellg();
	file_handle.seekg(0, std::ios::beg);

	std::vector<unsigned char> buffer(file_size);
	if (!file_handle.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
		std::cerr << "Error: Could not read file content.\n";
		return;
	}

	const std::size_t bytes_per_row = 16;

	for (std::size_t i = 0; i < buffer.size(); i += bytes_per_row) {
		std::cout << std::setw(8) << std::setfill('0') << std::hex << i << "  ";

		for (std::size_t j = 0; j < bytes_per_row; ++j) {
			if (i + j < buffer.size()) {
				std::cout << std::setw(2) << std::setfill('0') << std::hex
					<< static_cast<int>(buffer[i + j]) << " ";
			}
			else {
				std::cout << "   ";
			}

			if (j == 7) std::cout << " ";
		}

		std::cout << " |";

		for (std::size_t j = 0; j < bytes_per_row; ++j) {
			if (i + j < buffer.size()) {
				unsigned char byte_val = buffer[i + j];
				if (byte_val >= 32 && byte_val <= 126) {
					std::cout << byte_val;
				}
				else {
					std::cout << ".";
				}
			}
		}
		std::cout << "|\n";
	}
}

static constexpr int CHUNK_SIZE = 256;
struct ChunkArray
{
	uint16_t data[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE] = {};
};
class VoxStructure : public OctreeStructure
{
public:
	vox::ChunkSizeData size_data;
	std::vector<uint16_t> voxels_3d_map;
	aabb3d bounds;
	glm::vec3 position;

	void set_voxel(int x, int y, int z, uint16_t value)
	{
		voxels_3d_map[size_data.index(x, y, z)] = value;
	}
	
	bool load(const std::string& filepath, const glm::vec3& position)
	{
#if USE_CUSTOM_VOX_LOADER
		this->position = position;

		bounds.min = glm::vec3(0);
		bounds.max = glm::vec3(0);
		
		std::vector<vox::Voxel> voxels;
		std::vector<uint32_t> colors;
		vox::LoadResult result = vox::load_vox(filepath, &voxels, &colors, &size_data);
		
		if (result != vox::LoadResult::success)
			return false;

		std::swap(size_data.y, size_data.z);
		voxels_3d_map.resize(size_data.volume(), 0);
		for (size_t i = 0; i < voxels.size(); i++)
		{
			vox::Voxel voxel = voxels[i];
			set_voxel(voxel.x, voxel.z, voxel.y, vox::quantize_color(colors[i]));
			bounds.extend({ voxel.x, voxel.z, voxel.y });
		}
		bounds.min += position;
		bounds.max += position;

		return true;
#else
		this->position = position;
		const ogt_vox_scene* scene = load_vox_scene(filepath.c_str(), 0);
		bounds.min = glm::vec3(std::numeric_limits<float>::max());
		bounds.max = glm::vec3(std::numeric_limits<float>::lowest());
		bool has_voxels = false;

		for (uint32_t i = 0; i < scene->num_instances; i++)
		{
			const ogt_vox_instance& instance = scene->instances[i];

			if (instance.hidden)
				continue;

			const ogt_vox_model* model = scene->models[instance.model_index];

			glm::mat4 transform_matrix(
				instance.transform.m00, instance.transform.m01, instance.transform.m02, instance.transform.m03,
				instance.transform.m10, instance.transform.m11, instance.transform.m12, instance.transform.m13,
				instance.transform.m20, instance.transform.m21, instance.transform.m22, instance.transform.m23,
				instance.transform.m30, instance.transform.m31, instance.transform.m32, instance.transform.m33
			);
			transform_matrix = glm::rotate(transform_matrix, glm::radians(-90.f), glm::vec3(1, 0, 0));

			for (uint32_t z = 0; z < model->size_z; z++)
			{
				for (uint32_t y = 0; y < model->size_y; y++)
				{
					for (uint32_t x = 0; x < model->size_x; x++)
					{
						uint32_t voxel_index = x + (y * model->size_x) + (z * model->size_x * model->size_y);
						uint8_t color_index = model->voxel_data[voxel_index];
						if (color_index == 0)
							continue;

						ogt_vox_rgba rgba = scene->palette.color[color_index];

						uint32_t packed_color = static_cast<uint32_t>(rgba.r) |
							(static_cast<uint32_t>(rgba.g) << 8) |
							(static_cast<uint32_t>(rgba.b) << 16) |
							(static_cast<uint32_t>(rgba.a) << 24);

						uint16_t quantized_color = vox::quantize_color(packed_color);

						glm::vec4 local_pos(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), 1.f);
						glm::vec4 world_pos_v4 = transform_matrix * local_pos;
						glm::ivec3 world_pos(glm::floor(world_pos_v4));
						bounds.extend(glm::vec3(world_pos));
						has_voxels = true;
						glm::ivec3 chunk_coord = glm::floor(glm::vec3(world_pos) / static_cast<float>(CHUNK_SIZE));
						glm::ivec3 local_voxel_pos = world_pos - chunk_coord * CHUNK_SIZE;

						ChunkArray* chunk = model_data.get_or_emplace(chunk_coord);
						if (chunk)
						{
							chunk->data[local_voxel_pos.z][local_voxel_pos.y][local_voxel_pos.x] = quantized_color;
						}
					}
				}
			}
		}
		if (has_voxels)
		{
			bounds.min += position;
			bounds.max += position;
		}
		else
		{
			bounds.min = position;
			bounds.max = position;
		}
		ogt_vox_destroy_scene(scene);
#endif
		return true;
	}
	SparseSet<glm::ivec3, ChunkArray> model_data;

	uint16_t get_voxel(const glm::vec3& p, float size) override
	{
		glm::ivec3 voxel_pos = glm::floor(p + size * 0.5f);

		glm::ivec3 chunk_coord = glm::floor(glm::vec3(voxel_pos) / static_cast<float>(CHUNK_SIZE));

		glm::ivec3 local_pos = voxel_pos - chunk_coord * CHUNK_SIZE;

		const ChunkArray* chunk = model_data.get(chunk_coord);
		if (!chunk)
			return 0;

		if (local_pos.x < 0 || local_pos.x >= CHUNK_SIZE ||
			local_pos.y < 0 || local_pos.y >= CHUNK_SIZE ||
			local_pos.z < 0 || local_pos.z >= CHUNK_SIZE)
		{
			return 0;
		}

		return chunk->data[local_pos.z][local_pos.y][local_pos.x];
	}

	void get_bounds(glm::vec3* min, glm::vec3* max) override
	{
		*min = bounds.min;
		*max = bounds.max;
	}

	bool is_destructive() override
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
struct WorldInstance
{
	structure_id structure_idx;
	glm::vec3 position;
};
class WorldEdits
{
public:
	using OctreeNode = TreeNode<8, std::vector<instance_id>>;
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

	instance_id place_structure(structure_id id, const glm::vec3& position)
	{
		aabb3d structure_aabb;
		auto* structure = get_structure(id);
		if (structure == nullptr)
			return UINT64_MAX;
		
		instance_id inst_id = m_instances.size();
		
		WorldInstance instance;
		instance.structure_idx = id;
		instance.position = position;
		m_instances.push_back(instance);

		insert_instance_to_octree(inst_id, structure, position);

		return inst_id;
	}

	uint32_t find_instances_in_region(const aabb3d& aabb, WorldInstance* out_instances, uint32_t max_instances)
	{
		OctreeNode* node = find_smallest_node<0>(aabb);
		if (node == nullptr)
			return 0;

		uint32_t write_idx = 0;
		auto& instances = node->value;
		for (uint32_t i = 0; i < instances.size(); i++)
		{
			if (write_idx >= max_instances)
				break;

			instance_id inst_id = instances[i];
			WorldInstance& instance = m_instances[inst_id];
			OctreeStructure* structure = get_structure(instance.structure_idx);
			if (structure == nullptr)
				continue;

			aabb3d bounds;
			structure->get_bounds(&bounds.min, &bounds.max);

			bounds.min += instance.position;
			bounds.max += instance.position;

			if (bounds.intersects(aabb))
			{
				out_instances[write_idx++] = instance;
			}
		}
		return write_idx;
	}

	OctreeStructure* get_structure(structure_id id)
	{
		return (id < m_structures.size()) ? m_structures[id] : nullptr;
	}

	~WorldEdits()
	{
		for (int i = 0; i < m_structures.size(); i++)
		{
			delete m_structures[i];
			m_structures[i] = nullptr;
		}
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

			for (instance_id id : node->value)
			{
				const WorldInstance& instance = m_instances[id];
				OctreeStructure* structure = get_structure(instance.structure_idx);
				aabb3d structure_bounds;
				structure->get_bounds(&structure_bounds.min, &structure_bounds.max);
				structure_bounds.min += instance.position;
				structure_bounds.max += instance.position;

				if (structure_bounds.intersects(child_bounds))
					add_node_instance(node->children[i], child_bounds, structure_bounds, id);
			}
		}
	}

	void add_node_instance(OctreeNode* node, const aabb3d& node_bounds, const aabb3d& structure_bounds, instance_id instance)
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

			item.node->value.push_back(instance);

			for (int i = 0; i < 8; i++)
			{
				if (item.node->children[i])
					stack.push({ item.node->children[i], item.bounds.octree_child(i) });
			} 
		}
	}

	void insert_instance_to_octree(instance_id instance, OctreeStructure* structure, const glm::vec3& position)
	{
		aabb3d structure_aabb;
		structure->get_bounds(&structure_aabb.min, &structure_aabb.max);
		structure_aabb.min += position;
		structure_aabb.max += position;

		glm::vec3 p = structure_aabb.center();

		aabb3d node_bounds;
		glm::ivec3 morton;
		auto* node = find_smallest_node_for_each<0>(structure_aabb, &node_bounds, &morton,
			[instance](OctreeNode* node)
			{
				node->value.push_back(instance);
			}
		);

		if (!node)
			return;


		if (node->is_leaf() && node->value.size() >= max_structures_before_split)
		{
			split_node(node, node_bounds);
		}
		else
		{
			node->value.pop_back();
			add_node_instance(node, node_bounds, structure_aabb, instance);
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
	std::vector<WorldInstance> m_instances;

	uint64_t max_split_factor;
	OctreeNode* m_root;
	aabb3d m_root_bounds;
};