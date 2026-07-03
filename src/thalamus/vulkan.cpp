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

#include <thalamus/vulkan.hpp>
#include <thalamus/assert.hpp>
#include <thalamus/async.hpp>
#include <cstring>
#include <mutex>
#include <vector>

namespace thalamus {

Vulkan get_vulkan(std::optional<uint32_t> device_id) {
  VkSurfaceKHR surface = nullptr;
  Vulkan result{};

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    THALAMUS_LOG(error) << "SDL_Init: " << SDL_GetError();
    return result;
  }
  auto window = SDL_CreateWindow("Thalamus Vulkan Init", 800, 600, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
  if(window == nullptr) {
    THALAMUS_LOG(error) << "SDL_CreateWindow: " << SDL_GetError();
    return result;
  }
  Finally f_window([&] {
    SDL_DestroyWindow(window);
  });

  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.apiVersion = VK_API_VERSION_1_0;

  uint32_t ext_count = 0;
  auto sdl_exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
  if(sdl_exts == nullptr) {
    THALAMUS_LOG(error) << "SDL_Vulkan_GetInstanceExtensions: " << SDL_GetError();
    return result;
  }

  VkInstanceCreateInfo inst_ci{};
  inst_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  inst_ci.pApplicationInfo = &app_info;
  inst_ci.enabledExtensionCount = ext_count;
  inst_ci.ppEnabledExtensionNames = sdl_exts;

  auto validation_layer = "VK_LAYER_KHRONOS_validation";

  uint32_t layer_count = 0;
  if(vkEnumerateInstanceLayerProperties(&layer_count, nullptr) != VK_SUCCESS) {
    THALAMUS_LOG(error) << "vkEnumerateInstanceLayerProperties";
    return result;
  }
  std::vector<VkLayerProperties> layers(layer_count);
  if(vkEnumerateInstanceLayerProperties(&layer_count, layers.data()) != VK_SUCCESS) {
    THALAMUS_LOG(error) << "vkEnumerateInstanceLayerProperties";
    return result;
  }

  bool found = false;
  for (const auto& l : layers) {
    if (strcmp(l.layerName, validation_layer) == 0) { found = true; break; }
  }
  if (found) {
    THALAMUS_LOG(info) << validation_layer << " Enabled";
    inst_ci.enabledLayerCount = 1;
    inst_ci.ppEnabledLayerNames = &validation_layer;
  } else {
    THALAMUS_LOG(info) << validation_layer << " Disabled";
  }

  if(vkCreateInstance(&inst_ci, nullptr, &result.instance) != VK_SUCCESS) {
    THALAMUS_LOG(error) << "vkCreateInstance";
    return result;
  }
  THALAMUS_LOG(info) << "VkInstance Created";

  if (!SDL_Vulkan_CreateSurface(window, result.instance, nullptr, &surface)) {
    THALAMUS_LOG(error) << "SDL_Vulkan_CreateSurface: " << SDL_GetError();
    return result;
  }
  Finally f_surface([&] {
    SDL_Vulkan_DestroySurface(result.instance, surface, nullptr);
  });

  uint32_t num_physical_devices = 0;
  if(vkEnumeratePhysicalDevices(result.instance, &num_physical_devices, nullptr) != VK_SUCCESS) {
    THALAMUS_LOG(error) << "vkEnumeratePhysicalDevices";
    return result;
  }
  if(num_physical_devices == 0) {
    THALAMUS_LOG(error) << "No physical devices found";
    return result;
  }
  std::vector<VkPhysicalDevice> physical_devices(num_physical_devices);
  if(vkEnumeratePhysicalDevices(result.instance, &num_physical_devices, physical_devices.data()) != VK_SUCCESS) {
    THALAMUS_LOG(error) << "vkEnumeratePhysicalDevices";
    return result;
  }

  VkPhysicalDevice physical_device = nullptr;
  //If device_id is specified find that device, otherwise, get the first discrete GPU and if there is no discrete GPU get the integrated GPU
  for(auto p : physical_devices) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(p, &props);
    THALAMUS_LOG(info) << "ID:" << props.deviceID << "Type:" << props.deviceType << " " << props.deviceName;
    if(device_id) {
      if(*device_id == props.deviceID) {
        physical_device = p;
        THALAMUS_LOG(info) << "Selected";
        break;
      }
    } else {
      if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        physical_device = p;
        THALAMUS_LOG(info) << "Selected";
        break;
      } else if(physical_device == nullptr && props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        physical_device = p;
        THALAMUS_LOG(info) << "Selected";
      }
    }
  }

  if(physical_device == nullptr) {
    THALAMUS_LOG(error) << "No suitable physical device found";
    return result;
  }
  result.physical_device = physical_device;
  THALAMUS_LOG(info) << "VkPhysicalDevice Selected";

  uint32_t num_qf = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &num_qf, nullptr);
  if(num_qf == 0) {
    THALAMUS_LOG(error) << "No Queue Families";
    return result;
  }

  std::vector<VkQueueFamilyProperties> qf_props(num_qf);
  std::vector<int> qf_scores(num_qf, 0);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &num_qf, qf_props.data());
  for (uint32_t i = 0; i < num_qf; i++) {
    VkBool32 present = VK_FALSE;
    if(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &present) != VK_SUCCESS) {
      THALAMUS_LOG(error) << "vkGetPhysicalDeviceSurfaceSupportKHR";
      continue;
    }
    if(qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      qf_scores[i] += 10;
    }
    if(present) {
      qf_scores[i] += 1;
    }
  }

  auto max_qf_score_iter = std::max_element(qf_scores.begin(), qf_scores.end());
  auto qf_index = uint32_t(std::distance(qf_scores.begin(), max_qf_score_iter));

  float pri = 1.0f;
  VkDeviceQueueCreateInfo q_ci{};
  q_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  q_ci.queueFamilyIndex = qf_index;
  q_ci.queueCount = 1;
  q_ci.pQueuePriorities = &pri;

  const char* dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo dev_ci{};
  dev_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dev_ci.queueCreateInfoCount = 1;
  dev_ci.pQueueCreateInfos = &q_ci;
  dev_ci.enabledExtensionCount = 1;
  dev_ci.ppEnabledExtensionNames = dev_exts;

  if (vkCreateDevice(physical_device, &dev_ci, nullptr, &result.device) != VK_SUCCESS) {
    THALAMUS_LOG(error) << "vkCreateDevice";
    return result;
  }
  THALAMUS_LOG(info) << "VkDevice Created";

  vkGetDeviceQueue(result.device, qf_index, 0, &result.queue);
  THALAMUS_LOG(info) << "VkQueue Acquired";
  result.queue_family_index = qf_index;
  result.supports_presentation = *max_qf_score_iter > 10;
  return result;
}

void destroy_vulkan(Vulkan vulkan) {
  if(vulkan.device != nullptr) {
    vkDestroyDevice(vulkan.device, nullptr);
  }
  if(vulkan.instance != nullptr) {
    vkDestroyInstance(vulkan.instance, nullptr);
  }
}
} // namespace thalamus
