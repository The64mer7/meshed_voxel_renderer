#pragma once

#include "world.hpp"

void World::create(const WorldData& data, const OctreeClipmapGenerateSettings& settings)
{
	m_data = data;
	m_settings = settings;

	aabb3d bounds;
	bounds.min = { 0,0,0 };
	bounds.max = { data.world_size(), data.world_size(), data.world_size() };

	m_edits.init(bounds, 20, 3);

	m_world_buffer.Create();
	m_world_buffer.AllocateStorage(data.world_vram, nullptr, GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
	m_world_buffer_mapping = m_world_buffer.Map();

	m_chunk_aabbs_buffer.Create();
	m_chunk_draw_cmds_buffer.Create();

	m_world_buffer_manager.create(data.world_vram);
	m_world_buffer_manager.free(0, data.world_vram);

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
	m_clipmap.world_scale = data.world_size();
	m_last_update_pos = glm::vec3(0.f);


}

bool World::erase_chunk(const ChunkKey& key)
{
	DrawArraysIndirectCommand* cmd = m_chunk_draw_cmds.get(key.raw);
	if (!cmd)
		return false;

	offset_t offset_bytes = (cmd->first * sizeof(GreedyFace)) / VERTICES_PER_FACE;
	size_t size_bytes = (cmd->count * sizeof(GreedyFace)) / VERTICES_PER_FACE;

	m_world_buffer_manager.free(offset_bytes, size_bytes);

	m_chunk_aabbs.remove(key.raw);
	m_chunk_draw_cmds.remove(key.raw);
	
	return true;
}

void World::update(const glm::vec3& player_position, bool force)
{
	ChunkMesherTaskData data;
	while (m_chunks_to_commit.TryDequeue(data))
	{
		if (data.remesh)
			erase_chunk(data.key);

		m_chunk_aabbs.insert(data.key, data.aabb);
		m_chunk_draw_cmds.insert(data.key, data.cmd);
	}

	if (!m_tasks.all_tasks_completed(&m_chunks_to_remesh_counter))
		return;

	if (!m_tasks.all_tasks_completed(&m_chunks_to_mesh_counter)) // because we cant use m_edits while this is running
		return;

	if (!m_placed_structures.empty())
	{
		structure_id id = m_placed_structures.front();
		m_placed_structures.pop();


		OctreeStructure* structure = m_edits.get_structure(id);
		if (!structure)
			return;

		m_edits.place_structure(id, glm::vec3(0.f));

		aabb3d bounds;
		structure->get_bounds(&bounds.min, &bounds.max);

		m_chunks_to_remesh.clear();
		get_chunks_in_area(&m_chunks_to_remesh, bounds);

		submit_tasks(&m_chunks_to_remesh, true, &m_chunks_to_remesh_counter);
		
		return;
	}

	
	{
		bool past_range = glm::distance(player_position, m_last_update_pos) > m_data.update_distance;
		if (past_range)
		{
			m_last_update_pos = player_position;
			double t0 = glfwGetTime();
			m_clipmap.generate_by_chunks(m_settings, player_position);
			double t1 = glfwGetTime();

			m_clipmap.for_each_chunk_removed([this](const ChunkKey& key) {erase_chunk(key); });
			submit_tasks(&m_clipmap.get_leaves_created(), false, &m_chunks_to_mesh_counter);
		}
	};
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

void World::destroy()
{
	m_edits.cleanup();
}

structure_id World::create_structure(OctreeStructure* structure)
{
	return m_edits.create_structure(structure);
}

void World::place_structure(structure_id handle)
{
	m_placed_structures.push(handle);
}

void World::update_settings(const OctreeClipmapGenerateSettings& settings)
{
	m_settings = settings;
}
 
void World::regenerate_chunks(const glm::vec3& player_position)
{
	m_clipmap.for_each_chunk_removed([this](const ChunkKey& key) {erase_chunk(key); });
	m_clipmap.for_each_chunk([this](const ChunkKey& key) {erase_chunk(key); });
	m_clipmap.for_each_chunk_created([this](const ChunkKey& key) {erase_chunk(key); });

	submit_tasks(&m_clipmap.get_leaves(), true, &m_chunks_to_mesh_counter);
}

void World::get_loaded_chunks_in_area(std::vector<ChunkKey>* out_chunks, const aabb3d& bounds)
{
	for (size_t i = 0; i < m_chunk_aabbs.get_keys().size(); i++)
	{
		ChunkKey key = m_chunk_aabbs.get_keys()[i];
		
		aabb3d aabb;
		aabb.min = m_data.chunk_origin(key);
		aabb.max = aabb.min + m_data.chunk_size(key.lod);

		if (aabb.intersects(bounds))
			out_chunks->push_back(key);
	}
}


void World::get_chunks_in_area(std::vector<ChunkKey>* out_chunks, const aabb3d& bounds)
{
	for (size_t i = 0; i < m_clipmap.get_leaves().size(); i++)
	{
		ChunkKey key = m_clipmap.get_leaves()[i];

		aabb3d aabb;
		aabb.min = m_data.chunk_origin(key);
		aabb.max = aabb.min + m_data.chunk_size(key.lod);

		if (aabb.intersects(bounds))
			out_chunks->push_back(key);
	}
}

