#pragma once
// Minimal Win32/MSVC compatibility for the reference build.
//
// NOTE: do *not* #define stricmp here.  ltbasedefs.h already declares an inline
// stricmp() for __LINUX, and a macro would rewrite that declaration's own name,
// producing an infinitely recursive function (stack overflow on the first call).
#include <strings.h>
#include <cstring>
#include <cstdint>
