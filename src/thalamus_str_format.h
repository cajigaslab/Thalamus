#pragma once

#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wdeprecated-builtins"
    #include <absl/strings/str_format.h>
  #pragma clang diagnostic pop
#else
  #include <absl/strings/str_format.h>
#endif
