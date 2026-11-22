#pragma once

#include <Framework/Framework.hpp>
#include <Program/Code/Types/Math.hpp>

namespace Program
{
	constexpr Uint32 WINDOW_TITLE_MAX_LENGTH = 128;
	constexpr Uint32 MAX_DUCKS = 16384;

	/* The layout every mesh uses, matching the TEXCOORD0..3 inputs of the shaders */
	struct Vertex
	{
		float Position[3];
		float Normal[3];
		float UV[2];
		float Tangent[4];
	};

	struct Mesh
	{
		SDL_GPUBuffer *Vertices = null;
		SDL_GPUBuffer *Indices = null;
		Uint32 IndexCount = 0;
	};

	enum Animation
	{
		ANIMATION_STATIC,      /* the ducks spin in the vertex shader */
		ANIMATION_GPU_COMPUTE, /* a compute shader moves them every frame */
		ANIMATION_CPU_UPLOAD,  /* the CPU moves them and uploads every frame */
		ANIMATION_COUNT
	};

	struct GpuState
	{
		SDL_GPUDevice *Device = null;
		SDL_GPUTextureFormat ColorFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
		SDL_GPUTextureFormat HdrFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
		SDL_GPUTextureFormat DepthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
		SDL_GPUTextureFormat ShadowFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
		SDL_GPUSampleCount Samples = SDL_GPU_SAMPLECOUNT_1;
		bool Anisotropy = true;

		/* The switches: each one stresses another part of the GPU, or of the CPU feeding it */
		bool VSync = true;
		bool InstancedDraws = true;
		bool Shadows = true;
		bool Bloom = true;
		Animation AnimationMode = ANIMATION_GPU_COMPUTE;
		Uint32 ParticleCount = 4096;
		Uint32 DrawCalls = 0;

		/* Render targets: the multisampled HDR scene, its resolve, depth, two half-size bloom buffers, the shadow map */
		SDL_GPUTexture *SceneColor = null;
		SDL_GPUTexture *SceneResolve = null;
		SDL_GPUTexture *DepthTarget = null;
		SDL_GPUTexture *BloomA = null;
		SDL_GPUTexture *BloomB = null;
		SDL_GPUTexture *ShadowMap = null;
		Uint32 TargetWidth = 0;
		Uint32 TargetHeight = 0;

		SDL_GPUGraphicsPipeline *SkyPipeline = null;
		SDL_GPUGraphicsPipeline *LitPipeline = null;
		SDL_GPUGraphicsPipeline *MarkerPipeline = null;
		SDL_GPUGraphicsPipeline *ShadowPipeline = null;
		SDL_GPUGraphicsPipeline *ParticlePipeline = null;
		SDL_GPUGraphicsPipeline *BrightPipeline = null;
		SDL_GPUGraphicsPipeline *BlurPipeline = null;
		SDL_GPUGraphicsPipeline *CompositePipeline = null;
		SDL_GPUComputePipeline *AnimatePipeline = null;

		SDL_GPUSampler *Sampler = null;
		SDL_GPUSampler *ClampSampler = null;
		SDL_GPUSampler *ShadowSampler = null;

		Mesh Duck;
		Mesh Plane;
		Mesh Sphere;
		SDL_GPUTexture *DuckTexture = null;
		SDL_GPUTexture *FlatNormal = null;
		SDL_GPUTexture *BrickAlbedo = null;
		SDL_GPUTexture *BrickNormal = null;

		/* Per duck, two float4: x, y, z, scale, then angle, spin, phase */
		SDL_GPUBuffer *Instances = null;
		SDL_GPUBuffer *Animated = null;
		SDL_GPUTransferBuffer *AnimatedUpload = null;
		Uint32 UploadedDucks = 0;
	};

	struct MusicState
	{
		SDL_AudioStream *Stream = null;
		void *Decoder = null;
		void *File = null;
		int Channels = 0;
		int SampleRate = 0;
		bool Muted = false;
	};

	struct State
	{
		String(WINDOW_TITLE_MAX_LENGTH) WindowTitle = { PROGRAM_NAME };
		SDL_Point Resolution = {.x = 1280, .y = 720};
		SDL_Window *Window = null;
		SDL_Renderer *Renderer = null;
		String(256) GpuError = {};

		GpuState Gpu;
		MusicState Music;

		Uint32 DuckCount = 1;
		double Time = 0;
		Uint64 LastTicks = 0;
		double StatsTime = 0;
		Uint32 StatsFrames = 0;
	};
}
