#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <thalamus/vulkan_instance.hpp>
#include <thalamus/assert.hpp>
#include <cstring>
#include <mutex>
#include <vector>

namespace thalamus {

VkInstance* get_vk_instance() {
  struct Holder {
    VkInstance instance = VK_NULL_HANDLE;
    ~Holder() {
      if (instance != VK_NULL_HANDLE)
        vkDestroyInstance(instance, nullptr);
    }
  };
  static Holder holder;
  static std::once_flag flag;
  std::call_once(flag, [] {
    if (!SDL_Init(SDL_INIT_VIDEO))
      THALAMUS_ABORT("SDL_Init: %s", SDL_GetError());

    VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.apiVersion = VK_API_VERSION_1_0;

    uint32_t ext_count = 0;
    const char* const* sdl_exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);

    VkInstanceCreateInfo inst_ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    inst_ci.pApplicationInfo = &app_info;
    inst_ci.enabledExtensionCount = ext_count;
    inst_ci.ppEnabledExtensionNames = sdl_exts;

#ifndef NDEBUG
    static const char* validation_layer = "VK_LAYER_KHRONOS_validation";

    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, layers.data());

    bool found = false;
    for (const auto& l : layers) {
      if (strcmp(l.layerName, validation_layer) == 0) { found = true; break; }
    }
    if (found) {
      inst_ci.enabledLayerCount = 1;
      inst_ci.ppEnabledLayerNames = &validation_layer;
    }
#endif

    vkCreateInstance(&inst_ci, nullptr, &holder.instance);
  });
  return &holder.instance;
}

} // namespace thalamus
