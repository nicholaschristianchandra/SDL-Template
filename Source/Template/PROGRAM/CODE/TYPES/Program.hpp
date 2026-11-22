#pragma once

#include <Framework/Framework.hpp>

namespace Program
{
	constexpr Uint32 WINDOW_TITLE_MAX_LENGTH = 128;

	struct State
	{
		String(WINDOW_TITLE_MAX_LENGTH) WindowTitle = { PROGRAM_NAME };
		SDL_Point Resolution = {.x = 640, .y = 480};
		SDL_Window *Window = null;
		SDL_Renderer *Renderer = null;
	};
}
