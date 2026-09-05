#pragma once
#include <iostream>
#include <fstream>

#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"


namespace vox
{
	
	enum ChunkType
	{
		ChunkType_None,
		ChunkType_MAIN,
		ChunkType_SIZE,
		ChunkType_XYZI,
		ChunkType_PACK,
		ChunkType_RGBA
	};
	static ChunkType get_chunk_type(char id[4])
	{
		if (strncmp(id, "MAIN", 4) == 0)
			return ChunkType_MAIN;

		if (strncmp(id, "SIZE", 4) == 0)
			return ChunkType_SIZE;

		if (strncmp(id, "XYZI", 4) == 0)
			return ChunkType_XYZI;

		if (strncmp(id, "RGBA", 4) == 0)
			return ChunkType_RGBA;

		if (strncmp(id, "PACK", 4) == 0)
			return ChunkType_PACK;
		return ChunkType_None;
	}

	struct ChunkHeader
	{
		char id[4];
		int bytes;
		int children_bytes;

		void print()
		{
			printf("id %u\n", get_chunk_type(id));
			printf("content :%u\n", bytes);
			printf("children:%u\n", children_bytes);
		}
	};
	struct ChunkSizeData
	{
		int32_t x;
		int32_t y;
		int32_t z;
		int32_t volume()
		{
			return x * y * z;
		}
		int32_t index(int32_t cx, int32_t cy, int32_t cz)
		{
			return cx + cy * x + cz * x * y;
		}
		void print()
		{
			printf("size: %ux%ux%u\n", x, y, z);
		}
	};
	struct Voxel
	{
		uint8_t x, y, z, i;
		void print()
		{
			printf("voxel: %u %u %u %u\n", x, y, z, i);
		}
	};

	inline constexpr char c_magic_string[5] = "VOX ";


	enum class LoadResult
	{
		success,
		file_not_found,
		invalid_format,
		read_error
	};

	inline static const uint32_t default_palette[256] = {
		0x00000000, 0xffffffff, 0xffccffff, 0xff99ffff, 0xff66ffff, 0xff33ffff, 0xff00ffff, 0xffffccff,
		0xffccccff, 0xff99ccff, 0xff66ccff, 0xff33ccff, 0xff00ccff, 0xffff99ff, 0xffcc99ff, 0xff9999ff,
		0xff6699ff, 0xff3399ff, 0xff0099ff, 0xffff66ff, 0xffcc66ff, 0xff9966ff, 0xff6666ff, 0xff3366ff,
		0xff0066ff, 0xffff33ff, 0xffcc33ff, 0xff9933ff, 0xff6633ff, 0xff3333ff, 0xff0033ff, 0xffff00ff,
		0xffcc00ff, 0xff9900ff, 0xff6600ff, 0xff3300ff, 0xff0000ff, 0xffffffcc, 0xffccffcc, 0xff99ffcc,
		0xff66ffcc, 0xff33ffcc, 0xff00ffcc, 0xffffcccc, 0xffcccccc, 0xff99cccc, 0xff66cccc, 0xff33cccc,
		0xff00cccc, 0xffff99cc, 0xffcc99cc, 0xff9999cc, 0xff6699cc, 0xff3399cc, 0xff0099cc, 0xffff66cc,
		0xffcc66cc, 0xff9966cc, 0xff6666cc, 0xff3366cc, 0xff0066cc, 0xffff33cc, 0xffcc33cc, 0xff9933cc,
		0xff6633cc, 0xff3333cc, 0xff0033cc, 0xffff00cc, 0xffcc00cc, 0xff9900cc, 0xff6600cc, 0xff3300cc,
		0xff0000cc, 0xffffff99, 0xffccff99, 0xff99ff99, 0xff66ff99, 0xff33ff99, 0xff00ff99, 0xffffcc99,
		0xffcccc99, 0xff99cc99, 0xff66cc99, 0xff33cc99, 0xff00cc99, 0xffff9999, 0xffcc9999, 0xff999999,
		0xff669999, 0xff339999, 0xff009999, 0xffff6699, 0xffcc6699, 0xff996699, 0xff666699, 0xff336699,
		0xff006699, 0xffff3399, 0xffcc3399, 0xff993399, 0xff663399, 0xff333399, 0xff003399, 0xffff0099,
		0xffcc0099, 0xff990099, 0xff660099, 0xff330099, 0xff000099, 0xffffff66, 0xffccff66, 0xff99ff66,
		0xff66ff66, 0xff33ff66, 0xff00ff66, 0xffffcc66, 0xffcccc66, 0xff99cc66, 0xff66cc66, 0xff33cc66,
		0xff00cc66, 0xffff9966, 0xffcc9966, 0xff999966, 0xff669966, 0xff339966, 0xff009966, 0xffff6666,
		0xffcc6666, 0xff996666, 0xff666666, 0xff336666, 0xff006666, 0xffff3366, 0xffcc3366, 0xff993366,
		0xff663366, 0xff333366, 0xff003366, 0xffff0066, 0xffcc0066, 0xff990066, 0xff660066, 0xff330066,
		0xff000066, 0xffffff33, 0xffccff33, 0xff99ff33, 0xff66ff33, 0xff33ff33, 0xff00ff33, 0xffffcc33,
		0xffcccc33, 0xff99cc33, 0xff66cc33, 0xff33cc33, 0xff00cc33, 0xffff9933, 0xffcc9933, 0xff999933,
		0xff669933, 0xff339933, 0xff009933, 0xffff6633, 0xffcc6633, 0xff996633, 0xff666633, 0xff336633,
		0xff006633, 0xffff3333, 0xffcc3333, 0xff993333, 0xff663333, 0xff333333, 0xff003333, 0xffff0033,
		0xffcc0033, 0xff990033, 0xff660033, 0xff330033, 0xff000033, 0xffffff00, 0xffccff00, 0xff99ff00,
		0xff66ff00, 0xff33ff00, 0xff00ff00, 0xffffcc00, 0xffcccc00, 0xff99cc00, 0xff66cc00, 0xff33cc00,
		0xff00cc00, 0xffff9900, 0xffcc9900, 0xff999900, 0xff669900, 0xff339900, 0xff009900, 0xffff6600,
		0xffcc6600, 0xff996600, 0xff666600, 0xff336600, 0xff006600, 0xffff3300, 0xffcc3300, 0xff993300,
		0xff663300, 0xff333300, 0xff003300, 0xffff0000, 0xffcc0000, 0xff990000, 0xff660000, 0xff330000,
		0xff0000ee, 0xff0000dd, 0xff0000bb, 0xff0000aa, 0xff000088, 0xff000077, 0xff000055, 0xff000044,
		0xff000022, 0xff000011, 0xff00ee00, 0xff00dd00, 0xff00bb00, 0xff00aa00, 0xff008800, 0xff007700,
		0xff005500, 0xff004400, 0xff002200, 0xff001100, 0xffee0000, 0xffdd0000, 0xffbb0000, 0xffaa0000,
		0xff880000, 0xff770000, 0xff550000, 0xff440000, 0xff220000, 0xff110000, 0xffeeeeee, 0xffdddddd,
		0xffbbbbbb, 0xffaaaaaa, 0xff888888, 0xff777777, 0xff555555, 0xff444444, 0xff222222, 0xff111111
	};

	inline static uint16_t quantize_color(uint32_t color)
	{
		uint32_t r = (color >> 0) & 0xFF;
		uint32_t g = (color >> 8) & 0xFF;
		uint32_t b = (color >> 16) & 0xFF;
		uint32_t a = (color >> 24) & 0xFF;

		uint16_t r4 = r >> 4;
		uint16_t g4 = g >> 4;
		uint16_t b4 = b >> 4;
		uint16_t a4 = a >> 4;

		return r4 | (g4 << 4) | (b4 << 8) | (a4 << 12);
	}

	static LoadResult load_vox(const std::string& filepath, std::vector<Voxel>* out_voxels, std::vector<uint32_t>* out_voxel_colors, ChunkSizeData* out_data)
	{
		std::ifstream file(filepath, std::ios::binary);

		uint32_t file_palette[256];
		bool has_custom_palette = false;

		if (!file.is_open())
			return LoadResult::file_not_found;

		char magic_string[4];
		file.read(magic_string, 4);

		if (strncmp(magic_string, c_magic_string, 4) != 0)
			return LoadResult::invalid_format;

		int version;
		file.read(reinterpret_cast<char*>(&version), sizeof(version));

		ChunkHeader main_header;
		file.read(reinterpret_cast<char*>(&main_header), sizeof(ChunkHeader));
		main_header.print();

		if (get_chunk_type(main_header.id) != ChunkType_MAIN)
			return LoadResult::read_error;
		uint32_t bytes_left = main_header.children_bytes;

		ChunkSizeData size_data;

		while (bytes_left > 0)
		{
			uint32_t bytes_read = 0;

			ChunkHeader header;
			file.read(reinterpret_cast<char*>(&header), sizeof(ChunkHeader));
			bytes_read += sizeof(ChunkHeader);

			ChunkType type = get_chunk_type(header.id);

			switch (type)
			{
			case ChunkType_SIZE:
			{
				file.read(reinterpret_cast<char*>(&size_data), sizeof(ChunkSizeData));
				bytes_read += sizeof(ChunkSizeData);

				size_data.print();
				*out_data = size_data;
				break;
			}
			case ChunkType_XYZI:
			{
				int voxel_count = header.bytes / sizeof(Voxel);
				out_voxels->resize(voxel_count);
				file.read(reinterpret_cast<char*>(out_voxels->data()), header.bytes);
				bytes_read += header.bytes;

				for (int i = 0; i < out_voxels->size(); i++)
					(*out_voxels)[i].print();
				break;
			}
			case ChunkType_RGBA:
			{
				file.read(reinterpret_cast<char*>(file_palette), sizeof(file_palette));
				bytes_read += sizeof(file_palette);
				has_custom_palette = true;
				break;
			}
			default:
				file.seekg(header.bytes, std::ios::cur);
				bytes_read += header.bytes;
				break;
			}
			bytes_left -= bytes_read;
		}

		const uint32_t* read_palette = &default_palette[0];
		if (has_custom_palette)
			read_palette = file_palette;

		out_voxel_colors->resize(out_voxels->size());
		for (int i = 0; i < out_voxels->size(); i++)
		{
			uint32_t palette_index = (*out_voxels)[i].i;
			printf("color %x\n", read_palette[palette_index]);
			uint32_t adjusted_index = 0;
			if (palette_index > 0 && palette_index <= 255)
			{
				adjusted_index = palette_index - 1;
			}

			(*out_voxel_colors)[i] = read_palette[adjusted_index];
		}

		return LoadResult::success;
	}

}