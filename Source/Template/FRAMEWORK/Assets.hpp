#pragma once

#include <SDL3/SDL.h>
#include <string.h>

#ifndef IS_DEBUG
    extern const unsigned char BakedPack[];
    extern const unsigned int BakedPackSize;
    extern const char BakedPackFile[];
    extern const unsigned int BakedPackHash;
    extern const unsigned long long BakedPackKey;
#endif

namespace Assets
{
    namespace Internal
    {
        constexpr int MaxPathLength = 1024;

        // Where the packed assets are; a platform can define FRAMEWORK_ASSETS_BASE_PATH when SDL_GetBasePath isn't it
        inline const char *BasePath()
        {
#ifdef FRAMEWORK_ASSETS_BASE_PATH
            return FRAMEWORK_ASSETS_BASE_PATH;
#else
            return SDL_GetBasePath() ? SDL_GetBasePath() : "";
#endif
        }

        inline bool IsRelative(const char *path)
        {
            return path[0] != '\0' && path[0] != '/' && path[0] != '\\' && path[1] != ':';
        }

#ifdef IS_DEBUG

        inline bool DebugPath(const char *pathWithExtension, char *output)
        {
            const char *base = SDL_GetBasePath();
            SDL_snprintf(output, MaxPathLength, "%s../../../../../Program/Assets/%s", base ? base : "", pathWithExtension);

            if (SDL_GetPathInfo(output, nullptr))
            {
                return true;
            }

            SDL_snprintf(output, MaxPathLength, "%s../../../../Cache/Assets/Debug/%s", base ? base : "", pathWithExtension);
            return SDL_GetPathInfo(output, nullptr);
        }

#else

        inline Uint64 Scramble(Uint64 x)
        {
            x += 0x9E3779B97F4A7C15ull;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            return x ^ (x >> 31);
        }

        inline bool ReadPack(Uint64 offset, void *data, Uint32 size)
        {
            bool read = BakedPackSize > 0;

            if (read)
            {
                SDL_memcpy(data, BakedPack + offset, size);
            }
            else
            {
                char path[MaxPathLength];
                SDL_snprintf(path, sizeof(path), "%s%s", BasePath(), BakedPackFile);
                SDL_IOStream *file = SDL_IOFromFile(path, "rb");
                read = file && SDL_SeekIO(file, (Sint64)offset, SDL_IO_SEEK_SET) == (Sint64)offset && SDL_ReadIO(file, data, size) == size;

                if (file)
                {
                    SDL_CloseIO(file);
                }
            }

            Uint64 block = 0;

            for (Uint64 i = offset; read && BakedPackKey && i < offset + size; i++)
            {
                block = i == offset || i % 8 == 0 ? Scramble(BakedPackKey + i / 8) : block;
                ((Uint8 *)data)[i - offset] ^= i < 12 ? 0 : (Uint8)(block >> (i % 8 * 8));
            }

            return read;
        }

        inline const Uint8 *PackIndex()
        {
            static Uint8 *index = nullptr;
            static bool loaded = false;

            if (loaded)
            {
                return index;
            }

            loaded = true;
            Uint32 header[3] = {};

            if (ReadPack(0, header, sizeof(header)) && header[0] == 0x4B434150u && (index = (Uint8 *)SDL_malloc(sizeof(header) + header[2])) && ReadPack(0, index, (Uint32)sizeof(header) + header[2]))
            {
                Uint32 hash = 2166136261u;

                for (Uint32 i = 0; i < sizeof(header) + header[2]; i++)
                {
                    hash = (hash ^ index[i]) * 16777619u;
                }

                if (hash == BakedPackHash)
                {
                    return index;
                }
            }

            SDL_free(index);
            index = nullptr;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ASSETS: %s is missing, or is from another build of the game", BakedPackSize ? "The packed assets" : BakedPackFile);
            return nullptr;
        }

        inline bool FindPacked(const char *path, Uint64 *offset, Uint32 *size)
        {
            const Uint8 *index = PackIndex();
            Uint32 header[3] = {};

            while (path[0] == '.' && (path[1] == '/' || path[1] == '\\'))
            {
                path += 2;
            }

            if (index)
            {
                SDL_memcpy(header, index, sizeof(header));
            }

            for (const Uint8 *entry = index + sizeof(header); index && entry < index + sizeof(header) + header[2];)
            {
                Uint32 fields[4];
                SDL_memcpy(fields, entry, sizeof(fields));
                const char *name = (const char *)entry + sizeof(fields);
                entry += sizeof(fields) + fields[3];
                Uint32 i = 0;

                while (i < fields[3] && path[i] && (name[i] == '\\' ? '/' : SDL_tolower(name[i])) == (path[i] == '\\' ? '/' : SDL_tolower(path[i])))
                {
                    i++;
                }

                if (i == fields[3] && !path[i])
                {
                    *offset = sizeof(header) + header[2] + (fields[0] | (Uint64)fields[1] << 32);
                    *size = fields[2];
                    return true;
                }
            }

            return false;
        }

        inline SDL_IOStream *OpenPacked(Uint64 offset, Uint32 size)
        {
            if (BakedPackSize && !BakedPackKey)
            {
                return SDL_IOFromConstMem(BakedPack + offset, size);
            }

            void *data = SDL_malloc(size);
            SDL_IOStream *stream = data && ReadPack(offset, data, size) ? SDL_IOFromMem(data, size) : nullptr;

            if (stream)
            {
                SDL_SetPointerProperty(SDL_GetIOProperties(stream), SDL_PROP_IOSTREAM_MEMORY_FREE_FUNC_POINTER, (void *)SDL_free);
            }
            else
            {
                SDL_free(data);
            }

            return stream;
        }

#endif

        inline bool Exists(const char *path)
        {
#ifdef IS_DEBUG
            char assetPath[MaxPathLength];
            return IsRelative(path) && DebugPath(path, assetPath);
#else
            Uint64 offset = 0;
            Uint32 size = 0;
            return FindPacked(path, &offset, &size);
#endif
        }

        inline bool EndsWith(const char *text, const char *ending)
        {
            const size_t textLength = SDL_strlen(text);
            const size_t endingLength = SDL_strlen(ending);
            return textLength >= endingLength && SDL_strcasecmp(text + textLength - endingLength, ending) == 0;
        }
    }

    inline SDL_IOStream *Open(const char *path)
    {
        if (!path)
        {
            SDL_SetError("Assets::Open: path is null");
            return nullptr;
        }

#ifdef IS_DEBUG
        if (Internal::IsRelative(path))
        {
            char assetPath[Internal::MaxPathLength];

            if (Internal::DebugPath(path, assetPath))
            {
                return SDL_IOFromFile(assetPath, "rb");
            }
        }
#else
        Uint64 offset = 0;
        Uint32 size = 0;

        if (Internal::FindPacked(path, &offset, &size))
        {
            return Internal::OpenPacked(offset, size);
        }
#endif

        SDL_IOStream *stream = SDL_IOFromFile(path, "rb");

        if (!stream)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ASSETS: [%s] Failed to open file: %s", path, SDL_GetError());
        }

        return stream;
    }

    inline void *Load(const char *path, size_t *dataSize)
    {
        size_t ignored = 0;

        if (!dataSize)
        {
            dataSize = &ignored;
        }

        *dataSize = 0;
        SDL_IOStream *stream = Assets::Open(path);
        return stream ? SDL_LoadFile_IO(stream, dataSize, true) : nullptr;
    }

    inline char *LoadText(const char *path)
    {
        return (char *)Load(path, nullptr);
    }

    inline SDL_Surface *LoadSurface(const char *path)
    {
        SDL_IOStream *stream = Assets::Open(path);
        return stream ? SDL_LoadSurface_IO(stream, true) : nullptr;
    }

    // An image for SDL_SetWindowIcon, enlarged by whole pixels to near 256x256: systems shrink it to the sizes they
    // show, which keeps pixel art sharp, where enlarging a small icon themselves would blur it
    inline SDL_Surface *LoadIcon(const char *path)
    {
        SDL_Surface *icon = Assets::LoadSurface(path);
        const int scale = icon ? 256 / SDL_max(icon->w, icon->h) : 0;
        SDL_Surface *larger = scale >= 2 ? SDL_ScaleSurface(icon, icon->w * scale, icon->h * scale, SDL_SCALEMODE_NEAREST) : NULL;

        if (larger)
        {
            SDL_DestroySurface(icon);
            icon = larger;
        }

        return icon;
    }

    inline SDL_Texture *LoadTexture(SDL_Renderer *renderer, const char *path)
    {
        SDL_Surface *surface = Assets::LoadSurface(path);

        if (!surface)
        {
            return nullptr;
        }

        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        return texture;
    }

    inline bool LoadWAV(const char *path, SDL_AudioSpec *spec, Uint8 **audioBuffer, Uint32 *audioLength)
    {
        SDL_IOStream *stream = Assets::Open(path);
        return stream && SDL_LoadWAV_IO(stream, true, spec, audioBuffer, audioLength);
    }

    struct ShaderFormatInfo { SDL_GPUShaderFormat Format; const char *Extension; const char *EntryPoint; };

    inline constexpr ShaderFormatInfo ShaderFormatList[] = {
        { SDL_GPU_SHADERFORMAT_DXIL, ".dxil", "main" },
        { SDL_GPU_SHADERFORMAT_DXBC, ".dxbc", "main" },
        { SDL_GPU_SHADERFORMAT_SPIRV, ".spv", "main" },
        { SDL_GPU_SHADERFORMAT_MSL, ".msl", "main0" },
        { SDL_GPU_SHADERFORMAT_METALLIB, ".metallib", "main0" },
        { SDL_GPU_SHADERFORMAT_PRIVATE, ".private", "main" },
    };

    inline constexpr SDL_GPUShaderFormat ShaderFormats = SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB | SDL_GPU_SHADERFORMAT_PRIVATE;

    namespace Internal
    {
        inline void *LoadShaderCode(SDL_GPUDevice *device, const char *path, const ShaderFormatInfo **format, size_t *size)
        {
            for (const ShaderFormatInfo &candidate : ShaderFormatList)
            {
                char compiledPath[MaxPathLength];
                SDL_snprintf(compiledPath, sizeof(compiledPath), "%s%s", path, candidate.Extension);

                if ((SDL_GetGPUShaderFormats(device) & candidate.Format) && Exists(compiledPath))
                {
                    *format = &candidate;
                    return Assets::Load(compiledPath, size);
                }
            }

            SDL_SetError("[%s] No compiled shader matches this GPU. Was its .hlsl compiled for this platform, or did it move?", path);
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "SHADER: %s", SDL_GetError());
            return nullptr;
        }
    }

    inline SDL_GPUShader *LoadShader(SDL_GPUDevice *device, const char *path, Uint32 samplerCount, Uint32 storageTextureCount, Uint32 storageBufferCount, Uint32 uniformBufferCount)
    {
        const bool vertex = Internal::EndsWith(path, ".vert");

        if (!vertex && !Internal::EndsWith(path, ".frag"))
        {
            SDL_SetError("[%s] A shader path ends with .vert or .frag (.comp is LoadComputePipeline's)", path);
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "SHADER: %s", SDL_GetError());
            return nullptr;
        }

        const ShaderFormatInfo *format = nullptr;
        size_t codeSize = 0;
        void *code = Internal::LoadShaderCode(device, path, &format, &codeSize);

        if (!code)
        {
            return nullptr;
        }

        SDL_GPUShaderCreateInfo info = {};
        info.code_size = codeSize;
        info.code = (const Uint8 *)code;
        info.entrypoint = format->EntryPoint;
        info.format = format->Format;
        info.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        info.num_samplers = samplerCount;
        info.num_storage_textures = storageTextureCount;
        info.num_storage_buffers = storageBufferCount;
        info.num_uniform_buffers = uniformBufferCount;
        SDL_GPUShader *shader = SDL_CreateGPUShader(device, &info);

        if (!shader)
        {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "SHADER: [%s] Could not create the shader: %s", path, SDL_GetError());
        }

        SDL_free(code);
        return shader;
    }

    inline SDL_GPUComputePipeline *LoadComputePipeline(SDL_GPUDevice *device, const char *path, SDL_GPUComputePipelineCreateInfo info)
    {
        const ShaderFormatInfo *format = nullptr;
        size_t codeSize = 0;
        void *code = Internal::LoadShaderCode(device, path, &format, &codeSize);

        if (!code)
        {
            return nullptr;
        }

        info.code_size = codeSize;
        info.code = (const Uint8 *)code;
        info.entrypoint = format->EntryPoint;
        info.format = format->Format;
        SDL_GPUComputePipeline *pipeline = SDL_CreateGPUComputePipeline(device, &info);

        if (!pipeline)
        {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "SHADER: [%s] Could not create the compute pipeline: %s", path, SDL_GetError());
        }

        SDL_free(code);
        return pipeline;
    }
}
