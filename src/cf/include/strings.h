// strings.h on MSVC, which lacks it (the GameSpy SDK's strcasecmp); elsewhere the host's
#pragma once

#if defined(_MSC_VER)
#include <string.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include_next <strings.h>
#endif
