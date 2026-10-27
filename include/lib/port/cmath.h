// The C++ library's float math functions on PC (the PC port). The game calls them through std::
// (std::fabsf, std::floorf, ...), as C++11 declares them in <cmath>; libstdc++ leaves them out of
// std (they are only in the global namespace there). Included by types.h in C++ on PC.
#ifndef PORT_CMATH_H
#define PORT_CMATH_H

#if TARGET_PC && defined(__cplusplus)

extern "C++" {
#include <cfloat>
#include <cmath>
}

#if defined(__GLIBCXX__)
#include <math.h>

namespace std {
using ::acosf;
using ::asinf;
using ::atan2f;
using ::atanf;
using ::ceilf;
using ::cosf;
using ::expf;
using ::fabsf;
using ::floorf;
using ::fmodf;
using ::frexpf;
using ::ldexpf;
using ::log10f;
using ::logf;
using ::modff;
using ::powf;
using ::sinf;
using ::sqrtf;
using ::tanf;
} // namespace std
#endif

#endif

#endif // PORT_CMATH_H
