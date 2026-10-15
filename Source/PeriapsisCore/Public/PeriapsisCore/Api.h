// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

// Inside Unreal, UBT defines PERIAPSISCORE_API as DLLEXPORT/DLLIMPORT so other modules
// can call the Core across DLLs. Those two macros come from Unreal's platform headers,
// which the Core never includes, so they are defined here with the exact same text
// (identical redefinitions are legal if a UE header comes later). The standalone tests
// build a static library and get an empty PERIAPSISCORE_API.
#ifdef PERIAPSISCORE_API
	#if defined(_MSC_VER)
		#ifndef DLLEXPORT
			#define DLLEXPORT __declspec(dllexport)
		#endif
		#ifndef DLLIMPORT
			#define DLLIMPORT __declspec(dllimport)
		#endif
	#else
		#ifndef DLLEXPORT
			#define DLLEXPORT __attribute__((visibility("default")))
		#endif
		#ifndef DLLIMPORT
			#define DLLIMPORT __attribute__((visibility("default")))
		#endif
	#endif
#else
	#define PERIAPSISCORE_API
#endif
