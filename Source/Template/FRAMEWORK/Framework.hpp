#pragma once

#include <Framework/Precompiled.hpp>

#ifdef FRAMEWORK_UNIT_DECLARATIONS
	#include <Framework/Includes/Types.hpp>
#else
	#include <Framework/Includes/ForwardDeclarations.hpp>
#endif

#define FRAMEWORK_EVENT_UNLOAD (SDL_EVENT_USER + 0)
#define FRAMEWORK_EVENT_RELOAD (SDL_EVENT_USER + 1)
#define FRAMEWORK_EVENT_COUNT  2

#if defined(IS_DEBUG) && !defined(FRAMEWORK_HOST)
	extern "C" __declspec(dllexport) __declspec(selectany) const unsigned int ProgramStateSize = sizeof(Program::State);
	extern "C" __declspec(dllexport) __declspec(selectany) const unsigned int ProgramStateSignature = FRAMEWORK_STATE_SIGNATURE;
#endif
