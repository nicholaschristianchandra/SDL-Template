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

			programState->Renderer = SDL_CreateRenderer(programState->Window, "gpu");
			if (!programState->Renderer)
			{
				programState->Renderer = SDL_CreateRenderer(programState->Window, null);
			}

			if (!programState->Renderer)
			{
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Program][Lifecycle][Startup] Could not create the renderer: %s", SDL_GetError());
				return SDL_APP_FAILURE;
			}

			SDL_SetRenderVSync(programState->Renderer, 1);
			SDL_SetRenderLogicalPresentation(programState->Renderer, programState->Resolution.x, programState->Resolution.y, SDL_LOGICAL_PRESENTATION_LETTERBOX);

			SDL_Surface *windowIcon = Assets::LoadIcon("Icon.png");
			if (windowIcon)
			{
				SDL_SetWindowIcon(programState->Window, windowIcon);
				SDL_DestroySurface(windowIcon);
			}
			else
			{
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[Program][Lifecycle][Startup] No window icon: %s", SDL_GetError());
			}

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

			SDL_Renderer *renderer = programState->Renderer;

			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
			SDL_RenderClear(renderer);

			const char *text = programState->WindowTitle;
			const float scale = 3.0f;
			const float width = (float)(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * String::Length(text));

			SDL_SetRenderScale(renderer, scale, scale);
			SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
			SDL_RenderDebugText(renderer, (programState->Resolution.x / scale - width) / 2.0f, (programState->Resolution.y / scale - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) / 2.0f, text);
			SDL_SetRenderScale(renderer, 1.0f, 1.0f);

			SDL_RenderPresent(renderer);

			return SDL_APP_CONTINUE;
		}

		/* Called for every event */
		DllFunction SDL_AppResult Event(void *appState, SDL_Event *event)
		{
			Program::State *programState = (Program::State *)appState;
			SDL_ConvertEventToRenderCoordinates(programState->Renderer, event);

			switch (event->type)
			{
				case SDL_EVENT_QUIT:
				{
					return SDL_APP_SUCCESS;
				}

				case SDL_EVENT_KEY_DOWN:
				{
#if !defined(SDL_PLATFORM_EMSCRIPTEN) && !defined(SDL_PLATFORM_ANDROID)
					if (event->key.scancode == SDL_SCANCODE_ESCAPE)
					{
						return SDL_APP_SUCCESS;
					}
#endif

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

			SDL_DestroyRenderer(programState->Renderer);
			SDL_DestroyWindow(programState->Window);
		}
	}
}
