#pragma once

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#include <NIDAQmx.h>
#pragma clang diagnostic pop
#else
#include <NIDAQmx.h>
#endif
