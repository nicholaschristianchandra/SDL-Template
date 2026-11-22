#pragma once

#include <Framework/Framework.hpp>

#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>

namespace Scene
{
	bool Load(Program::GpuState *gpu)
	{
		return LoadDuck(gpu, "Models/Duck.glb") && MakePlane(gpu) && MakeSphere(gpu) && MakeBricks(gpu, 1024);
	}

	/* Every triangle of the .glb in one mesh, placed by its node transforms, standing at the origin */
	bool LoadDuck(Program::GpuState *gpu, const char *path)
	{
		size_t size = 0;
		void *file = Assets::Load(path, &size);
		cgltf_options options = {};
		cgltf_data *gltf = null;

		if (!file || cgltf_parse(&options, file, size, &gltf) != cgltf_result_success || cgltf_load_buffers(&options, gltf, null) != cgltf_result_success)
		{
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Scene] Could not read %s", path);
			cgltf_free(gltf);
			SDL_free(file);
			return false;
		}

		size_t vertexTotal = 0;
		size_t indexTotal = 0;

		for (cgltf_size n = 0; n < gltf->nodes_count; n++)
		{
			for (cgltf_size p = 0; gltf->nodes[n].mesh && p < gltf->nodes[n].mesh->primitives_count; p++)
			{
				const cgltf_primitive *primitive = &gltf->nodes[n].mesh->primitives[p];
				const cgltf_accessor *positions = cgltf_find_accessor(primitive, cgltf_attribute_type_position, 0);
				vertexTotal += positions ? positions->count : 0;
				indexTotal += primitive->indices ? primitive->indices->count : 0;
			}
		}

		Program::Vertex *vertices = (Program::Vertex *)SDL_calloc(vertexTotal, sizeof(Program::Vertex));
		Uint32 *indices = (Uint32 *)SDL_calloc(indexTotal, sizeof(Uint32));
		size_t vertexCount = 0;
		size_t indexCount = 0;
		SDL_Surface *image = null;
		float low[3] = {1e9f, 1e9f, 1e9f};
		float high[3] = {-1e9f, -1e9f, -1e9f};

		for (cgltf_size n = 0; n < gltf->nodes_count; n++)
		{
			const cgltf_node *node = &gltf->nodes[n];
			float world[16];
			cgltf_node_transform_world(node, world);

			for (cgltf_size p = 0; node->mesh && p < node->mesh->primitives_count; p++)
			{
				const cgltf_primitive *primitive = &node->mesh->primitives[p];
				const cgltf_accessor *positions = cgltf_find_accessor(primitive, cgltf_attribute_type_position, 0);
				const cgltf_accessor *normals = cgltf_find_accessor(primitive, cgltf_attribute_type_normal, 0);
				const cgltf_accessor *uvs = cgltf_find_accessor(primitive, cgltf_attribute_type_texcoord, 0);

				if (primitive->type != cgltf_primitive_type_triangles || !positions || !primitive->indices)
				{
					continue;
				}

				const size_t first = vertexCount;

				for (cgltf_size v = 0; v < positions->count; v++)
				{
					float position[3] = {};
					float normal[3] = {0, 1, 0};
					Program::Vertex *vertex = &vertices[vertexCount++];
					cgltf_accessor_read_float(positions, v, position, 3);

					if (normals)
					{
						cgltf_accessor_read_float(normals, v, normal, 3);
					}

					if (uvs)
					{
						cgltf_accessor_read_float(uvs, v, vertex->UV, 2);
					}

					for (int axis = 0; axis < 3; axis++)
					{
						vertex->Position[axis] = world[axis] * position[0] + world[4 + axis] * position[1] + world[8 + axis] * position[2] + world[12 + axis];
						vertex->Normal[axis] = world[axis] * normal[0] + world[4 + axis] * normal[1] + world[8 + axis] * normal[2];
						low[axis] = SDL_min(low[axis], vertex->Position[axis]);
						high[axis] = SDL_max(high[axis], vertex->Position[axis]);
					}

					const Math::Vec3 unit = Math::Normalize(Math::Vec3{vertex->Normal[0], vertex->Normal[1], vertex->Normal[2]});
					vertex->Normal[0] = unit.x;
					vertex->Normal[1] = unit.y;
					vertex->Normal[2] = unit.z;
				}

				for (cgltf_size i = 0; i < primitive->indices->count; i++)
				{
					indices[indexCount++] = (Uint32)(first + cgltf_accessor_read_index(primitive->indices, i));
				}

				const cgltf_texture *texture = primitive->material ? primitive->material->pbr_metallic_roughness.base_color_texture.texture : null;

				if (!image && texture && texture->image && texture->image->buffer_view)
				{
					SDL_IOStream *stream = SDL_IOFromConstMem(cgltf_buffer_view_data(texture->image->buffer_view), texture->image->buffer_view->size);
					image = stream ? SDL_LoadSurface_IO(stream, true) : null;
				}
			}
		}

		/* Centered, with its lowest point on the ground */
		for (size_t v = 0; v < vertexCount; v++)
		{
			vertices[v].Position[0] -= (low[0] + high[0]) * 0.5f;
			vertices[v].Position[1] -= low[1];
			vertices[v].Position[2] -= (low[2] + high[2]) * 0.5f;
		}

		gpu->Duck.Vertices = Gpu::UploadBuffer(gpu->Device, SDL_GPU_BUFFERUSAGE_VERTEX, vertices, (Uint32)(vertexCount * sizeof(Program::Vertex)));
		gpu->Duck.Indices = Gpu::UploadBuffer(gpu->Device, SDL_GPU_BUFFERUSAGE_INDEX, indices, (Uint32)(indexCount * sizeof(Uint32)));
		gpu->Duck.IndexCount = (Uint32)indexCount;

		SDL_Surface *pixels = image ? SDL_ConvertSurface(image, SDL_PIXELFORMAT_ABGR8888) : null;
		const Uint32 white = 0xFFFFFFFF;
		gpu->DuckTexture = pixels ? Gpu::UploadTexture(gpu->Device, pixels->pixels, pixels->pitch, pixels->w, pixels->h, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, true)
		                          : Gpu::UploadTexture(gpu->Device, &white, 4, 1, 1, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, false);

		SDL_DestroySurface(pixels);
		SDL_DestroySurface(image);
		SDL_free(vertices);
		SDL_free(indices);
		cgltf_free(gltf);
		SDL_free(file);

		const Uint32 flat = 0xFFFF8080;
		gpu->FlatNormal = Gpu::UploadTexture(gpu->Device, &flat, 4, 1, 1, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false);
		return gpu->Duck.Vertices && gpu->Duck.Indices && gpu->DuckTexture && gpu->FlatNormal;
	}

	/* 200 x 200 meters, the brick texture repeating every 2.5 */
	bool MakePlane(Program::GpuState *gpu)
	{
		const float half = 100.0f;
		Program::Vertex vertices[4] = {};
		const float corners[4][2] = {{-half, -half}, {-half, half}, {half, half}, {half, -half}};

		for (int i = 0; i < 4; i++)
		{
			vertices[i] = Program::Vertex{{corners[i][0], 0, corners[i][1]}, {0, 1, 0}, {corners[i][0] * 0.4f, corners[i][1] * 0.4f}, {1, 0, 0, -1}};
		}

		const Uint32 indices[6] = {0, 1, 2, 0, 2, 3};
		gpu->Plane.Vertices = Gpu::UploadBuffer(gpu->Device, SDL_GPU_BUFFERUSAGE_VERTEX, vertices, sizeof(vertices));
		gpu->Plane.Indices = Gpu::UploadBuffer(gpu->Device, SDL_GPU_BUFFERUSAGE_INDEX, indices, sizeof(indices));
		gpu->Plane.IndexCount = 6;
		return gpu->Plane.Vertices && gpu->Plane.Indices;
	}

	/* A unit sphere, for the lights */
	bool MakeSphere(Program::GpuState *gpu)
	{
		const int rings = 12;
		const int segments = 16;
		Program::Vertex vertices[(rings + 1) * (segments + 1)] = {};
		Uint32 indices[rings * segments * 6] = {};
		int count = 0;

		for (int r = 0; r <= rings; r++)
		{
			for (int s = 0; s <= segments; s++)
			{
				const float theta = Math::PI * r / rings;
				const float phi = 2 * Math::PI * s / segments;
				const float x = SDL_sinf(theta) * SDL_cosf(phi);
				const float y = SDL_cosf(theta);
				const float z = SDL_sinf(theta) * SDL_sinf(phi);
				vertices[r * (segments + 1) + s] = Program::Vertex{{x, y, z}, {x, y, z}, {(float)s / segments, (float)r / rings}, {1, 0, 0, 1}};
			}
		}

		for (int r = 0; r < rings; r++)
		{
			for (int s = 0; s < segments; s++)
			{
				const Uint32 a = r * (segments + 1) + s;
				const Uint32 b = a + segments + 1;
				const Uint32 quad[6] = {a, a + 1, b, a + 1, b + 1, b};
				SDL_memcpy(&indices[count], quad, sizeof(quad));
				count += 6;
			}
		}

		gpu->Sphere.Vertices = Gpu::UploadBuffer(gpu->Device, SDL_GPU_BUFFERUSAGE_VERTEX, vertices, sizeof(vertices));
		gpu->Sphere.Indices = Gpu::UploadBuffer(gpu->Device, SDL_GPU_BUFFERUSAGE_INDEX, indices, sizeof(indices));
		gpu->Sphere.IndexCount = (Uint32)count;
		return gpu->Sphere.Vertices && gpu->Sphere.Indices;
	}

	/* Where duck i starts, as two float4 (x, y, z, scale), (angle, spin, phase, unused): the first in the middle, standing
	   still, the rest on a spiral beyond the camera's circle */
	void PlaceDuck(Uint32 i, float *duck)
	{
		const float angle = i * 2.39996f;
		const float radius = i ? SDL_sqrtf(64.0f + i * 0.45f) : 0.0f;
		const float values[8] = {radius * SDL_cosf(angle), 0, radius * SDL_sinf(angle), i ? 0.6f : 1.0f, i ? -angle : 0.0f, i ? 0.6f : 0.0f, SDL_fmodf(i * 0.618034f, 1.0f) * 6.2831853f, 0};
		SDL_memcpy(duck, values, sizeof(values));
	}

	/* The same animation as AnimateDuck in Shaders/Common.hlsli, on the CPU */
	void AnimateDuck(const float *start, float time, float *duck)
	{
		SDL_memcpy(duck, start, 8 * sizeof(float));

		if (start[5] > 0)
		{
			const float s = SDL_sinf(time * 0.03f);
			const float c = SDL_cosf(time * 0.03f);
			duck[0] = c * start[0] - s * start[2];
			duck[1] = SDL_fabsf(SDL_sinf(time * 5.0f + start[6])) * 0.3f * start[3];
			duck[2] = s * start[0] + c * start[2];
			duck[4] = start[4] + time * start[5];
		}
	}

	static Uint32 Hash(Uint32 x, Uint32 y, Uint32 seed)
	{
		Uint32 h = x * 374761393u + y * 668265263u + seed * 2246822519u;
		h = (h ^ (h >> 13)) * 1274126177u;
		return h ^ (h >> 16);
	}

	static float Random(Uint32 x, Uint32 y, Uint32 seed)
	{
		return (Hash(x, y, seed) & 0xFFFFFF) / 16777215.0f;
	}

	/* Smooth noise that repeats every period cells, so the texture tiles */
	static float Noise(float x, float y, int period, Uint32 seed)
	{
		const int x0 = (int)SDL_floorf(x);
		const int y0 = (int)SDL_floorf(y);
		const float fx = x - x0;
		const float fy = y - y0;
		const float sx = fx * fx * (3 - 2 * fx);
		const float sy = fy * fy * (3 - 2 * fy);
		auto corner = [&](int cx, int cy) { return Random((Uint32)((cx % period + period) % period), (Uint32)((cy % period + period) % period), seed); };
		const float top = corner(x0, y0) + (corner(x0 + 1, y0) - corner(x0, y0)) * sx;
		const float bottom = corner(x0, y0 + 1) + (corner(x0 + 1, y0 + 1) - corner(x0, y0 + 1)) * sx;
		return top + (bottom - top) * sy;
	}

	/* Procedural bricks with cracks: an albedo map and a normal map made from a height field, both tiling */
	bool MakeBricks(Program::GpuState *gpu, int size)
	{
		const int brickWidth = size / 4;
		const int brickHeight = size / 8;
		const float mortar = 6.0f;
		float *height = (float *)SDL_malloc(sizeof(float) * size * size);
		float *crack = (float *)SDL_calloc((size_t)size * size, sizeof(float));
		Uint8 *albedo = (Uint8 *)SDL_malloc((size_t)size * size * 4);
		Uint8 *normal = (Uint8 *)SDL_malloc((size_t)size * size * 4);

		/* Cracks: random walks that wander and sometimes fork */
		for (int c = 0; c < 45; c++)
		{
			float x = Random(c, 1, 7) * size;
			float y = Random(c, 2, 7) * size;
			float direction = Random(c, 3, 7) * 2 * Math::PI;
			const int length = 60 + (int)(Random(c, 4, 7) * 260);

			for (int step = 0; step < length; step++)
			{
				direction += (Random(c, step, 11) - 0.5f) * 0.45f;
				x += SDL_cosf(direction);
				y += SDL_sinf(direction);
				const float width = 2.0f * (1.0f - (float)step / length) + 0.6f;

				for (int dy = -3; dy <= 3; dy++)
				{
					for (int dx = -3; dx <= 3; dx++)
					{
						const int px = (((int)x + dx) % size + size) % size;
						const int py = (((int)y + dy) % size + size) % size;
						const float depth = Math::Clamp01(width - SDL_sqrtf((float)(dx * dx + dy * dy)) * 0.6f);
						crack[py * size + px] = SDL_max(crack[py * size + px], depth);
					}
				}
			}
		}

		for (int y = 0; y < size; y++)
		{
			const int row = y / brickHeight;
			const int shift = (row % 2) * brickWidth / 2;

			for (int x = 0; x < size; x++)
			{
				const int bx = (x + shift) % size;
				const int column = bx / brickWidth;
				const float localX = (float)(bx % brickWidth);
				const float localY = (float)(y % brickHeight);
				const float edge = SDL_min(SDL_min(localX, brickWidth - 1 - localX), SDL_min(localY, brickHeight - 1 - localY));
				const float brick = Math::SmoothStep(mortar, mortar + 8.0f, edge);
				const float grain = Noise(x / 6.0f, y / 6.0f, size / 6, 3) * 0.6f + Noise(x / 1.5f, y / 1.5f, (int)(size / 1.5f), 5) * 0.4f;
				const float lumps = Noise(x / 24.0f, y / 24.0f, size / 24, 9);
				const float cracked = crack[y * size + x] * brick;
				height[y * size + x] = brick * (0.75f + 0.15f * lumps + 0.1f * grain) + (1 - brick) * 0.15f * grain - cracked * 0.65f;

				const float tone = Random(column, row, 13);
				const float brickColor[3] = {0.50f + 0.18f * tone, 0.20f + 0.08f * tone, 0.14f + 0.05f * tone};
				const float mortarColor[3] = {0.58f, 0.56f, 0.52f};
				const float shade = (0.8f + 0.25f * grain) * (1.0f - 0.75f * cracked);
				Uint8 *pixel = &albedo[(y * size + x) * 4];

				for (int channel = 0; channel < 3; channel++)
				{
					const float value = (brickColor[channel] * brick + mortarColor[channel] * (1 - brick)) * shade;
					pixel[channel] = (Uint8)(Math::Clamp01(value) * 255);
				}

				pixel[3] = 255;
			}
		}

		/* The height field's slopes, as tangent-space normals (x along u, y along v) */
		const float strength = 5.0f;

		for (int y = 0; y < size; y++)
		{
			for (int x = 0; x < size; x++)
			{
				const float dx = height[y * size + (x + 1) % size] - height[y * size + (x + size - 1) % size];
				const float dy = height[((y + 1) % size) * size + x] - height[((y + size - 1) % size) * size + x];
				const Math::Vec3 n = Math::Normalize(Math::Vec3{-dx * strength, -dy * strength, 1});
				Uint8 *pixel = &normal[(y * size + x) * 4];
				pixel[0] = (Uint8)((n.x * 0.5f + 0.5f) * 255);
				pixel[1] = (Uint8)((n.y * 0.5f + 0.5f) * 255);
				pixel[2] = (Uint8)((n.z * 0.5f + 0.5f) * 255);
				pixel[3] = 255;
			}
		}

		gpu->BrickAlbedo = Gpu::UploadTexture(gpu->Device, albedo, size * 4, size, size, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, true);
		gpu->BrickNormal = Gpu::UploadTexture(gpu->Device, normal, size * 4, size, size, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, true);
		SDL_free(height);
		SDL_free(crack);
		SDL_free(albedo);
		SDL_free(normal);
		return gpu->BrickAlbedo && gpu->BrickNormal;
	}
}
