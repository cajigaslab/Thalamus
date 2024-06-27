#pragma once

#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wmicrosoft-cpp-macro"
  #pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
  #pragma clang diagnostic ignored "-Wlanguage-extension-token"
    #include <boost/signals2.hpp>
  #pragma clang diagnostic pop
#else
  #include <boost/signals2.hpp>
#endif

