#pragma once

#include <vulkan/vulkan.h>
#include <thalamus/image_node.hpp>
#include <memory>

namespace thalamus {

class ImageViewer {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  explicit ImageViewer(NodeGraph*);
  ~ImageViewer();
  ImageViewer(const ImageViewer&) = delete;
  ImageViewer& operator=(const ImageViewer&) = delete;
  void poll_events();
  void update(ImageNode* node);
};

} // namespace thalamus
