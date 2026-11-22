#include <Framework/Framework.hpp>

namespace Program
{
	namespace Lifecycle
	{
		/* Called once, when the program starts */
		DllFunction SDL_AppResult Startup(void **appState, const int argc, char *argv[])
		{
			Program::State *programState = (Program::State *)*appState;
			if (!programState)
			{
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Program][Lifecycle][Startup] programState is null!");
				return SDL_APP_FAILURE;
			}

			SDL_SetAppMetadata(PROGRAM_NAME, PROGRAM_VERSION, PROGRAM_IDENTIFIER);

			SDL_SetLogPriorities(SDL_LOG_PRIORITY_ERROR);
			SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);
			SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, SDL_LOG_PRIORITY_INFO);

			if (!SDL_Init(SDL_INIT_VIDEO))
			{
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Program][Lifecycle][Startup] Could not initialize SDL: %s", SDL_GetError());
				return SDL_APP_FAILURE;
			}

			if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
			{
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[Program][Lifecycle][Startup] No sound: %s", SDL_GetError());
			}

			SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE;
#ifdef SDL_PLATFORM_EMSCRIPTEN
			windowFlags |= SDL_WINDOW_FILL_DOCUMENT;
#elif defined(SDL_PLATFORM_ANDROID)
			windowFlags |= SDL_WINDOW_FULLSCREEN;
#endif

			if (!(programState->Window = SDL_CreateWindow(programState->WindowTitle, programState->Resolution.x, programState->Resolution.y, windowFlags)))
			{
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Program][Lifecycle][Startup] Could not create the window: %s", SDL_GetError());
				return SDL_APP_FAILURE;
			}

			SDL_Surface *windowIcon = Assets::LoadIcon("Icon.png");
			if (windowIcon)
			{
				SDL_SetWindowIcon(programState->Window, windowIcon);
				SDL_DestroySurface(windowIcon);
			}

			/* Browsers have no SDL_GPU: there, a note on SDL's renderer instead, with the music still playing */
			if (!Gpu::Start(programState))
			{
				Gpu::Stop(&programState->Gpu);
				programState->Renderer = SDL_CreateRenderer(programState->Window, null);

				if (!programState->Renderer)
				{
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Program][Lifecycle][Startup] Could not create the renderer: %s", SDL_GetError());
					return SDL_APP_FAILURE;
				}

				SDL_SetRenderVSync(programState->Renderer, 1);
			}

			Music::Start(&programState->Music, "Music/BlueMonday.ogg");
			SDL_Log("Up/Down: ducks x2 or /2, Left/Right: fireflies /2 or x2, D: one draw per duck, S: shadows, A: animation (static, GPU compute, CPU upload), "
			        "B: bloom, V: VSync, M: mute. Phones: tap the right half for more ducks, the left half for fewer.");
			programState->LastTicks = SDL_GetTicksNS();
			return SDL_APP_CONTINUE;
		}

		/* Called every frame */
		DllFunction SDL_AppResult Update(void *appState)
		{
			Program::State *programState = (Program::State *)appState;
			if (!programState)
			{
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Program][Lifecycle][Update] programState is null!");
				return SDL_APP_FAILURE;
			}

			const Uint64 ticks = SDL_GetTicksNS();
			const double deltaTime = (ticks - programState->LastTicks) / 1e9;
			programState->LastTicks = ticks;
			programState->Time += deltaTime;

			Music::Feed(&programState->Music);

			if (programState->Gpu.Device)
			{
				Gpu::Frame(programState);
			}
			else
			{
				/* Why SDL_GPU isn't running, wrapped to the window's width */
				SDL_Renderer *renderer = programState->Renderer;
				const char *reason = programState->GpuError[0] ? (const char *)programState->GpuError : "SDL_GPU is not available.";
				int width = 0;
				SDL_GetCurrentRenderOutputSize(renderer, &width, null);
				const float scale = width >= 1200 ? 2.0f : 1.0f;
				const int columns = SDL_max((int)(width / scale / SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) - 4, 10);
				SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
				SDL_RenderClear(renderer);
				SDL_SetRenderScale(renderer, scale, scale);
				SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

				for (int start = 0, line = 0; reason[start]; start += columns, line++)
				{
					char text[256];
					SDL_strlcpy(text, reason + start, SDL_min((size_t)columns + 1, sizeof(text)));
					SDL_RenderDebugText(renderer, 16, 16 + line * 12.0f, text);

					if ((int)SDL_strlen(reason + start) <= columns)
					{
						break;
					}
				}

				SDL_SetRenderScale(renderer, 1.0f, 1.0f);
				SDL_RenderPresent(renderer);
			}

			/* Twice a second: frames per second and the load in the title */
			programState->StatsFrames++;
			programState->StatsTime += deltaTime;

			if (programState->StatsTime >= 0.5)
			{
				const Program::GpuState *gpu = &programState->Gpu;
				const double fps = programState->StatsFrames / programState->StatsTime;
				const double triangles = programState->DuckCount * (gpu->Duck.IndexCount / 3.0) / 1000.0;
				const char *animations[Program::ANIMATION_COUNT] = {"static", "GPU compute", "CPU upload"};
				const char *title = String::Format("%u ducks (%.0fk tris) | %u fireflies | %u draws | shadows %s | bloom %s | %s | MSAA %dx | VSync %s | %.0f fps (%.2f ms)",
				                                   programState->DuckCount, triangles, gpu->ParticleCount, gpu->DrawCalls, gpu->Shadows ? "on" : "off", gpu->Bloom ? "on" : "off",
				                                   animations[gpu->AnimationMode], 1 << (int)gpu->Samples, gpu->VSync ? "on" : "off", fps, 1000.0 / fps);
				SDL_SetWindowTitle(programState->Window, title);
				SDL_Log("%s", title);
				programState->StatsFrames = 0;
				programState->StatsTime = 0;
			}

			return SDL_APP_CONTINUE;
		}

		static void ChangeDucks(Program::State *programState, bool more)
		{
			if (more && programState->DuckCount < Program::MAX_DUCKS)
			{
				programState->DuckCount *= 2;
			}
			else if (!more && programState->DuckCount > 1)
			{
				programState->DuckCount /= 2;
			}
		}

		/* Called for every event */
		DllFunction SDL_AppResult Event(void *appState, SDL_Event *event)
		{
			Program::State *programState = (Program::State *)appState;

			switch (event->type)
			{
				case SDL_EVENT_QUIT:
				{
					return SDL_APP_SUCCESS;
				}

				/* Each key switches one part of the stress test (see the list Startup logs) */
				case SDL_EVENT_KEY_DOWN:
				{
					const SDL_Scancode key = event->key.scancode;
					Program::GpuState *gpu = &programState->Gpu;

#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID)
					if (key == SDL_SCANCODE_ESCAPE)
					{
						return SDL_APP_SUCCESS;
					}
#endif

					if (key == SDL_SCANCODE_UP || key == SDL_SCANCODE_EQUALS || key == SDL_SCANCODE_KP_PLUS)
					{
						ChangeDucks(programState, true);
					}
					else if (key == SDL_SCANCODE_DOWN || key == SDL_SCANCODE_MINUS || key == SDL_SCANCODE_KP_MINUS)
					{
						ChangeDucks(programState, false);
					}
					else if (key == SDL_SCANCODE_RIGHT && gpu->ParticleCount < 262144)
					{
						gpu->ParticleCount = gpu->ParticleCount ? gpu->ParticleCount * 2 : 1024;
					}
					else if (key == SDL_SCANCODE_LEFT)
					{
						gpu->ParticleCount = gpu->ParticleCount > 1024 ? gpu->ParticleCount / 2 : 0;
					}
					else if (key == SDL_SCANCODE_D)
					{
						gpu->InstancedDraws = !gpu->InstancedDraws;
					}
					else if (key == SDL_SCANCODE_S)
					{
						gpu->Shadows = !gpu->Shadows;
					}
					else if (key == SDL_SCANCODE_A)
					{
						gpu->AnimationMode = (Program::Animation)((gpu->AnimationMode + 1) % Program::ANIMATION_COUNT);
					}
					else if (key == SDL_SCANCODE_B)
					{
						gpu->Bloom = !gpu->Bloom;
					}
					else if (key == SDL_SCANCODE_V && gpu->Device)
					{
						Gpu::ToggleVSync(programState);
					}
					else if (key == SDL_SCANCODE_M)
					{
						Music::ToggleMute(&programState->Music);
					}

					break;
				}

				/* Phones: tap the right half for twice the ducks, the left half for half */
				case SDL_EVENT_FINGER_DOWN:
				{
					ChangeDucks(programState, event->tfinger.x >= 0.5f);
					break;
				}

				/* Debug only: Program.dll is about to be replaced. Release what lives outside Program::State */
				case FRAMEWORK_EVENT_UNLOAD:
				{
					break;
				}

				/* Debug only: the new Program.dll was loaded. Recreate what FRAMEWORK_EVENT_UNLOAD released */
				case FRAMEWORK_EVENT_RELOAD:
				{
					if (programState->Gpu.Device)
					{
						Gpu::CreatePipelines(&programState->Gpu);
					}

					break;
				}

				default:
				{
					break;
				}
			}

			return SDL_APP_CONTINUE;
		}

		/* Called once, when the program ends */
		DllFunction void Shutdown(void *appState, const SDL_AppResult result)
		{
			Program::State *programState = (Program::State *)appState;
			if (!programState)
			{
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Program][Lifecycle][Shutdown] programState is null!");
				return;
			}

			Music::Stop(&programState->Music);
			Gpu::Stop(&programState->Gpu);

			if (programState->Renderer)
			{
				SDL_DestroyRenderer(programState->Renderer);
			}

			SDL_DestroyWindow(programState->Window);
		}
	}
}
