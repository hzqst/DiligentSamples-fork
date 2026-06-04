#ifndef __CONFIG_H__
#define __CONFIG_H__

// Path-tracer compile-time configuration. Values mirror
// D:/RTXPT-fork/Rtxpt/Shaders/PathTracer/Config.h.
#define PATH_TRACER_MODE_REFERENCE           0
#define PATH_TRACER_MODE_BUILD_STABLE_PLANES 1
#define PATH_TRACER_MODE_FILL_STABLE_PLANES  2

#ifndef PATH_TRACER_MODE
#    define PATH_TRACER_MODE PATH_TRACER_MODE_REFERENCE
#endif

#ifndef ENABLE_DEBUG_VIZUALISATIONS
#    define ENABLE_DEBUG_VIZUALISATIONS 0
#endif

#ifndef ENABLE_DEBUG_DELTA_TREE_VIZUALISATION
#    define ENABLE_DEBUG_DELTA_TREE_VIZUALISATION 0
#endif

// ENABLE_MATERIAL_TEXTURES and MATERIAL_TEXTURE_COUNT are supplied by C++ when the
// bindless material-texture table exists.

#endif // __CONFIG_H__
