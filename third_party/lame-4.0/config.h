/* MicVST: hand-written config.h for building libmp3lame (LAME 4.0) without autotools, on MSVC
   (x64, ARM64) and GCC/Clang. Plain C, no assembler, no SIMD (portable; the encoder runs on the
   recording thread, never the audio thread). Same choices as LAME's configure on x86_64. */
#ifndef MICVST_LAME_CONFIG_H
#define MICVST_LAME_CONFIG_H
#define STDC_HEADERS 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDINT_H 1
#define HAVE_STRCHR 1
#define HAVE_MEMCPY 1
#define PROTOTYPES 1
#define USE_FAST_LOG 1
#define TAKEHIRO_IEEE754_HACK 0
#define LAME_LIBRARY_BUILD 1
#include <stdint.h>
typedef float       ieee754_float32_t;
typedef double      ieee754_float64_t;
typedef long double ieee854_float80_t;
#if defined(_MSC_VER)
 #pragma warning (disable : 4305 4244 4018 4267 4996 4334 4146 4100 4127 4389 4456 4457 4701 4703 4706)
#endif
#endif
