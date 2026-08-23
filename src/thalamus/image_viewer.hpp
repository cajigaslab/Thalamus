#pragma once

#include <vulkan/vulkan.h>
#include <thalamus/image_node.hpp>
#include <memory>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <SDL3/SDL.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <thalamus/plugin.h>
#include <functional>

namespace thalamus {

class ImageViewer : public std::enable_shared_from_this<ImageViewer> {
  struct Impl;
  std::unique_ptr<Impl> impl;
  void handle_event(const SDL_Event& event);
  void do_poll();
public:
  explicit ImageViewer(NodeGraph*, boost::asio::io_context&, ObservableDictPtr, Node*);
  ~ImageViewer();
  ImageViewer(const ImageViewer&) = delete;
  ImageViewer& operator=(const ImageViewer&) = delete;
  static std::shared_ptr<ImageViewer> create(NodeGraph*, boost::asio::io_context&, ObservableDictPtr, Node*);
  static void setup();
  static void teardown();
  static void poll_events();
  void update(ImageNode* node);

  // Lets plugins observe raw SDL events (e.g. for windows the plugin itself
  // created via ThalamusAPI's sdl_create_window) without needing their own
  // event pump -- poll_events() is the one place SDL_PollEvent is already
  // called, so subscribers just get invoked from inside that existing loop.
  static struct THALAMUS_SDL_EventSubscription* subscribe(std::function<void(THALAMUS_SDL_Event*)> callback);
  static void unsubscribe(struct THALAMUS_SDL_EventSubscription* subscription);
};

} // namespace thalamus
