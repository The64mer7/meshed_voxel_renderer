#pragma once

#include "world.hpp"

void World::create(const WorldData& data, const OctreeClipmapGenerateSettings& settings)
{
	m_data = data;
	m_settings = settings;

	m_world_buffer.Create();
	m_world_buffer.AllocateStorage(data.world_vram, nullptr, GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
	m_world_buffer_mapping = m_world_buffer.Map();

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
	m_clipmap.s = data.world_size();
	m_last_update_pos = glm::vec3(0.f);


}

void World::update_world_mesh(const glm::vec3& player_position)
{
	static OctreeClipmap::LeavesVector::iterator it = m_clipmap.get_leaves_created().end();
	bool past_range = glm::distance(player_position, m_last_update_pos) > m_data.update_distance;
	if (past_range && it == m_clipmap.get_leaves_created().end())
	{
		m_last_update_pos = player_position;
		double t0 = glfwGetTime();
		m_clipmap.generate_by_chunks(m_settings, player_position);
		double t1 = glfwGetTime();
		LOG("chunk_gen_time = {}", t1 - t0);
		it = m_clipmap.get_leaves_created().begin();
	}

	uint32_t face_count_max = ((m_data.voxels_per_chunk_axis * m_data.voxels_per_chunk_axis * m_data.voxels_per_chunk_axis) * 6) / 2; // checkerboard worst case
	uint32_t chunk_count = 8;
	static std::vector<uint32_t> faces(face_count_max * chunk_count);
	static size_t allocated_amount = 0;
	m_clipmap.for_each_chunk_removed(
		[&](ChunkKey chunk)
		{
			DrawArraysIndirectCommand* cmd = m_chunk_draw_cmds.get(chunk.raw);
			if (!cmd)
			{
				return;
			}
			offset_t offset_bytes = (cmd->first * sizeof(ChunkMesher::voxel_face_t)) / VERTICES_PER_FACE;
			size_t size_bytes = (cmd->count * sizeof(ChunkMesher::voxel_face_t)) / VERTICES_PER_FACE;

			allocated_amount -= size_bytes;
			m_world_buffer_manager.Insert(offset_bytes, size_bytes);

			m_chunk_aabbs.remove(chunk.raw);
			m_chunk_draw_cmds.remove(chunk.raw);
		}
	);

	static std::vector<ChunkMeshResult> results(chunk_count);

	m_clipmap.for_each_chunk_created(
		[&](ChunkKey chunk)
		{
			ChunkMeshResult result ; // guaranteed to have maxFacesPerChunk count

			if (result.face_count > 0)
			{
				size_t size_bytes = result.face_count * sizeof(ChunkMesher::voxel_face_t);
				offset_t offset_bytes;
				if (m_world_buffer_manager.Allocate(size_bytes, offset_bytes))
				{

					allocated_amount += size_bytes;
					m_world_buffer.Upload(offset_bytes, size_bytes, faces.data());

					m_chunk_aabbs.insert(chunk.raw, result.shrinkwrap_aabb);

					DrawArraysIndirectCommand cmd;
					cmd.baseInstance = 0;
					cmd.first = (offset_bytes * VERTICES_PER_FACE) / sizeof(ChunkMesher::voxel_face_t);
					cmd.count = (size_bytes * VERTICES_PER_FACE) / sizeof(ChunkMesher::voxel_face_t);
					cmd.instanceCount = 1;

					m_chunk_draw_cmds.insert(chunk.raw, cmd);
				}
				else
				{
					LOG("OUT OF MEMORY");
				}
			}
		}, it, 128
	);
}

void World::update(const glm::vec3& player_position)
{
	ChunkMesherTaskData data;
	while (m_chunk_meshed_queue.TryDequeue(data))
	{
		m_chunk_aabbs.insert(data.key, data.aabb);
		m_chunk_draw_cmds.insert(data.key, data.cmd);
	}

	//auto remaining = m_tasks.tasks_counter.load();
	//if (remaining)
	//	LOG("remaining: {}", remaining);

	bool past_range = glm::distance(player_position, m_last_update_pos) > m_data.update_distance;
	if (past_range && m_tasks.all_tasks_completed())
	{
		m_last_update_pos = player_position;

		double t0 = glfwGetTime();
		m_clipmap.generate_by_chunks(m_settings, player_position);
		double t1 = glfwGetTime();

		m_clipmap.for_each_chunk_removed(
			[&](ChunkKey chunk)
			{
				DrawArraysIndirectCommand* cmd = m_chunk_draw_cmds.get(chunk.raw);
				if (!cmd)
					return;

				offset_t offset_bytes = (cmd->first * sizeof(GreedyFace)) / VERTICES_PER_FACE;
				size_t size_bytes = (cmd->count * sizeof(GreedyFace)) / VERTICES_PER_FACE;

				m_world_buffer_manager.Insert(offset_bytes, size_bytes);

				m_chunk_aabbs.remove(chunk.raw);
				m_chunk_draw_cmds.remove(chunk.raw);
			}
		);


		uint32_t batch_size = std::thread::hardware_concurrency() * 4;
		uint32_t batch_count = (m_clipmap.get_leaves_created().size() + batch_size - 1) / batch_size;
		
		if(batch_count > 0)
		{
			UpdateGreedyMeshTask task;
			task.gpu_buffer_mapping = &m_world_buffer_mapping;
			task.leaves_to_add = &m_clipmap.get_leaves_created();
			task.manager = &m_world_buffer_manager;
			task.voxels_per_axis = m_data.voxels_per_chunk_axis;
			task.world_data = &m_data;
			task.tasks_counter = &m_tasks.tasks_counter;
			task.chunk_meshed_queue = &m_chunk_meshed_queue;
			TaskGen generator;

			generator.task = task;
			generator.clipmap = &m_clipmap;
			generator.batch_size = batch_size;

			m_tasks.try_submit_batch(*m_data.thread_pool, batch_count, generator);
		}
	}
}

void World::render(const glm::vec3& world_origin, const FirstPersonCamera& camera, const glm::ivec3& camera_chunk_coord, float camera_chunk_size)
{
	if (m_chunk_draw_cmds.get_keys().size() > 0)
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
		if (false)
		for (int i = 0; i < m_chunk_aabbs.get_keys().size(); i++)
		{
			ChunkKey key = m_chunk_aabbs.get_keys()[i];
			uint32_t packed_aabb = m_chunk_aabbs.get_values()[i];
			float chunk_size = m_data.chunk_size(key.lod);
			float voxel_size = m_data.voxel_size(key.lod);
		
			glm::vec3 aabb_origin;
			aabb_origin = key.coord;
			aabb_origin *= chunk_size;
		
		
			m_sp.uniform3f("u_cube_min", aabb_origin);
			m_sp.uniform3f("u_cube_size", glm::vec3(chunk_size));
			glDrawArrays(GL_LINES, 0, 24);
		}
		m_sp.unbind();
	}
}

void World::update_settings(const OctreeClipmapGenerateSettings& settings)
{
	m_settings = settings;
}
