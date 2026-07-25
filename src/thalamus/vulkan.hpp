#pragma once

#include <optional>
#include <cstdint>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <vulkan/vulkan.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
struct Vulkan {
  VkInstance instance;
  VkPhysicalDevice physical_device;
  VkDevice device;
  VkQueue queue;
  uint32_t queue_family_index;
  bool supports_presentation;
};

Vulkan get_vulkan(std::optional<uint32_t> device_id);
void destroy_vulkan(Vulkan);
} // namespace thalamus
