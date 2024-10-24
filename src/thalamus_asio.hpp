#pragma once

#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wunused-private-field"
  #pragma clang diagnostic ignored "-Wmicrosoft-cpp-macro"
  #pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
  #pragma clang diagnostic ignored "-Wunused-private-field"
  #pragma clang diagnostic ignored "-Wlanguage-extension-token"
  #pragma clang diagnostic ignored "-Wunknown-attributes"
    #include <boost/asio.hpp>
  #pragma clang diagnostic pop
#else
  #include <boost/asio.hpp>
#endif
