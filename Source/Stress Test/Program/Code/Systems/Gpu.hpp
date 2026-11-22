#pragma once

#include <Framework/Framework.hpp>

namespace Gpu
{
	/* The uniform blocks, laid out like the shaders' cbuffers (float4 rows) */
	struct LitVertexUniforms
	{
		Math::Mat4 ViewProjection;
		float Params[4];
	};

	struct LitFragmentUniforms
	{
		Math::Mat4 LightViewProjection;
		float ShadowParams[4];
		float CameraPosition[4];
		float SunDirection[4];
		float SunColor[4];
		float MoonDirection[4];
		float MoonColor[4];
		float Ambient[4];
		float Fog[4];
		float LightPositions[4][4];
		float LightColors[4][4];
		float Material[4];
	};

	struct SkyUniforms
	{
		float Right[4];
		float Up[4];
		float Forward[4];
		float SunDirection[4];
	};

	struct MarkerUniforms
	{
		Math::Mat4 ViewProjection;
		float LightPositions[4][4];
		float LightColors[4][4];
	};

	struct ParticleUniforms
	{
		Math::Mat4 ViewProjection;
		float CameraRight[4];
		float CameraUp[4];
		float Params[4];
		float LightColors[4][4];
	};

	struct PostUniforms
	{
		float Params[4];
	};

	/* What a pipeline needs, beyond its shaders' names */
	struct PipelineSpec
	{
		const char *Vertex;
		const char *Fragment;
		Uint32 VertexStorageBuffers;
		Uint32 VertexUniforms;
		Uint32 FragmentSamplers;
		Uint32 FragmentUniforms;
		bool MeshInput;
		bool DepthTest;
		bool DepthWrite;
		bool Additive;
		bool DepthBias;
		SDL_GPUCullMode Cull;
		SDL_GPUTextureFormat Color;
		SDL_GPUTextureFormat Depth;
		SDL_GPUSampleCount Samples;
	};

	constexpr Uint32 SHADOW_MAP_SIZE = 2048;
	constexpr float SHADOW_AREA = 16.0f;

	/* SDL_GPU's Release functions assert on null in some builds of SDL */
	static void ReleaseTexture(SDL_GPUDevice *device, SDL_GPUTexture *texture) { if (texture) SDL_ReleaseGPUTexture(device, texture); }
	static void ReleaseBuffer(SDL_GPUDevice *device, SDL_GPUBuffer *buffer) { if (buffer) SDL_ReleaseGPUBuffer(device, buffer); }
	static void ReleaseTransfer(SDL_GPUDevice *device, SDL_GPUTransferBuffer *transfer) { if (transfer) SDL_ReleaseGPUTransferBuffer(device, transfer); }
	static void ReleaseSampler(SDL_GPUDevice *device, SDL_GPUSampler *sampler) { if (sampler) SDL_ReleaseGPUSampler(device, sampler); }

	static SDL_GPUSampler *MakeSampler(SDL_GPUDevice *device, SDL_GPUSamplerAddressMode address, bool anisotropy, bool compare)
	{
		SDL_GPUSamplerCreateInfo info = {};
		info.min_filter = SDL_GPU_FILTER_LINEAR;
		info.mag_filter = SDL_GPU_FILTER_LINEAR;
		info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
		info.address_mode_u = address;
		info.address_mode_v = address;
		info.address_mode_w = address;
		info.enable_anisotropy = anisotropy;
		info.max_anisotropy = anisotropy ? 16.0f : 1.0f;
		info.enable_compare = compare;
		info.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
		info.max_lod = 1000;
		return SDL_CreateGPUSampler(device, &info);
	}

	static bool Supports(SDL_GPUDevice *device, SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage)
	{
		return SDL_GPUTextureSupportsFormat(device, format, SDL_GPU_TEXTURETYPE_2D, usage);
	}

	/* The device. Many phones lack some optional Vulkan features, so without them it tries again, giving up anisotropic
	   filtering (the rest aren't used here) */
	static SDL_GPUDevice *CreateDevice(Program::GpuState *gpu)
	{
		SDL_PropertiesID properties = SDL_CreateProperties();
		SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, (Assets::ShaderFormats & SDL_GPU_SHADERFORMAT_SPIRV) != 0);
		SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, (Assets::ShaderFormats & SDL_GPU_SHADERFORMAT_DXIL) != 0);
		SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXBC_BOOLEAN, (Assets::ShaderFormats & SDL_GPU_SHADERFORMAT_DXBC) != 0);
		SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, (Assets::ShaderFormats & SDL_GPU_SHADERFORMAT_MSL) != 0);
		SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_METALLIB_BOOLEAN, (Assets::ShaderFormats & SDL_GPU_SHADERFORMAT_METALLIB) != 0);
		SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_PRIVATE_BOOLEAN, (Assets::ShaderFormats & SDL_GPU_SHADERFORMAT_PRIVATE) != 0);
#ifndef IS_DEBUG
		SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, false);
#endif
		SDL_GPUDevice *device = SDL_CreateGPUDeviceWithProperties(properties);

		if (!device)
		{
			SDL_LogWarn(SDL_LOG_CATEGORY_GPU, "[Gpu] No device with every feature (%s): trying again without the optional ones", SDL_GetError());
			SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
			SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, false);
			SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
			SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, false);
			device = SDL_CreateGPUDeviceWithProperties(properties);
			gpu->Anisotropy = false;
		}

		SDL_DestroyProperties(properties);
		return device;
	}

	/* Why SDL_GPU didn't start, for the log and the screen */
	static bool Failed(Program::State *state, const char *what)
	{
		String::Set(state->GpuError, String::Format("%s: %s", what, SDL_GetError()));
		SDL_LogError(SDL_LOG_CATEGORY_GPU, "[Gpu] %s", (const char *)state->GpuError);
		return false;
	}

	/* The device with HDR rendering, the most samples (MSAA) the GPU offers up to 8, every pipeline, and the scene loaded */
	bool Start(Program::State *state)
	{
		Program::GpuState *gpu = &state->Gpu;
		gpu->Device = CreateDevice(gpu);

		if (!gpu->Device)
		{
			return Failed(state, "No SDL_GPU device");
		}

		if (!SDL_ClaimWindowForGPUDevice(gpu->Device, state->Window))
		{
			return Failed(state, "SDL_GPU could not use the window");
		}

		SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "[Gpu] Driver: %s", SDL_GetGPUDeviceDriver(gpu->Device));
		const SDL_GPUTextureUsageFlags targetUsage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
		const SDL_GPUTextureUsageFlags shadowUsage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
		gpu->ColorFormat = SDL_GetGPUSwapchainTextureFormat(gpu->Device, state->Window);
		gpu->HdrFormat = Supports(gpu->Device, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, targetUsage) ? SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		gpu->DepthFormat = Supports(gpu->Device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET) ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT : SDL_GPU_TEXTUREFORMAT_D16_UNORM;
		gpu->ShadowFormat = Supports(gpu->Device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, shadowUsage) ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT : SDL_GPU_TEXTUREFORMAT_D16_UNORM;

		const SDL_GPUSampleCount counts[3] = {SDL_GPU_SAMPLECOUNT_8, SDL_GPU_SAMPLECOUNT_4, SDL_GPU_SAMPLECOUNT_2};

		for (SDL_GPUSampleCount count : counts)
		{
			if (gpu->Samples == SDL_GPU_SAMPLECOUNT_1 && SDL_GPUTextureSupportsSampleCount(gpu->Device, gpu->HdrFormat, count) && SDL_GPUTextureSupportsSampleCount(gpu->Device, gpu->DepthFormat, count))
			{
				gpu->Samples = count;
			}
		}

		gpu->Sampler = MakeSampler(gpu->Device, SDL_GPU_SAMPLERADDRESSMODE_REPEAT, gpu->Anisotropy, false);
		gpu->ClampSampler = MakeSampler(gpu->Device, SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE, false, false);
		gpu->ShadowSampler = MakeSampler(gpu->Device, SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE, false, true);

		SDL_GPUBufferCreateInfo buffer = {};
		buffer.size = Program::MAX_DUCKS * 8 * sizeof(float);
		buffer.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
		gpu->Instances = SDL_CreateGPUBuffer(gpu->Device, &buffer);
		buffer.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
		gpu->Animated = SDL_CreateGPUBuffer(gpu->Device, &buffer);

		SDL_GPUTransferBufferCreateInfo upload = {};
		upload.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		upload.size = buffer.size;
		gpu->AnimatedUpload = SDL_CreateGPUTransferBuffer(gpu->Device, &upload);

		gpu->ShadowMap = MakeTarget(gpu->Device, gpu->ShadowFormat, shadowUsage, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, SDL_GPU_SAMPLECOUNT_1);

		if (!gpu->Sampler || !gpu->ClampSampler || !gpu->ShadowSampler || !gpu->Instances || !gpu->Animated || !gpu->AnimatedUpload || !gpu->ShadowMap)
		{
			return Failed(state, "Could not make the GPU resources");
		}

		if (!CreatePipelines(gpu))
		{
			return Failed(state, "Could not make the pipelines (shaders)");
		}

		return Scene::Load(gpu) || Failed(state, "Could not load the scene");
	}

	static SDL_GPUGraphicsPipeline *MakePipeline(Program::GpuState *gpu, const PipelineSpec &spec)
	{
		char path[64];
		SDL_snprintf(path, sizeof(path), "Shaders/%s.vert", spec.Vertex);
		SDL_GPUShader *vertex = Assets::LoadShader(gpu->Device, path, 0, 0, spec.VertexStorageBuffers, spec.VertexUniforms);
		SDL_snprintf(path, sizeof(path), "Shaders/%s.frag", spec.Fragment);
		SDL_GPUShader *fragment = Assets::LoadShader(gpu->Device, path, spec.FragmentSamplers, 0, 0, spec.FragmentUniforms);

		const SDL_GPUVertexBufferDescription buffer = {.slot = 0, .pitch = sizeof(Program::Vertex), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX};
		const SDL_GPUVertexAttribute attributes[4] = {
			{.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(Program::Vertex, Position)},
			{.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(Program::Vertex, Normal)},
			{.location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(Program::Vertex, UV)},
			{.location = 3, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = offsetof(Program::Vertex, Tangent)},
		};

		SDL_GPUColorTargetDescription color = {};
		color.format = spec.Color;

		if (spec.Additive)
		{
			color.blend_state.enable_blend = true;
			color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
			color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
			color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
			color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
			color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
			color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
		}

		SDL_GPUGraphicsPipelineCreateInfo info = {};
		info.vertex_shader = vertex;
		info.fragment_shader = fragment;
		info.vertex_input_state.vertex_buffer_descriptions = spec.MeshInput ? &buffer : null;
		info.vertex_input_state.num_vertex_buffers = spec.MeshInput ? 1 : 0;
		info.vertex_input_state.vertex_attributes = spec.MeshInput ? attributes : null;
		info.vertex_input_state.num_vertex_attributes = spec.MeshInput ? 4 : 0;
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.rasterizer_state.cull_mode = spec.Cull;
		info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
		info.rasterizer_state.enable_depth_clip = true;
		info.rasterizer_state.enable_depth_bias = spec.DepthBias;
		info.rasterizer_state.depth_bias_constant_factor = spec.DepthBias ? 1.5f : 0.0f;
		info.rasterizer_state.depth_bias_slope_factor = spec.DepthBias ? 2.0f : 0.0f;
		info.multisample_state.sample_count = spec.Samples;
		info.depth_stencil_state.enable_depth_test = spec.DepthTest;
		info.depth_stencil_state.enable_depth_write = spec.DepthWrite;
		info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
		info.target_info.color_target_descriptions = spec.Color != SDL_GPU_TEXTUREFORMAT_INVALID ? &color : null;
		info.target_info.num_color_targets = spec.Color != SDL_GPU_TEXTUREFORMAT_INVALID ? 1 : 0;
		info.target_info.depth_stencil_format = spec.Depth;
		info.target_info.has_depth_stencil_target = spec.Depth != SDL_GPU_TEXTUREFORMAT_INVALID;

		SDL_GPUGraphicsPipeline *pipeline = vertex && fragment ? SDL_CreateGPUGraphicsPipeline(gpu->Device, &info) : null;

		if (!pipeline)
		{
			SDL_LogError(SDL_LOG_CATEGORY_GPU, "[Gpu] Could not make the %s/%s pipeline: %s", spec.Vertex, spec.Fragment, SDL_GetError());
		}

		if (vertex)
		{
			SDL_ReleaseGPUShader(gpu->Device, vertex);
		}

		if (fragment)
		{
			SDL_ReleaseGPUShader(gpu->Device, fragment);
		}

		return pipeline;
	}

	/* Also run on hot reload, so shader edits show up without a restart */
	bool CreatePipelines(Program::GpuState *gpu)
	{
		ReleasePipelines(gpu);
		const SDL_GPUTextureFormat none = SDL_GPU_TEXTUREFORMAT_INVALID;
		const SDL_GPUSampleCount one = SDL_GPU_SAMPLECOUNT_1;

		/* Vertex and fragment shader, vertex storage buffers and uniforms, fragment samplers and uniforms, mesh input, depth
		   test, depth write, additive blending, depth bias, culling, color format, depth format, samples */
		gpu->SkyPipeline = MakePipeline(gpu, {"Fullscreen", "Sky", 0, 0, 0, 1, false, false, false, false, false, SDL_GPU_CULLMODE_NONE, gpu->HdrFormat, gpu->DepthFormat, gpu->Samples});
		gpu->LitPipeline = MakePipeline(gpu, {"Lit", "Lit", 1, 1, 3, 1, true, true, true, false, false, SDL_GPU_CULLMODE_BACK, gpu->HdrFormat, gpu->DepthFormat, gpu->Samples});
		gpu->MarkerPipeline = MakePipeline(gpu, {"Marker", "Marker", 0, 1, 0, 0, true, true, true, false, false, SDL_GPU_CULLMODE_BACK, gpu->HdrFormat, gpu->DepthFormat, gpu->Samples});
		gpu->ParticlePipeline = MakePipeline(gpu, {"Particle", "Particle", 0, 1, 0, 0, false, true, false, true, false, SDL_GPU_CULLMODE_NONE, gpu->HdrFormat, gpu->DepthFormat, gpu->Samples});
		gpu->ShadowPipeline = MakePipeline(gpu, {"Lit", "Shadow", 1, 1, 0, 0, true, true, true, false, true, SDL_GPU_CULLMODE_NONE, none, gpu->ShadowFormat, one});
		gpu->BrightPipeline = MakePipeline(gpu, {"Fullscreen", "Bright", 0, 0, 1, 1, false, false, false, false, false, SDL_GPU_CULLMODE_NONE, gpu->HdrFormat, none, one});
		gpu->BlurPipeline = MakePipeline(gpu, {"Fullscreen", "Blur", 0, 0, 1, 1, false, false, false, false, false, SDL_GPU_CULLMODE_NONE, gpu->HdrFormat, none, one});
		gpu->CompositePipeline = MakePipeline(gpu, {"Fullscreen", "Composite", 0, 0, 2, 1, false, false, false, false, false, SDL_GPU_CULLMODE_NONE, gpu->ColorFormat, none, one});

		SDL_GPUComputePipelineCreateInfo compute = {};
		compute.num_readonly_storage_buffers = 1;
		compute.num_readwrite_storage_buffers = 1;
		compute.num_uniform_buffers = 1;
		compute.threadcount_x = 64;
		compute.threadcount_y = 1;
		compute.threadcount_z = 1;
		gpu->AnimatePipeline = Assets::LoadComputePipeline(gpu->Device, "Shaders/Animate.comp", compute);

		return gpu->SkyPipeline && gpu->LitPipeline && gpu->MarkerPipeline && gpu->ParticlePipeline && gpu->ShadowPipeline && gpu->BrightPipeline && gpu->BlurPipeline &&
		       gpu->CompositePipeline && gpu->AnimatePipeline;
	}

	void ReleasePipelines(Program::GpuState *gpu)
	{
		SDL_GPUGraphicsPipeline **pipelines[8] = {&gpu->SkyPipeline, &gpu->LitPipeline, &gpu->MarkerPipeline, &gpu->ParticlePipeline,
		                                          &gpu->ShadowPipeline, &gpu->BrightPipeline, &gpu->BlurPipeline, &gpu->CompositePipeline};

		for (SDL_GPUGraphicsPipeline **pipeline : pipelines)
		{
			if (*pipeline)
			{
				SDL_ReleaseGPUGraphicsPipeline(gpu->Device, *pipeline);
				*pipeline = null;
			}
		}

		if (gpu->AnimatePipeline)
		{
			SDL_ReleaseGPUComputePipeline(gpu->Device, gpu->AnimatePipeline);
			gpu->AnimatePipeline = null;
		}
	}

	SDL_GPUBuffer *UploadBuffer(SDL_GPUDevice *device, SDL_GPUBufferUsageFlags usage, const void *data, Uint32 size)
	{
		SDL_GPUBufferCreateInfo info = {};
		info.usage = usage;
		info.size = size;
		SDL_GPUBuffer *buffer = SDL_CreateGPUBuffer(device, &info);
		SDL_GPUTransferBufferCreateInfo transferInfo = {};
		transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		transferInfo.size = size;
		SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
		void *mapped = transfer ? SDL_MapGPUTransferBuffer(device, transfer, false) : null;

		if (!buffer || !mapped)
		{
			ReleaseTransfer(device, transfer);
			ReleaseBuffer(device, buffer);
			return null;
		}

		SDL_memcpy(mapped, data, size);
		SDL_UnmapGPUTransferBuffer(device, transfer);

		SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
		SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
		const SDL_GPUTransferBufferLocation source = {.transfer_buffer = transfer, .offset = 0};
		const SDL_GPUBufferRegion destination = {.buffer = buffer, .offset = 0, .size = size};
		SDL_UploadToGPUBuffer(copy, &source, &destination, false);
		SDL_EndGPUCopyPass(copy);
		SDL_SubmitGPUCommandBuffer(commands);
		ReleaseTransfer(device, transfer);
		return buffer;
	}

	/* An RGBA8 texture, with its whole mipmap chain made on the GPU when asked */
	SDL_GPUTexture *UploadTexture(SDL_GPUDevice *device, const void *pixels, int pitch, int width, int height, SDL_GPUTextureFormat format, bool mipmaps)
	{
		mipmaps = mipmaps && Supports(device, format, SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
		Uint32 levels = 1;

		while (mipmaps && (width >> levels) > 0 && (height >> levels) > 0)
		{
			levels++;
		}

		SDL_GPUTextureCreateInfo info = {};
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = format;
		info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (mipmaps ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
		info.width = (Uint32)width;
		info.height = (Uint32)height;
		info.layer_count_or_depth = 1;
		info.num_levels = levels;
		SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &info);

		SDL_GPUTransferBufferCreateInfo transferInfo = {};
		transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		transferInfo.size = (Uint32)(width * height * 4);
		SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
		Uint8 *mapped = transfer ? (Uint8 *)SDL_MapGPUTransferBuffer(device, transfer, false) : null;

		if (!texture || !mapped)
		{
			ReleaseTransfer(device, transfer);
			ReleaseTexture(device, texture);
			return null;
		}

		for (int row = 0; row < height; row++)
		{
			SDL_memcpy(mapped + row * width * 4, (const Uint8 *)pixels + row * pitch, (size_t)width * 4);
		}

		SDL_UnmapGPUTransferBuffer(device, transfer);

		SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
		SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
		const SDL_GPUTextureTransferInfo source = {.transfer_buffer = transfer, .offset = 0, .pixels_per_row = (Uint32)width, .rows_per_layer = (Uint32)height};
		SDL_GPUTextureRegion destination = {};
		destination.texture = texture;
		destination.w = (Uint32)width;
		destination.h = (Uint32)height;
		destination.d = 1;
		SDL_UploadToGPUTexture(copy, &source, &destination, false);
		SDL_EndGPUCopyPass(copy);

		if (levels > 1)
		{
			SDL_GenerateMipmapsForGPUTexture(commands, texture);
		}

		SDL_SubmitGPUCommandBuffer(commands);
		ReleaseTransfer(device, transfer);
		return texture;
	}

	SDL_GPUTexture *MakeTarget(SDL_GPUDevice *device, SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, Uint32 width, Uint32 height, SDL_GPUSampleCount samples)
	{
		SDL_GPUTextureCreateInfo info = {};
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = format;
		info.usage = usage;
		info.width = width;
		info.height = height;
		info.layer_count_or_depth = 1;
		info.num_levels = 1;
		info.sample_count = samples;

		if (usage & SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
		{
			info.props = SDL_CreateProperties();
			SDL_SetFloatProperty(info.props, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_DEPTH_FLOAT, 1.0f);
		}

		SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &info);

		if (info.props)
		{
			SDL_DestroyProperties(info.props);
		}

		return texture;
	}

	/* The screen-sized targets, remade when the window's size changes */
	static bool EnsureTargets(Program::GpuState *gpu, Uint32 width, Uint32 height)
	{
		if (gpu->TargetWidth == width && gpu->TargetHeight == height && gpu->SceneResolve)
		{
			return true;
		}

		ReleaseTargets(gpu);
		const SDL_GPUTextureUsageFlags sampled = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
		const Uint32 halfWidth = SDL_max(width / 2, 1u);
		const Uint32 halfHeight = SDL_max(height / 2, 1u);
		gpu->DepthTarget = MakeTarget(gpu->Device, gpu->DepthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, width, height, gpu->Samples);
		gpu->SceneResolve = MakeTarget(gpu->Device, gpu->HdrFormat, sampled, width, height, SDL_GPU_SAMPLECOUNT_1);
		gpu->SceneColor = gpu->Samples != SDL_GPU_SAMPLECOUNT_1 ? MakeTarget(gpu->Device, gpu->HdrFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, width, height, gpu->Samples) : null;
		gpu->BloomA = MakeTarget(gpu->Device, gpu->HdrFormat, sampled, halfWidth, halfHeight, SDL_GPU_SAMPLECOUNT_1);
		gpu->BloomB = MakeTarget(gpu->Device, gpu->HdrFormat, sampled, halfWidth, halfHeight, SDL_GPU_SAMPLECOUNT_1);
		gpu->TargetWidth = width;
		gpu->TargetHeight = height;
		return gpu->DepthTarget && gpu->SceneResolve && gpu->BloomA && gpu->BloomB && (gpu->Samples == SDL_GPU_SAMPLECOUNT_1 || gpu->SceneColor);
	}

	void ReleaseTargets(Program::GpuState *gpu)
	{
		SDL_GPUTexture **targets[5] = {&gpu->SceneColor, &gpu->SceneResolve, &gpu->DepthTarget, &gpu->BloomA, &gpu->BloomB};

		for (SDL_GPUTexture **target : targets)
		{
			ReleaseTexture(gpu->Device, *target);
			*target = null;
		}

		gpu->TargetWidth = gpu->TargetHeight = 0;
	}

	void ToggleVSync(Program::State *state)
	{
		Program::GpuState *gpu = &state->Gpu;
		SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;

		if (gpu->VSync)
		{
			mode = SDL_WindowSupportsGPUPresentMode(gpu->Device, state->Window, SDL_GPU_PRESENTMODE_IMMEDIATE) ? SDL_GPU_PRESENTMODE_IMMEDIATE
			     : SDL_WindowSupportsGPUPresentMode(gpu->Device, state->Window, SDL_GPU_PRESENTMODE_MAILBOX)   ? SDL_GPU_PRESENTMODE_MAILBOX
			                                                                                                    : SDL_GPU_PRESENTMODE_VSYNC;
		}

		if (SDL_SetGPUSwapchainParameters(gpu->Device, state->Window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
		{
			gpu->VSync = mode == SDL_GPU_PRESENTMODE_VSYNC;
		}
	}

	/* The ducks' starting places, uploaded again when their number changes */
	static void UploadDucks(Program::State *state, SDL_GPUCommandBuffer *commands)
	{
		Program::GpuState *gpu = &state->Gpu;

		if (gpu->UploadedDucks == state->DuckCount)
		{
			return;
		}

		const Uint32 size = state->DuckCount * 8 * sizeof(float);
		SDL_GPUTransferBufferCreateInfo info = {};
		info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		info.size = size;
		SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(gpu->Device, &info);
		float *mapped = transfer ? (float *)SDL_MapGPUTransferBuffer(gpu->Device, transfer, false) : null;

		if (!mapped)
		{
			ReleaseTransfer(gpu->Device, transfer);
			return;
		}

		for (Uint32 i = 0; i < state->DuckCount; i++)
		{
			Scene::PlaceDuck(i, &mapped[i * 8]);
		}

		SDL_UnmapGPUTransferBuffer(gpu->Device, transfer);
		SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
		const SDL_GPUTransferBufferLocation source = {.transfer_buffer = transfer, .offset = 0};
		const SDL_GPUBufferRegion destination = {.buffer = gpu->Instances, .offset = 0, .size = size};
		SDL_UploadToGPUBuffer(copy, &source, &destination, true);
		SDL_EndGPUCopyPass(copy);
		ReleaseTransfer(gpu->Device, transfer);
		gpu->UploadedDucks = state->DuckCount;
	}

	/* This frame's duck places: a compute shader writes them, or the CPU does and uploads them */
	static void AnimateDucks(Program::State *state, SDL_GPUCommandBuffer *commands)
	{
		Program::GpuState *gpu = &state->Gpu;
		const float time = (float)state->Time;

		if (gpu->AnimationMode == Program::ANIMATION_GPU_COMPUTE)
		{
			const SDL_GPUStorageBufferReadWriteBinding output = {.buffer = gpu->Animated, .cycle = true};
			SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(commands, null, 0, &output, 1);
			const PostUniforms uniforms = {{time, (float)gpu->UploadedDucks, 0, 0}};
			SDL_BindGPUComputePipeline(pass, gpu->AnimatePipeline);
			SDL_BindGPUComputeStorageBuffers(pass, 0, &gpu->Instances, 1);
			SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
			SDL_DispatchGPUCompute(pass, (gpu->UploadedDucks + 63) / 64, 1, 1);
			SDL_EndGPUComputePass(pass);
		}
		else if (gpu->AnimationMode == Program::ANIMATION_CPU_UPLOAD)
		{
			float *mapped = (float *)SDL_MapGPUTransferBuffer(gpu->Device, gpu->AnimatedUpload, true);

			if (!mapped)
			{
				return;
			}

			for (Uint32 i = 0; i < gpu->UploadedDucks; i++)
			{
				float start[8];
				Scene::PlaceDuck(i, start);
				Scene::AnimateDuck(start, time, &mapped[i * 8]);
			}

			SDL_UnmapGPUTransferBuffer(gpu->Device, gpu->AnimatedUpload);
			SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
			const SDL_GPUTransferBufferLocation source = {.transfer_buffer = gpu->AnimatedUpload, .offset = 0};
			const SDL_GPUBufferRegion destination = {.buffer = gpu->Animated, .offset = 0, .size = gpu->UploadedDucks * 8 * (Uint32)sizeof(float)};
			SDL_UploadToGPUBuffer(copy, &source, &destination, true);
			SDL_EndGPUCopyPass(copy);
		}
	}

	static void Copy4(float *destination, Math::Vec3 v, float w)
	{
		destination[0] = v.x;
		destination[1] = v.y;
		destination[2] = v.z;
		destination[3] = w;
	}

	static void BindMesh(SDL_GPURenderPass *pass, const Program::Mesh &mesh)
	{
		const SDL_GPUBufferBinding vertices = {.buffer = mesh.Vertices, .offset = 0};
		const SDL_GPUBufferBinding indices = {.buffer = mesh.Indices, .offset = 0};
		SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
		SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
	}

	static void BindSamplers(SDL_GPURenderPass *pass, SDL_GPUTexture *const *textures, SDL_GPUSampler *const *samplers, Uint32 count)
	{
		SDL_GPUTextureSamplerBinding bindings[4] = {};

		for (Uint32 i = 0; i < count; i++)
		{
			bindings[i] = {.texture = textures[i], .sampler = samplers[i]};
		}

		SDL_BindGPUFragmentSamplers(pass, 0, bindings, count);
	}

	/* Every duck: one instanced draw, or one draw per duck, each with its own uniforms */
	static void DrawDucks(Program::GpuState *gpu, SDL_GPUCommandBuffer *commands, SDL_GPURenderPass *pass, LitVertexUniforms uniforms)
	{
		uniforms.Params[0] = 1;
		uniforms.Params[3] = gpu->AnimationMode == Program::ANIMATION_STATIC ? 1.0f : 0.0f;
		SDL_GPUBuffer *instances = gpu->AnimationMode == Program::ANIMATION_STATIC ? gpu->Instances : gpu->Animated;
		SDL_BindGPUVertexStorageBuffers(pass, 0, &instances, 1);
		BindMesh(pass, gpu->Duck);

		if (gpu->InstancedDraws)
		{
			uniforms.Params[2] = 0;
			SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));
			SDL_DrawGPUIndexedPrimitives(pass, gpu->Duck.IndexCount, gpu->UploadedDucks, 0, 0, 0);
			gpu->DrawCalls++;
			return;
		}

		for (Uint32 i = 0; i < gpu->UploadedDucks; i++)
		{
			uniforms.Params[2] = (float)i;
			SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));
			SDL_DrawGPUIndexedPrimitives(pass, gpu->Duck.IndexCount, 1, 0, 0, 0);
		}

		gpu->DrawCalls += gpu->UploadedDucks;
	}

	/* One full-screen pass of a post-processing pipeline, from source into target */
	static void PostPass(Program::GpuState *gpu, SDL_GPUCommandBuffer *commands, SDL_GPUGraphicsPipeline *pipeline, SDL_GPUTexture *const *sources, Uint32 sourceCount,
	                     SDL_GPUTexture *target, PostUniforms uniforms)
	{
		SDL_GPUColorTargetInfo color = {};
		color.texture = target;
		color.load_op = SDL_GPU_LOADOP_DONT_CARE;
		color.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &color, 1, null);
		SDL_GPUSampler *const samplers[2] = {gpu->ClampSampler, gpu->ClampSampler};
		SDL_BindGPUGraphicsPipeline(pass, pipeline);
		BindSamplers(pass, sources, samplers, sourceCount);
		SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
		SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
		SDL_EndGPURenderPass(pass);
		gpu->DrawCalls++;
	}

	/* One frame: animation, the shadow map, the HDR scene (sky, bricks, ducks, lights, fireflies), bloom, then the screen */
	void Frame(Program::State *state)
	{
		Program::GpuState *gpu = &state->Gpu;
		SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(gpu->Device);

		if (!commands)
		{
			return;
		}

		gpu->DrawCalls = 0;
		UploadDucks(state, commands);
		AnimateDucks(state, commands);

		SDL_GPUTexture *swapchain = null;
		Uint32 width = 0;
		Uint32 height = 0;

		if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, state->Window, &swapchain, &width, &height) || !swapchain || !EnsureTargets(gpu, width, height))
		{
			SDL_SubmitGPUCommandBuffer(commands);
			return;
		}

		const float time = (float)state->Time;
		const float aspect = (float)width / (float)height;
		const float fov = 55.0f * Math::PI / 180.0f;
		const Math::Vec3 worldUp = {0, 1, 0};

		/* The camera circles counterclockwise (seen from above), slowly */
		const float cameraAngle = -time * 0.08f;
		const Math::Vec3 target = {0, 0.75f, 0};
		const Math::Vec3 eye = {SDL_cosf(cameraAngle) * 5.5f, 2.3f, SDL_sinf(cameraAngle) * 5.5f};
		const Math::Mat4 viewProjection = Math::Multiply(Math::Perspective(fov, aspect, 0.05f, 400.0f), Math::LookAt(eye, target, worldUp));
		const Math::Vec3 forward = Math::Normalize(Math::Sub(target, eye));
		const Math::Vec3 right = Math::Normalize(Math::Cross(forward, worldUp));
		const Math::Vec3 up = Math::Cross(right, forward);

		/* A day every 60 seconds, starting in the morning. The sun casts the shadows by day, the moon by night */
		const float sunAngle = time * 2 * Math::PI / 60.0f + 0.4f;
		const Math::Vec3 sun = Math::Normalize(Math::Vec3{SDL_cosf(sunAngle), SDL_sinf(sunAngle), 0.35f});
		const Math::Vec3 moon = Math::Scale(sun, -1);
		const float day = Math::SmoothStep(-0.15f, 0.25f, sun.y);
		const float sunset = Math::Clamp01(1.0f - SDL_fabsf(sun.y) * 4.0f);
		const Math::Vec3 horizon = Math::Add(Math::Lerp(Math::Vec3{0.02f, 0.03f, 0.07f}, Math::Vec3{0.62f, 0.75f, 0.90f}, day), Math::Scale(Math::Vec3{0.55f, 0.22f, 0.06f}, sunset));
		const bool moonCasts = sun.y < 0;
		const Math::Vec3 caster = moonCasts ? moon : sun;
		const Math::Vec3 casterUp = SDL_fabsf(caster.y) > 0.99f ? Math::Vec3{0, 0, 1} : worldUp;
		const Math::Mat4 lightViewProjection = Math::Multiply(Math::Orthographic(SHADOW_AREA, 1.0f, 90.0f), Math::LookAt(Math::Scale(caster, 40.0f), Math::Vec3{}, casterUp));

		/* The four lights circle clockwise (seen from above) around the duck */
		const Math::Vec3 lightColors[4] = {{1.0f, 0.12f, 0.1f}, {1.0f, 0.8f, 0.1f}, {0.1f, 1.0f, 0.2f}, {0.15f, 0.35f, 1.0f}};
		float lightPositions[4][4];
		float lightColorValues[4][4];

		for (int i = 0; i < 4; i++)
		{
			const float angle = time * 1.2f + i * Math::PI * 0.5f;
			Copy4(lightPositions[i], Math::Vec3{SDL_cosf(angle) * 1.7f, 1.1f + 0.15f * SDL_sinf(time * 2.0f + i), SDL_sinf(angle) * 1.7f}, 5.0f);
			Copy4(lightColorValues[i], lightColors[i], 3.0f);
		}

		LitVertexUniforms litVertex = {};
		litVertex.Params[1] = time;

		LitFragmentUniforms lit = {};
		lit.LightViewProjection = lightViewProjection;
		lit.ShadowParams[0] = gpu->Shadows ? 1.0f : 0.0f;
		lit.ShadowParams[1] = 1.0f / SHADOW_MAP_SIZE;
		lit.ShadowParams[2] = moonCasts ? 1.0f : 0.0f;
		Copy4(lit.CameraPosition, eye, 0);
		Copy4(lit.SunDirection, sun, 0);
		Copy4(lit.SunColor, Math::Scale(Math::Add(Math::Vec3{1.0f, 0.93f, 0.82f}, Math::Scale(Math::Vec3{0.2f, -0.25f, -0.4f}, sunset)), 2.6f * day), 0);
		Copy4(lit.MoonDirection, moon, 0);
		Copy4(lit.MoonColor, Math::Scale(Math::Vec3{0.25f, 0.32f, 0.5f}, 0.5f * (1 - day)), 0);
		Copy4(lit.Ambient, Math::Lerp(Math::Vec3{0.03f, 0.03f, 0.04f}, Math::Vec3{0.22f, 0.27f, 0.35f}, day), 0);
		Copy4(lit.Fog, horizon, 0.012f);
		SDL_memcpy(lit.LightPositions, lightPositions, sizeof(lightPositions));
		SDL_memcpy(lit.LightColors, lightColorValues, sizeof(lightColorValues));

		/* The shadow map: every duck again, seen from the sun or the moon */
		if (gpu->Shadows)
		{
			SDL_GPUDepthStencilTargetInfo depth = {};
			depth.texture = gpu->ShadowMap;
			depth.clear_depth = 1.0f;
			depth.load_op = SDL_GPU_LOADOP_CLEAR;
			depth.store_op = SDL_GPU_STOREOP_STORE;
			depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
			depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
			SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, null, 0, &depth);
			SDL_BindGPUGraphicsPipeline(pass, gpu->ShadowPipeline);
			LitVertexUniforms shadowVertex = litVertex;
			shadowVertex.ViewProjection = lightViewProjection;
			DrawDucks(gpu, commands, pass, shadowVertex);
			SDL_EndGPURenderPass(pass);
		}

		/* The scene, in HDR */
		SDL_GPUColorTargetInfo color = {};
		color.texture = gpu->Samples == SDL_GPU_SAMPLECOUNT_1 ? gpu->SceneResolve : gpu->SceneColor;
		color.load_op = SDL_GPU_LOADOP_CLEAR;
		color.store_op = gpu->Samples == SDL_GPU_SAMPLECOUNT_1 ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_RESOLVE;
		color.resolve_texture = gpu->Samples == SDL_GPU_SAMPLECOUNT_1 ? null : gpu->SceneResolve;

		SDL_GPUDepthStencilTargetInfo depth = {};
		depth.texture = gpu->DepthTarget;
		depth.clear_depth = 1.0f;
		depth.load_op = SDL_GPU_LOADOP_CLEAR;
		depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
		depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
		depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
		SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &color, 1, &depth);

		SkyUniforms sky = {};
		Copy4(sky.Right, right, SDL_tanf(fov * 0.5f) * aspect);
		Copy4(sky.Up, up, SDL_tanf(fov * 0.5f));
		Copy4(sky.Forward, forward, 0);
		Copy4(sky.SunDirection, sun, time);
		SDL_BindGPUGraphicsPipeline(pass, gpu->SkyPipeline);
		SDL_PushGPUFragmentUniformData(commands, 0, &sky, sizeof(sky));
		SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
		gpu->DrawCalls++;

		SDL_BindGPUGraphicsPipeline(pass, gpu->LitPipeline);
		SDL_BindGPUVertexStorageBuffers(pass, 0, &gpu->Instances, 1);
		litVertex.ViewProjection = viewProjection;
		lit.Material[0] = 1;
		lit.Material[1] = 0.25f;
		lit.Material[2] = 24;
		SDL_GPUTexture *const brickTextures[3] = {gpu->BrickAlbedo, gpu->BrickNormal, gpu->ShadowMap};
		SDL_GPUSampler *const litSamplers[3] = {gpu->Sampler, gpu->Sampler, gpu->ShadowSampler};
		SDL_PushGPUVertexUniformData(commands, 0, &litVertex, sizeof(litVertex));
		SDL_PushGPUFragmentUniformData(commands, 0, &lit, sizeof(lit));
		BindSamplers(pass, brickTextures, litSamplers, 3);
		BindMesh(pass, gpu->Plane);
		SDL_DrawGPUIndexedPrimitives(pass, gpu->Plane.IndexCount, 1, 0, 0, 0);
		gpu->DrawCalls++;

		lit.Material[0] = 0;
		lit.Material[1] = 0.6f;
		lit.Material[2] = 48;
		SDL_GPUTexture *const duckTextures[3] = {gpu->DuckTexture, gpu->FlatNormal, gpu->ShadowMap};
		SDL_PushGPUFragmentUniformData(commands, 0, &lit, sizeof(lit));
		BindSamplers(pass, duckTextures, litSamplers, 3);
		DrawDucks(gpu, commands, pass, litVertex);

		MarkerUniforms marker = {};
		marker.ViewProjection = viewProjection;
		SDL_memcpy(marker.LightPositions, lightPositions, sizeof(lightPositions));
		SDL_memcpy(marker.LightColors, lightColorValues, sizeof(lightColorValues));
		SDL_BindGPUGraphicsPipeline(pass, gpu->MarkerPipeline);
		SDL_PushGPUVertexUniformData(commands, 0, &marker, sizeof(marker));
		BindMesh(pass, gpu->Sphere);
		SDL_DrawGPUIndexedPrimitives(pass, gpu->Sphere.IndexCount, 4, 0, 0, 0);
		gpu->DrawCalls++;

		/* Fireflies last: additive, tested against depth but not writing it */
		if (gpu->ParticleCount > 0)
		{
			ParticleUniforms particles = {};
			particles.ViewProjection = viewProjection;
			Copy4(particles.CameraRight, right, 0);
			Copy4(particles.CameraUp, up, 0);
			particles.Params[0] = time;
			particles.Params[1] = 0.8f + 2.2f * (1 - day);
			SDL_memcpy(particles.LightColors, lightColorValues, sizeof(lightColorValues));
			SDL_BindGPUGraphicsPipeline(pass, gpu->ParticlePipeline);
			SDL_PushGPUVertexUniformData(commands, 0, &particles, sizeof(particles));
			SDL_DrawGPUPrimitives(pass, 6, gpu->ParticleCount, 0, 0);
			gpu->DrawCalls++;
		}

		SDL_EndGPURenderPass(pass);

		/* Bloom: what is brighter than white, at half size, blurred twice in each direction */
		if (gpu->Bloom)
		{
			const float texelX = 1.0f / SDL_max(width / 2, 1u);
			const float texelY = 1.0f / SDL_max(height / 2, 1u);
			PostPass(gpu, commands, gpu->BrightPipeline, &gpu->SceneResolve, 1, gpu->BloomA, PostUniforms{{1.0f, 0, 0, 0}});

			for (int round = 0; round < 2; round++)
			{
				PostPass(gpu, commands, gpu->BlurPipeline, &gpu->BloomA, 1, gpu->BloomB, PostUniforms{{texelX, 0, 0, 0}});
				PostPass(gpu, commands, gpu->BlurPipeline, &gpu->BloomB, 1, gpu->BloomA, PostUniforms{{0, texelY, 0, 0}});
			}
		}

		SDL_GPUTexture *const composite[2] = {gpu->SceneResolve, gpu->Bloom ? gpu->BloomA : gpu->SceneResolve};
		PostPass(gpu, commands, gpu->CompositePipeline, composite, 2, swapchain, PostUniforms{{gpu->Bloom ? 0.6f : 0.0f, 0, 0, 0}});
		SDL_SubmitGPUCommandBuffer(commands);
	}

	void Stop(Program::GpuState *gpu)
	{
		if (!gpu->Device)
		{
			return;
		}

		SDL_WaitForGPUIdle(gpu->Device);
		ReleasePipelines(gpu);
		ReleaseTargets(gpu);
		Program::Mesh *meshes[3] = {&gpu->Duck, &gpu->Plane, &gpu->Sphere};

		for (Program::Mesh *mesh : meshes)
		{
			ReleaseBuffer(gpu->Device, mesh->Vertices);
			ReleaseBuffer(gpu->Device, mesh->Indices);
		}

		SDL_GPUTexture *textures[5] = {gpu->DuckTexture, gpu->FlatNormal, gpu->BrickAlbedo, gpu->BrickNormal, gpu->ShadowMap};

		for (SDL_GPUTexture *texture : textures)
		{
			ReleaseTexture(gpu->Device, texture);
		}

		ReleaseBuffer(gpu->Device, gpu->Instances);
		ReleaseBuffer(gpu->Device, gpu->Animated);
		ReleaseTransfer(gpu->Device, gpu->AnimatedUpload);
		ReleaseSampler(gpu->Device, gpu->Sampler);
		ReleaseSampler(gpu->Device, gpu->ClampSampler);
		ReleaseSampler(gpu->Device, gpu->ShadowSampler);
		SDL_DestroyGPUDevice(gpu->Device);
		*gpu = {};
	}
}
