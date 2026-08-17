
#include <tiny_obj_loader.h>

#include <vector>
#include <string>
#include <algorithm>
#include <stack>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <format>

#include "edit_octree.h"

struct Geometry
{
	std::vector<float> interleaved_vertices;
	size_t elements_per_vertex = 3;
};

glm::vec3 triangle_centroid(glm::vec3 a, glm::vec3 b, glm::vec3 c)
{
	return (a + b + c) / 3.f;
}

class Mesh
{
public:
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;
	void Init(std::string filepath)
	{
		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str()))
		{
			printf("loading %s failed\n", filepath.c_str());
			return;
		}

		m_Geometry.elements_per_vertex = 3;
		for (auto& shape : shapes)
		{
			size_t idx_offset = 0;
			for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++)
			{
				int fv = shape.mesh.num_face_vertices[f];
				assert(fv == 3);
				for (int v = 0; v < fv; v++)
				{
					const tinyobj::index_t& idx = shape.mesh.indices[idx_offset + v];
					m_Geometry.interleaved_vertices.push_back(attrib.vertices[3 * idx.vertex_index + 0]);
					m_Geometry.interleaved_vertices.push_back(attrib.vertices[3 * idx.vertex_index + 1]);
					m_Geometry.interleaved_vertices.push_back(attrib.vertices[3 * idx.vertex_index + 2]);
				}
				idx_offset += fv;
			}
		}
		
	}

	aabb3d GetBounds() const
	{
		return m_bounds;
	}
	const Geometry& GetGeometry() const
	{
		return m_Geometry;
	}
	Geometry& GetGeometry()
	{
		return m_Geometry;
	}
	glm::vec3 GetVertexPosition(uint64_t index) const
	{
		size_t stride = m_Geometry.elements_per_vertex;
		size_t offset = stride * index;

		if (offset + 2 < m_Geometry.interleaved_vertices.size())
			return glm::make_vec3(&m_Geometry.interleaved_vertices[offset]);
		return glm::vec3(0.f);

	}
	void GetVertexPosition(uint64_t index, glm::vec3& outPos) const
	{
		size_t stride = m_Geometry.elements_per_vertex;
		size_t offset = stride * index;

		if (offset + 2 < m_Geometry.interleaved_vertices.size())
			outPos = glm::make_vec3(&m_Geometry.interleaved_vertices[offset]);
	}

private:
	aabb3d m_bounds;
	Geometry m_Geometry;
};

struct BVHNode
{
	aabb3d bounds;
	int left = -1;
	int right = -1;

	int begin = 0;
	int count = 0;

	bool isLeaf() const
	{
		return left == -1 && right == -1;
	}
};

struct IntersectionData
{
	glm::vec3 point;
	glm::vec3 normal;
	float distance;
	float penetration;
	uint64_t triangleID;
};


struct BoundingVolumeHierarchy
{
public:
	void Create(const Mesh& mesh, uint64_t maxTrianglesPerNode)
	{
		m_MaxTrisPerNode = maxTrianglesPerNode;
		BuildNodes(mesh);
		printf("bvh: %u\n", m_Nodes.size());
	}

	void GetTriangle(const Mesh& mesh, uint64_t triangleID, glm::vec3& outV0, glm::vec3& outV1, glm::vec3& outV2)
	{
		outV0 = mesh.GetVertexPosition(3 * triangleID + 0);
		outV1 = mesh.GetVertexPosition(3 * triangleID + 1);
		outV2 = mesh.GetVertexPosition(3 * triangleID + 2);
	}

	const std::vector<BVHNode>& GetNodes()
	{
		return m_Nodes;
	}

	template<bool SkipSAT>
	bool IntersectsAABB(const Mesh& mesh, const aabb3d& aabb, uint64_t startNode = 0)
	{
		const BVHNode& node = m_Nodes[startNode];
		if (!aabb.intersects(node.bounds))
			return false;

		if (node.isLeaf())
		{
			for (uint64_t i = node.begin; i < node.begin + node.count; i++)
			{
				glm::vec3 a, b, c;
				a = mesh.GetVertexPosition(3 * m_TriangleStartIndices[i] + 0);;
				b = mesh.GetVertexPosition(3 * m_TriangleStartIndices[i] + 1);;
				c = mesh.GetVertexPosition(3 * m_TriangleStartIndices[i] + 2);;
				//GetTriangle(mesh, i, a, b, c); // probably wrong

				aabb3d triAabb;
				triAabb.extend(a);
				triAabb.extend(b);
				triAabb.extend(c);
				if (aabb.intersects(triAabb))
				{
					if constexpr (SkipSAT)
						return true;
					else
					{
						if (aabb.intersects_triangle(a, b, c))
							return true;
					}
				}
			}
		}
		else
			return IntersectsAABB<SkipSAT>(mesh, aabb, node.left) || IntersectsAABB<SkipSAT>(mesh, aabb, node.right);

		return false;
	}

	bool IntersectsAABBSlow(const Mesh& mesh, const aabb3d& aabb)
	{
		const auto& vertices = mesh.GetGeometry().interleaved_vertices;
		for (uint64_t i = 0; i + 8 < vertices.size(); i += 9)
		{
			glm::vec3 a(vertices[i + 0], vertices[i + 1], vertices[i + 2]);
			glm::vec3 b(vertices[i + 3], vertices[i + 4], vertices[i + 5]);
			glm::vec3 c(vertices[i + 6], vertices[i + 7], vertices[i + 8]);

			aabb3d triAabb;
			triAabb.min = glm::vec3(std::numeric_limits<float>::infinity());
			triAabb.max = glm::vec3(-std::numeric_limits<float>::infinity());
			triAabb.extend(a);
			triAabb.extend(b);
			triAabb.extend(c);
			if (triAabb.intersects(aabb))
			{
				if (aabb.intersects_triangle(a, b, c))
				{
					return true;
				}
			}
		}

		return false;
	}

private:
	std::vector<BVHNode> m_Nodes;
	uint64_t m_MaxTrisPerNode;
	std::vector<uint64_t> m_TriangleStartIndices;

	void BuildNodes(const Mesh& mesh)
	{
		struct BVHStackItem
		{
			BVHNode node;
			int32_t parent = -1;
			bool isLeft;
		};
		BVHStackItem root;

		root.node.begin = 0;
		root.node.count = mesh.GetGeometry().interleaved_vertices.size() / 3 / mesh.GetGeometry().elements_per_vertex;
		m_TriangleStartIndices.resize(root.node.count);
		for (uint64_t i = 0; i < m_TriangleStartIndices.capacity(); i++)
			m_TriangleStartIndices[i] = i;



		root.node.bounds = GetBounds(mesh, root.node.begin, root.node.count);
		std::stack<BVHStackItem> stack;
		stack.push(root);

		while (!stack.empty())
		{
			BVHStackItem item = stack.top(); stack.pop();
			BVHNode& node = item.node;
			uint64_t currNodeIndex = m_Nodes.size();

			if (node.isLeaf())
				node.bounds = GetBounds(mesh, node.begin, node.count);

			if (node.count > m_MaxTrisPerNode)
			{
				BVHStackItem left;
				left.isLeft = true;
				BVHStackItem right;
				right.isLeft = false;

				PartitionByAverage(mesh, node, left.node, right.node);

				left.parent = currNodeIndex;
				right.parent = currNodeIndex;

				if (right.node.count > 0)
					stack.push(right);
				if (left.node.count > 0)
					stack.push(left);
			}

			if (item.parent != -1)
			{
				if (item.isLeft)
					m_Nodes[item.parent].left = currNodeIndex;
				else
					m_Nodes[item.parent].right = currNodeIndex;
			}
			m_Nodes.push_back(node);
		}
	}

	aabb3d GetBounds(const Mesh& mesh, uint64_t begin, uint64_t count)
	{
		aabb3d aabb;

		for (uint64_t i = 0u; i < count; i++)
		{
			uint64_t offset = i + begin;
			uint64_t triangleOffset = m_TriangleStartIndices[offset];
			aabb.extend(mesh.GetVertexPosition(3 * triangleOffset + 0));
			aabb.extend(mesh.GetVertexPosition(3 * triangleOffset + 1));
			aabb.extend(mesh.GetVertexPosition(3 * triangleOffset + 2));
		}
		return aabb;
	}

	void PartitionByAverage(const Mesh& mesh, BVHNode parent, BVHNode& left, BVHNode& right)
	{
		float maxAxisSize = 0.f;
		int partitionAxis = -1;

		glm::vec3 boundsSize = parent.bounds.size();
		for (int i = 0; i < 3; i++)
		{
			float axisSize = boundsSize[i];
			if (axisSize > maxAxisSize)
			{
				maxAxisSize = axisSize;
				partitionAxis = i;
			}
		}

		std::vector<float> axisCentroids;
		for (uint64_t i = 0u; i < parent.count; i++)
		{
			uint64_t offset = parent.begin + i;
			float centroid = 0.f;
			centroid += mesh.GetVertexPosition(3 * m_TriangleStartIndices[offset] + 0)[partitionAxis];
			centroid += mesh.GetVertexPosition(3 * m_TriangleStartIndices[offset] + 1)[partitionAxis];
			centroid += mesh.GetVertexPosition(3 * m_TriangleStartIndices[offset] + 2)[partitionAxis];
			centroid /= 3;
			axisCentroids.push_back(centroid);
		}
		std::sort(axisCentroids.begin(), axisCentroids.end());
		float partitionPos = axisCentroids[axisCentroids.size() / 2];

		uint64_t leftPtr = parent.begin;
		uint64_t rightPtr = leftPtr + parent.count - 1;

		while (leftPtr <= rightPtr)
		{
			glm::vec3 a = mesh.GetVertexPosition(3 * m_TriangleStartIndices[leftPtr] + 0);
			glm::vec3 b = mesh.GetVertexPosition(3 * m_TriangleStartIndices[leftPtr] + 1);
			glm::vec3 c = mesh.GetVertexPosition(3 * m_TriangleStartIndices[leftPtr] + 2);

			glm::vec3 centroid = triangle_centroid(a, b, c);

			if (centroid[partitionAxis] < partitionPos)
				leftPtr++;
			else
			{
				std::swap(m_TriangleStartIndices[leftPtr], m_TriangleStartIndices[rightPtr]);
				if (rightPtr == 0)
					break;
				rightPtr--;
			}
		}

		left.begin = parent.begin;
		left.count = leftPtr - parent.begin;

		right.begin = leftPtr;
		right.count = parent.count - left.count;
	}


};

