#include "world.hpp"

#pragma once

void World::create(const WorldData& data, const OctreeClipmapGenerateSettings& settings)
{
	m_data = data;
	m_settings = settings;

	m_world_buffer.Create();
	m_world_buffer.Allocate(data.world_vram, nullptr, GL_STREAM_DRAW);

	m_chunk_aabbs_buffer.Create();
	m_chunk_draw_cmds_buffer.Create();

	m_world_buffer_manager.Init(data.world_vram);
	m_world_buffer_manager.Insert(0, data.world_vram);

	{
		int w, h, ch;
		stbi_set_flip_vertically_on_load(true);
		void* atlas_data = stbi_load("resources/textures/atlas.png", &w, &h, &ch, 4);
		if (!atlas_data)
		{
			LOG("ERROR: texture failed to load");
			return;
		}

		glCreateTextures(GL_TEXTURE_2D, 1, &texture_atlas);
		glTextureStorage2D(texture_atlas, 1, GL_RGBA8, 256, 256);
		glTextureParameteri(texture_atlas, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(texture_atlas, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(texture_atlas, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(texture_atlas, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTextureSubImage2D(texture_atlas, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, atlas_data);

		stbi_image_free(atlas_data);
	}

	glCreateVertexArrays(1, &m_dummy_vao);

	Shader vert;
	Shader frag;
	vert.createShader("shaders/default.vert", GL_VERTEX_SHADER);
	frag.createShader("shaders/default.frag", GL_FRAGMENT_SHADER);
	m_sp.createProgram(vert, frag);
	vert.cleanup();
	frag.cleanup();

	TerrainNoise::init();
	m_world_chunk_clipmap.s = data.world_size();
	m_last_update_pos = glm::vec3(0.f);

	m_mesher.create(m_data);
}

void World::update(const glm::vec3& player_position)
{
	if (glm::distance(player_position, m_last_update_pos) <= m_data.update_distance)
		return;
	m_last_update_pos = player_position;

	m_world_chunk_clipmap.GenerateByChunks(m_settings, player_position);
	uint32_t face_count_max = ((m_data.voxels_per_chunk_axis * m_data.voxels_per_chunk_axis * m_data.voxels_per_chunk_axis) * 6) / 2; // checkerboard worst case
	static std::vector<uint32_t> faces(face_count_max);
	
	static size_t allocated_amount = 0;


	static std::unordered_map<glm::ivec4, glm::uvec2> test_cache;

	m_world_chunk_clipmap.ForEachLeafRemoved(
		[&](packed_leaf3d_t leaf)
		{
			DrawArraysIndirectCommand* cmd = m_chunk_draw_cmds.get(leaf.packed);
			if (!cmd)
				return;
			offset_t offset_bytes = (cmd->first * sizeof(ChunkMesher::voxel_face_t)) / VERTICES_PER_FACE;
			size_t size_bytes = (cmd->count * sizeof(ChunkMesher::voxel_face_t)) / VERTICES_PER_FACE;

			glm::ivec2 cached = test_cache[leaf.packed];
			if (!(cached.x == offset_bytes && cached.y == size_bytes))
			{
				LOG("{} {} != {} {}", cached.x, cached.y, offset_bytes, size_bytes);
				exit(1);
			}
			test_cache.erase(leaf.packed);

			allocated_amount -= size_bytes;
			m_world_buffer_manager.Insert(offset_bytes, size_bytes);

			m_chunk_aabbs.remove(leaf.packed);
			m_chunk_draw_cmds.remove(leaf.packed);
		}
	);
	m_world_chunk_clipmap.ForEachLeafAdded(
		[&](packed_leaf3d_t leaf)
		{
			//LOG("CHUNKGEN {} {} {} {}", leaf.packed.x, leaf.packed.y, leaf.packed.z, leaf.packed.w);
			uint32_t packed_aabb;

			ChunkMeshResult result = m_mesher.mesh_3d_packed(leaf.packed, faces.data()); // guaranteed to have maxFacesPerChunk count

			if (result.face_count > 0)
			{
				size_t size_bytes = result.face_count * sizeof(ChunkMesher::voxel_face_t);
				offset_t offset_bytes;
				if (m_world_buffer_manager.Allocate(size_bytes, offset_bytes))
				{

					allocated_amount += size_bytes;
					m_world_buffer.Upload(offset_bytes, size_bytes, faces.data());
					test_cache[leaf.packed] = { offset_bytes, size_bytes };

					m_chunk_aabbs.insert(leaf.packed, result.shrinkwrap_aabb);

					DrawArraysIndirectCommand cmd;
					cmd.baseInstance = 0;
					cmd.first = (offset_bytes * VERTICES_PER_FACE) / sizeof(ChunkMesher::voxel_face_t);
					cmd.count = (size_bytes * VERTICES_PER_FACE) / sizeof(ChunkMesher::voxel_face_t);
					cmd.instanceCount = 1;

					m_chunk_draw_cmds.insert(leaf.packed, cmd);
				}
				else
				{
					LOG("OUT OF MEMORY");
				}
			}
		}
	);
	LOG("{}KB", allocated_amount/1024);
}

void World::render(const glm::vec3& world_origin, const FirstPersonCamera& camera, const glm::ivec3& camera_chunk_coord, float camera_chunk_size)
{

	m_sp.bind();
	m_sp.uniform1ui("u_offset", 0u);
	m_sp.uniformMat4("u_view_matrix", camera.GetViewMatrix());
	m_sp.uniformMat4("u_proj_matrix", camera.GetProjectionMatrix());

	glBindVertexArray(m_dummy_vao);

	m_sp.uniform1f("u_world_size", m_data.world_size());
	m_sp.uniform3f("u_world_origin", world_origin);

	m_sp.uniform1ui("u_render_cube", 0u);
	m_sp.uniform1ui("u_voxel_count", m_data.voxels_per_chunk_axis);
	m_sp.uniformTex2D("u_texture_atlas", texture_atlas, 0);
	m_sp.uniform3i("u_camera_chunk_coord", camera_chunk_coord);
	m_sp.uniform1f("u_camera_chunk_size", camera_chunk_size);

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_chunk_aabbs_buffer.Handle());
	glBufferData(GL_SHADER_STORAGE_BUFFER,
		m_chunk_draw_cmds.get_keys().size() * sizeof(ChunkKey),
		m_chunk_draw_cmds.get_keys().data(),
		GL_STREAM_DRAW);

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_chunk_draw_cmds_buffer.Handle());
	glBufferData(GL_SHADER_STORAGE_BUFFER,
		m_chunk_draw_cmds.get_values().size() * sizeof(DrawArraysIndirectCommand),
		m_chunk_draw_cmds.get_values().data(),
		GL_STREAM_DRAW);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_world_buffer.Handle());
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_chunk_aabbs_buffer.Handle());

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_chunk_draw_cmds_buffer.Handle());
	glMultiDrawArraysIndirect(GL_TRIANGLES, 0, m_chunk_draw_cmds.get_values().size(), sizeof(DrawArraysIndirectCommand));

	m_sp.uniform1ui("u_render_cube", 1u);
	for (int i = 0; i < m_chunk_aabbs.get_keys().size(); i++)
	{
		ChunkKey key = m_chunk_aabbs.get_keys()[i];
		uint32_t packed_aabb = m_chunk_aabbs.get_values()[i];
		float chunk_size = m_data.chunk_size(key.lod);
		float voxel_size = m_data.voxel_size(key.lod);


		glm::vec3 aabb_origin;
		aabb_origin = key.coord;
		aabb_origin *= chunk_size;

		glm::vec3 aabb_min;
		glm::vec3 aabb_max;

		aabb_min.x = ((packed_aabb >> 0) & BITMASK(5)) * voxel_size;
		aabb_min.y = ((packed_aabb >> 5) & BITMASK(5)) * voxel_size;
		aabb_min.z = ((packed_aabb >> 10) & BITMASK(5)) * voxel_size;
		aabb_max.x = (1 + ((packed_aabb >> 15) & BITMASK(5))) * voxel_size;
		aabb_max.y = (1 + ((packed_aabb >> 20) & BITMASK(5))) * voxel_size;
		aabb_max.z = (1 + ((packed_aabb >> 25) & BITMASK(5))) * voxel_size;

		m_sp.uniform3f("u_cube_min", aabb_origin + aabb_min);
		m_sp.uniform3f("u_cube_size", (aabb_max - aabb_min));
		glDrawArrays(GL_LINES, 0, 24);
	}
	m_sp.unbind();
}
