// Profiler zones (D-010). Compile to nothing unless built with ZARO_TRACY=ON.
#pragma once

#if defined(ZARO_TRACY)
#include <tracy/Tracy.hpp>
#define ZARO_ZONE(name) ZoneScopedN(name)
#define ZARO_FRAME_MARK() FrameMark
#else
#define ZARO_ZONE(name) ((void)0)
#define ZARO_FRAME_MARK() ((void)0)
#endif
