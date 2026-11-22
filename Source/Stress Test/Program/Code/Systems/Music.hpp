#pragma once

#include <Framework/Framework.hpp>
#include <stb/stb_vorbis.c>

namespace Music
{
	/* Streams an .ogg from memory: decoded a little ahead each frame, so it needs no audio callback (safe to hot reload) */
	bool Start(Program::MusicState *music, const char *path)
	{
		size_t size = 0;
		music->File = Assets::Load(path, &size);

		if (!music->File)
		{
			return false;
		}

		int error = 0;
		stb_vorbis *decoder = stb_vorbis_open_memory((const unsigned char *)music->File, (int)size, &error, null);

		if (!decoder)
		{
			SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "[Music] Could not decode %s (stb_vorbis error %d)", path, error);
			return false;
		}

		const stb_vorbis_info info = stb_vorbis_get_info(decoder);
		music->Decoder = decoder;
		music->Channels = info.channels;
		music->SampleRate = (int)info.sample_rate;

		const SDL_AudioSpec spec = {SDL_AUDIO_S16, music->Channels, music->SampleRate};
		music->Stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, null, null);

		if (!music->Stream)
		{
			SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "[Music] No audio device: %s", SDL_GetError());
			return false;
		}

		Feed(music);
		SDL_ResumeAudioStreamDevice(music->Stream);
		return true;
	}

	/* Keeps half a second queued, starting over at the end of the file */
	void Feed(Program::MusicState *music)
	{
		if (!music->Stream || !music->Decoder)
		{
			return;
		}

		stb_vorbis *decoder = (stb_vorbis *)music->Decoder;
		const int bytesPerSecond = music->SampleRate * music->Channels * (int)sizeof(short);
		short samples[4096];

		for (int attempts = 0; SDL_GetAudioStreamQueued(music->Stream) < bytesPerSecond / 2 && attempts < 64; attempts++)
		{
			const int frames = stb_vorbis_get_samples_short_interleaved(decoder, music->Channels, samples, SDL_arraysize(samples));

			if (frames == 0)
			{
				stb_vorbis_seek_start(decoder);
				continue;
			}

			SDL_PutAudioStreamData(music->Stream, samples, frames * music->Channels * (int)sizeof(short));
		}
	}

	void ToggleMute(Program::MusicState *music)
	{
		music->Muted = !music->Muted;

		if (music->Stream)
		{
			SDL_SetAudioStreamGain(music->Stream, music->Muted ? 0.0f : 1.0f);
		}
	}

	void Stop(Program::MusicState *music)
	{
		SDL_DestroyAudioStream(music->Stream);
		stb_vorbis_close((stb_vorbis *)music->Decoder);
		SDL_free(music->File);
		*music = {};
	}
}
