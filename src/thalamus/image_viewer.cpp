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

#include <thalamus/image_viewer.hpp>
#include <thalamus/assert.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <vector>
#include <texture.vert.spv.h>
#include <texture.frag.spv.h>

namespace thalamus {

static uint32_t findMemType(VkPhysicalDevice phys, uint32_t bits, VkMemoryPropertyFlags props) {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(phys, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
      return i;
  THALAMUS_ABORT("No suitable memory type");
}

static void makeBuffer(VkDevice dev, VkPhysicalDevice phys, VkDeviceSize size,
                       VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                       VkBuffer& buf, VkDeviceMemory& mem) {
  VkBufferCreateInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  vkCreateBuffer(dev, &bi, nullptr, &buf);
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(dev, buf, &req);
  VkMemoryAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = findMemType(phys, req.memoryTypeBits, props);
  vkAllocateMemory(dev, &ai, nullptr, &mem);
  vkBindBufferMemory(dev, buf, mem, 0);
}

static VkCommandBuffer beginOneShot(VkDevice dev, VkCommandPool pool) {
  VkCommandBufferAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
  VkCommandBuffer cb;
  vkAllocateCommandBuffers(dev, &ai, &cb);
  VkCommandBufferBeginInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cb, &bi);
  return cb;
}

static void endOneShot(VkDevice dev, VkCommandPool pool, VkQueue queue, VkCommandBuffer cb) {
  vkEndCommandBuffer(cb);
  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.commandBufferCount = 1; si.pCommandBuffers = &cb;
  vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue);
  vkFreeCommandBuffers(dev, pool, 1, &cb);
}

static void transitionLayout(VkDevice dev, VkCommandPool pool, VkQueue queue,
                              VkImage image, VkImageLayout from, VkImageLayout to) {
  VkCommandBuffer cb = beginOneShot(dev, pool);
  VkImageMemoryBarrier b{};
  b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b.oldLayout = from; b.newLayout = to;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkPipelineStageFlags src, dst;
  if (from == VK_IMAGE_LAYOUT_UNDEFINED) {
    b.srcAccessMask = 0; b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else {
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    src = VK_PIPELINE_STAGE_TRANSFER_BIT; dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }
  vkCmdPipelineBarrier(cb, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
  endOneShot(dev, pool, queue, cb);
}

static void recordBarrier(VkCommandBuffer cb, VkImage image, VkImageLayout from, VkImageLayout to,
                           VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                           VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
  VkImageMemoryBarrier b{};
  b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b.oldLayout = from; b.newLayout = to;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  b.srcAccessMask = srcAccess; b.dstAccessMask = dstAccess;
  vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

static bool read_geometry(const ObservableDictPtr &state, int &x, int &y,
                          int &w, int &h) {
  if (!state || !state->contains("view_geometry")) {
    return false;
  }
  auto value = state->at("view_geometry").get();
  if (!std::holds_alternative<ObservableListPtr>(value)) {
    return false;
  }
  auto list = thalamus::get<ObservableListPtr>(value);
  if (list->size() < 4) {
    return false;
  }
  x = int(int64_t((*list)[0]));
  y = int(int64_t((*list)[1]));
  w = int(int64_t((*list)[2]));
  h = int(int64_t((*list)[3]));
  return true;
}

static void write_geometry(const ObservableDictPtr &state, int x, int y,
                           int w, int h) {
  if (!state) {
    return;
  }
  boost::json::array geometry{x, y, w, h};
  (*state)["view_geometry"].assign(ObservableCollection::from_json(geometry), [] {});
}

// --- Input event conversion (SDL -> JS-style input events) ---

// JS MouseEvent.button: 0=left, 1=middle, 2=right, 3=back, 4=forward
static int js_button_from_sdl(Uint8 button) {
  switch (button) {
    case SDL_BUTTON_LEFT: return 0;
    case SDL_BUTTON_MIDDLE: return 1;
    case SDL_BUTTON_RIGHT: return 2;
    case SDL_BUTTON_X1: return 3;
    case SDL_BUTTON_X2: return 4;
    default: return 0;
  }
}

// JS MouseEvent.buttons bit layout (left=1,right=2,middle=4,back=8,forward=16)
// differs from SDL's (left=1,middle=2,right=4,x1=8,x2=16), so this can't be a
// straight bitmask copy.
static int js_buttons_from_sdl_state(SDL_MouseButtonFlags state) {
  int result = 0;
  if (state & SDL_BUTTON_LMASK) result |= 1;
  if (state & SDL_BUTTON_RMASK) result |= 2;
  if (state & SDL_BUTTON_MMASK) result |= 4;
  if (state & SDL_BUTTON_X1MASK) result |= 8;
  if (state & SDL_BUTTON_X2MASK) result |= 16;
  return result;
}

// --- Impl ---

struct ImageViewer::Impl {
  VkInstance instance = VK_NULL_HANDLE;

  ObservableDictPtr state;
  Node* node = nullptr;
  int geom_x = 0, geom_y = 0, geom_w = 0, geom_h = 0;
  std::chrono::steady_clock::time_point last_geometry_check =
      std::chrono::steady_clock::now();
  uint32_t image_w = 0, image_h = 0;

  const std::map<SDL_Scancode, std::string> scancode_table = {
    {SDL_SCANCODE_A, "KeyA"},
    {SDL_SCANCODE_B, "KeyB"},
    {SDL_SCANCODE_C, "KeyC"},
    {SDL_SCANCODE_D, "KeyD"},
    {SDL_SCANCODE_E, "KeyE"},
    {SDL_SCANCODE_F, "KeyF"},
    {SDL_SCANCODE_G, "KeyG"},
    {SDL_SCANCODE_H, "KeyH"},
    {SDL_SCANCODE_I, "KeyI"},
    {SDL_SCANCODE_J, "KeyJ"},
    {SDL_SCANCODE_K, "KeyK"},
    {SDL_SCANCODE_L, "KeyL"},
    {SDL_SCANCODE_M, "KeyM"},
    {SDL_SCANCODE_N, "KeyN"},
    {SDL_SCANCODE_O, "KeyO"},
    {SDL_SCANCODE_P, "KeyP"},
    {SDL_SCANCODE_Q, "KeyQ"},
    {SDL_SCANCODE_R, "KeyR"},
    {SDL_SCANCODE_S, "KeyS"},
    {SDL_SCANCODE_T, "KeyT"},
    {SDL_SCANCODE_U, "KeyU"},
    {SDL_SCANCODE_V, "KeyV"},
    {SDL_SCANCODE_W, "KeyW"},
    {SDL_SCANCODE_X, "KeyX"},
    {SDL_SCANCODE_Y, "KeyY"},
    {SDL_SCANCODE_Z, "KeyZ"},
    {SDL_SCANCODE_1, "Digit1"},
    {SDL_SCANCODE_2, "Digit2"},
    {SDL_SCANCODE_3, "Digit3"},
    {SDL_SCANCODE_4, "Digit4"},
    {SDL_SCANCODE_5, "Digit5"},
    {SDL_SCANCODE_6, "Digit6"},
    {SDL_SCANCODE_7, "Digit7"},
    {SDL_SCANCODE_8, "Digit8"},
    {SDL_SCANCODE_9, "Digit9"},
    {SDL_SCANCODE_0, "Digit0"},
    {SDL_SCANCODE_RETURN, "Enter"},
    {SDL_SCANCODE_ESCAPE, "Escape"},
    {SDL_SCANCODE_BACKSPACE, "Backspace"},
    {SDL_SCANCODE_TAB, "Tab"},
    {SDL_SCANCODE_SPACE, "Space"},
    {SDL_SCANCODE_MINUS, "Minus"},
    {SDL_SCANCODE_EQUALS, "Equal"},
    {SDL_SCANCODE_LEFTBRACKET, "BracketLeft"},
    {SDL_SCANCODE_RIGHTBRACKET, "BracketRight"},
    {SDL_SCANCODE_BACKSLASH, "Backslash"},
    {SDL_SCANCODE_SEMICOLON, "Semicolon"},
    {SDL_SCANCODE_APOSTROPHE, "Quote"},
    {SDL_SCANCODE_GRAVE, "Backquote"},
    {SDL_SCANCODE_COMMA, "Comma"},
    {SDL_SCANCODE_PERIOD, "Period"},
    {SDL_SCANCODE_SLASH, "Slash"},
    {SDL_SCANCODE_CAPSLOCK, "CapsLock"},
    {SDL_SCANCODE_F1, "F1"},
    {SDL_SCANCODE_F2, "F2"},
    {SDL_SCANCODE_F3, "F3"},
    {SDL_SCANCODE_F4, "F4"},
    {SDL_SCANCODE_F5, "F5"},
    {SDL_SCANCODE_F6, "F6"},
    {SDL_SCANCODE_F7, "F7"},
    {SDL_SCANCODE_F8, "F8"},
    {SDL_SCANCODE_F9, "F9"},
    {SDL_SCANCODE_F10, "F10"},
    {SDL_SCANCODE_F11, "F11"},
    {SDL_SCANCODE_F12, "F12"},
    {SDL_SCANCODE_F13, "F13"},
    {SDL_SCANCODE_F14, "F14"},
    {SDL_SCANCODE_F15, "F15"},
    {SDL_SCANCODE_F16, "F16"},
    {SDL_SCANCODE_F17, "F17"},
    {SDL_SCANCODE_F18, "F18"},
    {SDL_SCANCODE_F19, "F19"},
    {SDL_SCANCODE_F20, "F20"},
    {SDL_SCANCODE_F21, "F21"},
    {SDL_SCANCODE_F22, "F22"},
    {SDL_SCANCODE_F23, "F23"},
    {SDL_SCANCODE_F24, "F24"},
    {SDL_SCANCODE_PRINTSCREEN, "PrintScreen"},
    {SDL_SCANCODE_SCROLLLOCK, "ScrollLock"},
    {SDL_SCANCODE_PAUSE, "Pause"},
    {SDL_SCANCODE_INSERT, "Insert"},
    {SDL_SCANCODE_HOME, "Home"},
    {SDL_SCANCODE_PAGEUP, "PageUp"},
    {SDL_SCANCODE_DELETE, "Delete"},
    {SDL_SCANCODE_END, "End"},
    {SDL_SCANCODE_PAGEDOWN, "PageDown"},
    {SDL_SCANCODE_RIGHT, "ArrowRight"},
    {SDL_SCANCODE_LEFT, "ArrowLeft"},
    {SDL_SCANCODE_DOWN, "ArrowDown"},
    {SDL_SCANCODE_UP, "ArrowUp"},
    {SDL_SCANCODE_NUMLOCKCLEAR, "NumLock"},
    {SDL_SCANCODE_KP_DIVIDE, "NumpadDivide"},
    {SDL_SCANCODE_KP_MULTIPLY, "NumpadMultiply"},
    {SDL_SCANCODE_KP_MINUS, "NumpadSubtract"},
    {SDL_SCANCODE_KP_PLUS, "NumpadAdd"},
    {SDL_SCANCODE_KP_ENTER, "NumpadEnter"},
    {SDL_SCANCODE_KP_1, "Numpad1"},
    {SDL_SCANCODE_KP_2, "Numpad2"},
    {SDL_SCANCODE_KP_3, "Numpad3"},
    {SDL_SCANCODE_KP_4, "Numpad4"},
    {SDL_SCANCODE_KP_5, "Numpad5"},
    {SDL_SCANCODE_KP_6, "Numpad6"},
    {SDL_SCANCODE_KP_7, "Numpad7"},
    {SDL_SCANCODE_KP_8, "Numpad8"},
    {SDL_SCANCODE_KP_9, "Numpad9"},
    {SDL_SCANCODE_KP_0, "Numpad0"},
    {SDL_SCANCODE_KP_PERIOD, "NumpadDecimal"},
    {SDL_SCANCODE_KP_EQUALS, "NumpadEqual"},
    {SDL_SCANCODE_KP_COMMA, "NumpadComma"},
    {SDL_SCANCODE_LCTRL, "ControlLeft"},
    {SDL_SCANCODE_LSHIFT, "ShiftLeft"},
    {SDL_SCANCODE_LALT, "AltLeft"},
    {SDL_SCANCODE_LGUI, "MetaLeft"},
    {SDL_SCANCODE_RCTRL, "ControlRight"},
    {SDL_SCANCODE_RSHIFT, "ShiftRight"},
    {SDL_SCANCODE_RALT, "AltRight"},
    {SDL_SCANCODE_RGUI, "MetaRight"},
    {SDL_SCANCODE_APPLICATION, "ContextMenu"},
    {SDL_SCANCODE_HELP, "Help"},
  };

  std::string scancode_to_js_code(SDL_Scancode code) const {
    auto i = scancode_table.find(code);
    return i == scancode_table.end() ? std::string() : i->second;
  }

  // Maps window-space coordinates to image-pixel coordinates, accounting for
  // the aspect-preserving letterboxed viewport computed in update().
  std::pair<int, int> map_to_image(float wx, float wy) const {
    if (image_w == 0 || image_h == 0 || extent.width == 0 || extent.height == 0) {
      return {0, 0};
    }
    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(window, &win_w, &win_h);
    if (win_w > 0 && win_h > 0) {
      wx *= static_cast<float>(extent.width) / static_cast<float>(win_w);
      wy *= static_cast<float>(extent.height) / static_cast<float>(win_h);
    }

    float imgAspect = static_cast<float>(image_w) / static_cast<float>(image_h);
    float winAspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    float vpW, vpH, vpX, vpY;
    if (winAspect > imgAspect) {
      vpH = static_cast<float>(extent.height); vpW = vpH * imgAspect;
      vpX = (static_cast<float>(extent.width) - vpW) / 2.0f; vpY = 0.0f;
    } else {
      vpW = static_cast<float>(extent.width); vpH = vpW / imgAspect;
      vpX = 0.0f; vpY = (static_cast<float>(extent.height) - vpH) / 2.0f;
    }

    int ix = int((wx - vpX) / vpW * static_cast<float>(image_w));
    int iy = int((wy - vpY) / vpH * static_cast<float>(image_h));
    ix = std::clamp(ix, 0, int(image_w) - 1);
    iy = std::clamp(iy, 0, int(image_h) - 1);
    return {ix, iy};
  }

  void send_key_event(const SDL_KeyboardEvent& key_event) {
    if (!node) {
      return;
    }
    auto code = scancode_to_js_code(key_event.scancode);
    if (code.empty()) {
      return;
    }
    std::string type = key_event.down ? "keydown" : "keyup";
    boost::json::object inner;
    inner["type"] = type;
    inner["code"] = code;
    boost::json::object outer;
    outer[type] = inner;
    node->process(outer);
  }

  void send_mouse_event(const std::string& type, float wx, float wy, int button,
                        int buttons) {
    if (!node) {
      return;
    }
    auto [ix, iy] = map_to_image(wx, wy);
    boost::json::object inner;
    inner["type"] = type;
    inner["offsetX"] = ix;
    inner["offsetY"] = iy;
    inner["button"] = button;
    inner["buttons"] = buttons;
    boost::json::object outer;
    outer[type] = inner;
    node->process(outer);
  }

  SDL_Window* window = nullptr;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkSurfaceFormatKHR surf_fmt{};
  uint32_t gfx_family = UINT32_MAX;
  VkDevice dev = VK_NULL_HANDLE;
  NodeGraph* graph = nullptr;
  VkQueue queue = VK_NULL_HANDLE;
  VkCommandPool cmd_pool = VK_NULL_HANDLE;

  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkExtent2D extent{};
  std::vector<VkImage> sc_images;
  std::vector<VkImageView> sc_views;
  std::vector<VkFramebuffer> framebuffers;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  std::atomic<bool> needs_recreate{false};

  std::mutex swapchain_mutex;
  int swapchain_generation = 0;

  static constexpr int MAX_FRAMES = 2;

  uint32_t tex_w[MAX_FRAMES] = {}, tex_h[MAX_FRAMES] = {};
  VkImage tex_image[MAX_FRAMES]{};
  VkDeviceMemory tex_mem[MAX_FRAMES]{};
  VkImageView tex_view[MAX_FRAMES]{};
  VkSampler sampler = VK_NULL_HANDLE;
  VkBuffer stage_buf[MAX_FRAMES]{};
  VkDeviceMemory stage_mem[MAX_FRAMES]{};
  void* stage_mapped[MAX_FRAMES]{};
  VkDeviceSize stage_size[MAX_FRAMES]{};

  // Pipeline
  VkDescriptorSetLayout desc_layout = VK_NULL_HANDLE;
  VkDescriptorPool desc_pool = VK_NULL_HANDLE;
  VkDescriptorSet desc_set[MAX_FRAMES]{};
  VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;

  // Sync
  VkCommandBuffer cmd_bufs[MAX_FRAMES]{};
  VkSemaphore image_avail[MAX_FRAMES]{};
  std::vector<VkSemaphore> render_done;  // one per swapchain image, not per frame
  VkFence in_flight[MAX_FRAMES]{};
  int frame = 0;

  void drainAcquireSemaphore(int f) {
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1; si.pWaitSemaphores = &image_avail[f];
    si.pWaitDstStageMask = &waitStage;
    vkQueueSubmit(queue, 1, &si, in_flight[f]);
  }

  void destroySwapchainDeps() {
    for (auto fb : framebuffers) vkDestroyFramebuffer(dev, fb, nullptr);
    framebuffers.clear();
    for (auto iv : sc_views) vkDestroyImageView(dev, iv, nullptr);
    sc_views.clear();
    for (auto s : render_done) vkDestroySemaphore(dev, s, nullptr);
    render_done.clear();
  }

  bool buildSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
    if (caps.currentExtent.width == UINT32_MAX) {
      int w = 0, h = 0;
      SDL_GetWindowSizeInPixels(window, &w, &h);
      extent.width  = std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width,  caps.maxImageExtent.width);
      extent.height = std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height);
    } else {
      extent = caps.currentExtent;
    }
    if (extent.width == 0 || extent.height == 0) return false;

    VkSwapchainCreateInfoKHR scCI{};
    scCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scCI.surface = surface;
    scCI.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && scCI.minImageCount > caps.maxImageCount)
      scCI.minImageCount = caps.maxImageCount;
    scCI.imageFormat = surf_fmt.format;
    scCI.imageColorSpace = surf_fmt.colorSpace;
    scCI.imageExtent = extent;
    scCI.imageArrayLayers = 1;
    scCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    scCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scCI.preTransform = caps.currentTransform;
    scCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    scCI.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    scCI.clipped = VK_TRUE;
    scCI.oldSwapchain = swapchain;

    VkSwapchainKHR newSC;
    auto result = vkCreateSwapchainKHR(dev, &scCI, nullptr, &newSC);
    if (result != VK_SUCCESS) {
      THALAMUS_ABORT("vkCreateSwapchainKHR: %d", result);
    }
    if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(dev, swapchain, nullptr);
    swapchain = newSC;

    uint32_t nImg = 0;
    vkGetSwapchainImagesKHR(dev, swapchain, &nImg, nullptr);
    sc_images.resize(nImg);
    vkGetSwapchainImagesKHR(dev, swapchain, &nImg, sc_images.data());

    sc_views.resize(nImg);
    for (uint32_t i = 0; i < nImg; i++) {
      VkImageViewCreateInfo ivCI{};
      ivCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      ivCI.image = sc_images[i]; ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
      ivCI.format = surf_fmt.format;
      ivCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCreateImageView(dev, &ivCI, nullptr, &sc_views[i]);
    }

    framebuffers.resize(nImg);
    for (uint32_t i = 0; i < nImg; i++) {
      VkFramebufferCreateInfo fbCI{};
      fbCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fbCI.renderPass = render_pass;
      fbCI.attachmentCount = 1; fbCI.pAttachments = &sc_views[i];
      fbCI.width = extent.width; fbCI.height = extent.height; fbCI.layers = 1;
      vkCreateFramebuffer(dev, &fbCI, nullptr, &framebuffers[i]);
    }

    render_done.resize(nImg);
    VkSemaphoreCreateInfo rdSemCI{};
    rdSemCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < nImg; i++)
      vkCreateSemaphore(dev, &rdSemCI, nullptr, &render_done[i]);

    ++swapchain_generation;
    return true;
  }

  void recreateSwapchain() {
    std::lock_guard<std::mutex> lock(swapchain_mutex);
    vkDeviceWaitIdle(dev);

    destroySwapchainDeps();
    buildSwapchain();
  }

  void destroyTexture(int slot) {
    if (stage_mapped[slot]) { vkUnmapMemory(dev, stage_mem[slot]); stage_mapped[slot] = nullptr; }
    if (stage_buf[slot]  != VK_NULL_HANDLE) { vkDestroyBuffer(dev, stage_buf[slot], nullptr);  stage_buf[slot]  = VK_NULL_HANDLE; }
    if (stage_mem[slot]  != VK_NULL_HANDLE) { vkFreeMemory(dev, stage_mem[slot], nullptr);      stage_mem[slot]  = VK_NULL_HANDLE; }
    if (tex_view[slot]   != VK_NULL_HANDLE) { vkDestroyImageView(dev, tex_view[slot], nullptr); tex_view[slot]   = VK_NULL_HANDLE; }
    if (tex_image[slot]  != VK_NULL_HANDLE) { vkDestroyImage(dev, tex_image[slot], nullptr);    tex_image[slot]  = VK_NULL_HANDLE; }
    if (tex_mem[slot]    != VK_NULL_HANDLE) { vkFreeMemory(dev, tex_mem[slot], nullptr);         tex_mem[slot]    = VK_NULL_HANDLE; }
  }

  void buildTexture(int slot, uint32_t w, uint32_t h) {
    destroyTexture(slot);
    tex_w[slot] = w; tex_h[slot] = h;
    VkDeviceSize size = static_cast<VkDeviceSize>(w) * h;

    makeBuffer(dev, phys, size,
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stage_buf[slot], stage_mem[slot]);
    vkMapMemory(dev, stage_mem[slot], 0, size, 0, &stage_mapped[slot]);
    stage_size[slot] = size;

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8_UNORM;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1; ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(dev, &ici, nullptr, &tex_image[slot]);

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, tex_image[slot], &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemType(phys, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(dev, &ai, nullptr, &tex_mem[slot]);
    vkBindImageMemory(dev, tex_image[slot], tex_mem[slot], 0);

    {
      auto queue_lock = graph->lock_vulkan_queue();
      transitionLayout(dev, cmd_pool, queue, tex_image[slot],
                       VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    VkImageViewCreateInfo ivCI{};
    ivCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivCI.image = tex_image[slot]; ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivCI.format = VK_FORMAT_R8_UNORM;
    ivCI.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R,
                        VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE};
    ivCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(dev, &ivCI, nullptr, &tex_view[slot]);

    VkDescriptorImageInfo dii{sampler, tex_view[slot], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet dset{};
    dset.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    dset.dstSet = desc_set[slot]; dset.dstBinding = 0; dset.descriptorCount = 1;
    dset.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dset.pImageInfo = &dii;
    vkUpdateDescriptorSets(dev, 1, &dset, 0, nullptr);
  }

  void uploadTexture(int slot, VkCommandBuffer cb, ImageNode::Plane plane, uint32_t w, uint32_t h) {
    if (w != tex_w[slot] || h != tex_h[slot]) buildTexture(slot, w, h);

    std::memcpy(stage_mapped[slot], plane.data(), static_cast<size_t>(w) * h);

    recordBarrier(cb, tex_image[slot], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cb, stage_buf[slot], tex_image[slot], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    recordBarrier(cb, tex_image[slot], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  }
};

ImageViewer::ImageViewer(NodeGraph* graph, boost::asio::io_context&,
                          ObservableDictPtr state, Node* node)
    : impl(std::make_unique<Impl>()) {
  impl->state = state;
  impl->node = node;
  impl->instance = graph->get_vulkan_instance();
  impl->dev = graph->get_vulkan_device();
  impl->phys = graph->get_vulkan_physical_device();
  impl->graph = graph;
  impl->queue = graph->get_vulkan_queue();
  impl->cmd_pool = graph->create_vulkan_command_pool();

  // SDL window
  if (!SDL_Init(SDL_INIT_VIDEO))
    THALAMUS_ABORT("SDL_Init: %s", SDL_GetError());

  int init_x = 100, init_y = 100, init_w = 400, init_h = 400;
  if (!read_geometry(state, init_x, init_y, init_w, init_h)) {
    write_geometry(state, init_x, init_y, init_w, init_h);
  }
  impl->geom_x = init_x; impl->geom_y = init_y;
  impl->geom_w = init_w; impl->geom_h = init_h;

  impl->window = SDL_CreateWindow("Image Viewer", init_w, init_h,
                                  SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (!impl->window) THALAMUS_ABORT("%s", SDL_GetError());
  SDL_SetWindowPosition(impl->window, init_x, init_y);

  // Surface
  if (!SDL_Vulkan_CreateSurface(impl->window, impl->instance, nullptr, &impl->surface))
    THALAMUS_ABORT("%s", SDL_GetError());

  // Surface format
  uint32_t nFmt = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(impl->phys, impl->surface, &nFmt, nullptr);
  std::vector<VkSurfaceFormatKHR> fmts(nFmt);
  vkGetPhysicalDeviceSurfaceFormatsKHR(impl->phys, impl->surface, &nFmt, fmts.data());
  impl->surf_fmt = fmts[0];
  for (auto& f : fmts)
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM) { impl->surf_fmt = f; break; }

  // Render pass
  VkAttachmentDescription att{};
  att.format = impl->surf_fmt.format;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo rpCI{};
  rpCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpCI.attachmentCount = 1; rpCI.pAttachments = &att;
  rpCI.subpassCount = 1; rpCI.pSubpasses = &sub;
  rpCI.dependencyCount = 1; rpCI.pDependencies = &dep;
  vkCreateRenderPass(impl->dev, &rpCI, nullptr, &impl->render_pass);

  // Sampler
  VkSamplerCreateInfo sampCI{};
  sampCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampCI.magFilter = VK_FILTER_LINEAR; sampCI.minFilter = VK_FILTER_LINEAR;
  sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  vkCreateSampler(impl->dev, &sampCI, nullptr, &impl->sampler);

  // Descriptor layout + pool + set
  VkDescriptorSetLayoutBinding bind{};
  bind.binding = 0; bind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bind.descriptorCount = 1; bind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo dslCI{};
  dslCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  dslCI.bindingCount = 1; dslCI.pBindings = &bind;
  vkCreateDescriptorSetLayout(impl->dev, &dslCI, nullptr, &impl->desc_layout);

  VkDescriptorPoolSize psz{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, Impl::MAX_FRAMES};
  VkDescriptorPoolCreateInfo dpCI{};
  dpCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpCI.maxSets = Impl::MAX_FRAMES; dpCI.poolSizeCount = 1; dpCI.pPoolSizes = &psz;
  vkCreateDescriptorPool(impl->dev, &dpCI, nullptr, &impl->desc_pool);

  VkDescriptorSetLayout setLayouts[Impl::MAX_FRAMES];
  for (int i = 0; i < Impl::MAX_FRAMES; i++) setLayouts[i] = impl->desc_layout;
  VkDescriptorSetAllocateInfo dsAI{};
  dsAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  dsAI.descriptorPool = impl->desc_pool; dsAI.descriptorSetCount = Impl::MAX_FRAMES;
  dsAI.pSetLayouts = setLayouts;
  vkAllocateDescriptorSets(impl->dev, &dsAI, impl->desc_set);

  // Pipeline
  auto makeShader = [&](std::span<const uint32_t> code) {
    VkShaderModuleCreateInfo smCI{};
    smCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smCI.codeSize = code.size_bytes();
    smCI.pCode = code.data();
    VkShaderModule sm;
    vkCreateShaderModule(impl->dev, &smCI, nullptr, &sm);
    return sm;
  };
  VkShaderModule vertSM = makeShader(std::span<const uint32_t>(vert_shader_data));
  VkShaderModule fragSM = makeShader(std::span<const uint32_t>(frag_shader_data));

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertSM; stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragSM; stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo   viState{};
  viState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineInputAssemblyStateCreateInfo iaState{};
  iaState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  iaState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynState{};
  dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynState.dynamicStateCount = 2; dynState.pDynamicStates = dynStates;
  VkPipelineViewportStateCreateInfo vpState{};
  vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vpState.viewportCount = 1; vpState.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rsState{};
  rsState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rsState.polygonMode = VK_POLYGON_MODE_FILL; rsState.cullMode = VK_CULL_MODE_NONE;
  rsState.frontFace = VK_FRONT_FACE_CLOCKWISE; rsState.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo msState{};
  msState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  msState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState blendAtt{};
  blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cbState{};
  cbState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cbState.attachmentCount = 1; cbState.pAttachments = &blendAtt;

  VkPipelineLayoutCreateInfo plCI{};
  plCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plCI.setLayoutCount = 1; plCI.pSetLayouts = &impl->desc_layout;
  vkCreatePipelineLayout(impl->dev, &plCI, nullptr, &impl->pipe_layout);

  VkGraphicsPipelineCreateInfo gpCI{};
  gpCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gpCI.stageCount = 2; gpCI.pStages = stages;
  gpCI.pVertexInputState = &viState; gpCI.pInputAssemblyState = &iaState;
  gpCI.pViewportState = &vpState; gpCI.pRasterizationState = &rsState;
  gpCI.pMultisampleState = &msState; gpCI.pColorBlendState = &cbState;
  gpCI.pDynamicState = &dynState;
  gpCI.layout = impl->pipe_layout; gpCI.renderPass = impl->render_pass;
  vkCreateGraphicsPipelines(impl->dev, VK_NULL_HANDLE, 1, &gpCI, nullptr, &impl->pipeline);

  vkDestroyShaderModule(impl->dev, vertSM, nullptr);
  vkDestroyShaderModule(impl->dev, fragSM, nullptr);

  // Command buffers
  VkCommandBufferAllocateInfo cbAI{};
  cbAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cbAI.commandPool = impl->cmd_pool;
  cbAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbAI.commandBufferCount = Impl::MAX_FRAMES;
  vkAllocateCommandBuffers(impl->dev, &cbAI, impl->cmd_bufs);

  // Sync objects
  VkSemaphoreCreateInfo semCI{};
  semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkFenceCreateInfo fenCI{};
  fenCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  // acquire_fence starts unsignaled (unlike in_flight) -- it's meant to be
  // signaled by the acquire call itself, not pre-signaled as "free".
  VkFenceCreateInfo acquireFenCI{};
  acquireFenCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  for (int i = 0; i < Impl::MAX_FRAMES; i++) {
    vkCreateSemaphore(impl->dev, &semCI, nullptr, &impl->image_avail[i]);
    vkCreateFence(impl->dev, &fenCI, nullptr, &impl->in_flight[i]);
  }

  // Initial swapchain
  impl->buildSwapchain();
}

ImageViewer::~ImageViewer() {
  if (!impl->dev) return;
  vkDeviceWaitIdle(impl->dev);

  for (int i = 0; i < Impl::MAX_FRAMES; i++) {
    impl->destroyTexture(i);
    vkDestroySemaphore(impl->dev, impl->image_avail[i], nullptr);
    vkDestroyFence(impl->dev, impl->in_flight[i], nullptr);
  }
  vkDestroyPipeline(impl->dev, impl->pipeline, nullptr);
  vkDestroyPipelineLayout(impl->dev, impl->pipe_layout, nullptr);
  vkDestroyDescriptorPool(impl->dev, impl->desc_pool, nullptr);
  vkDestroyDescriptorSetLayout(impl->dev, impl->desc_layout, nullptr);
  vkDestroySampler(impl->dev, impl->sampler, nullptr);
  impl->destroySwapchainDeps();
  vkDestroyRenderPass(impl->dev, impl->render_pass, nullptr);
  vkDestroyCommandPool(impl->dev, impl->cmd_pool, nullptr);
  if (impl->swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(impl->dev, impl->swapchain, nullptr);
  vkDestroySurfaceKHR(impl->instance, impl->surface, nullptr);
  if (impl->window) SDL_DestroyWindow(impl->window);
  SDL_Quit();
}

void ImageViewer::poll_events() {
  SDL_Event event;
  if (SDL_PollEvent(&event)) {
    do {
      if (event.type == SDL_EVENT_WINDOW_RESIZED) impl->needs_recreate = true;
      else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        (*impl->state)["View"].assign(false, [] {});
      } else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
        impl->send_key_event(event.key);
      } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
        impl->send_mouse_event("mousemove", event.motion.x, event.motion.y, 0,
                               js_buttons_from_sdl_state(event.motion.state));
      } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        auto buttons = js_buttons_from_sdl_state(SDL_GetMouseState(nullptr, nullptr));
        impl->send_mouse_event(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? "mousedown" : "mouseup",
                               event.button.x, event.button.y,
                               js_button_from_sdl(event.button.button), buttons);
      }
    } while (SDL_PollEvent(&event));
  }

  if (impl->needs_recreate) {
    impl->recreateSwapchain();
    impl->needs_recreate = false;
  }

  auto now = std::chrono::steady_clock::now();
  if (now - impl->last_geometry_check >= std::chrono::seconds(1)) {
    impl->last_geometry_check = now;
    int x, y, w, h;
    SDL_GetWindowPosition(impl->window, &x, &y);
    SDL_GetWindowSize(impl->window, &w, &h);
    if (x != impl->geom_x || y != impl->geom_y || w != impl->geom_w || h != impl->geom_h) {
      impl->geom_x = x; impl->geom_y = y;
      impl->geom_w = w; impl->geom_h = h;
      write_geometry(impl->state, x, y, w, h);
    }
  }
}

void ImageViewer::update(ImageNode* node) {
  if (node && node->has_image_data()) {
    impl->image_w = static_cast<uint32_t>(node->width());
    impl->image_h = static_cast<uint32_t>(node->height());
  }

  if (impl->needs_recreate) return;
  if (impl->extent.width == 0 || impl->extent.height == 0) return;

  int f = impl->frame;

  if (vkGetFenceStatus(impl->dev, impl->in_flight[f]) != VK_SUCCESS) return;

  bool have_new_data = node && node->has_image_data();
  if (!have_new_data) return;  // nothing to draw yet

  std::unique_lock<std::mutex> lock(impl->swapchain_mutex);
  int generation = impl->swapchain_generation;

  uint32_t imgIdx;
  VkResult res = vkAcquireNextImageKHR(impl->dev, impl->swapchain, 0,
                                        impl->image_avail[f], nullptr, &imgIdx);
  if (res == VK_ERROR_OUT_OF_DATE_KHR) {
    impl->needs_recreate = true;
    return;
  }
  if (res == VK_NOT_READY || res == VK_TIMEOUT) return;  // no image ready right now; drop frame
  if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) return;

  vkResetFences(impl->dev, 1, &impl->in_flight[f]);

  VkCommandBuffer cb = impl->cmd_bufs[f];
  vkResetCommandBuffer(cb, 0);
  VkCommandBufferBeginInfo cbbi{};
  cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  vkBeginCommandBuffer(cb, &cbbi);

  if (have_new_data) {
    impl->uploadTexture(f, cb, node->plane(0), static_cast<uint32_t>(node->width()),
                         static_cast<uint32_t>(node->height()));
  }

  VkClearValue clear{};
  VkRenderPassBeginInfo rpBI{};
  rpBI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rpBI.renderPass = impl->render_pass;
  rpBI.framebuffer = impl->framebuffers[imgIdx];
  rpBI.renderArea = {{0, 0}, impl->extent};
  rpBI.clearValueCount = 1; rpBI.pClearValues = &clear;
  vkCmdBeginRenderPass(cb, &rpBI, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, impl->pipeline);

  // Aspect-ratio preserving viewport
  float imgAspect = static_cast<float>(impl->tex_w[f]) / static_cast<float>(impl->tex_h[f]);
  float winAspect = static_cast<float>(impl->extent.width) / static_cast<float>(impl->extent.height);
  float vpW, vpH, vpX, vpY;
  if (winAspect > imgAspect) {
    vpH = static_cast<float>(impl->extent.height); vpW = vpH * imgAspect;
    vpX = (static_cast<float>(impl->extent.width) - vpW) / 2.0f; vpY = 0.0f;
  } else {
    vpW = static_cast<float>(impl->extent.width); vpH = vpW / imgAspect;
    vpX = 0.0f; vpY = (static_cast<float>(impl->extent.height) - vpH) / 2.0f;
  }
  VkViewport vp{vpX, vpY, vpW, vpH, 0.0f, 1.0f};
  VkRect2D scissor{{static_cast<int32_t>(vpX), static_cast<int32_t>(vpY)},
                   {static_cast<uint32_t>(vpW), static_cast<uint32_t>(vpH)}};
  vkCmdSetViewport(cb, 0, 1, &vp);
  vkCmdSetScissor(cb, 0, 1, &scissor);

  vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           impl->pipe_layout, 0, 1, &impl->desc_set[f], 0, nullptr);
  vkCmdDraw(cb, 3, 1, 0, 0);
  vkCmdEndRenderPass(cb);
  vkEndCommandBuffer(cb);
  lock.unlock();

  auto queue_lock = impl->graph->lock_vulkan_queue();

  if (impl->swapchain_generation != generation) {
    impl->drainAcquireSemaphore(f);
    impl->frame = (f + 1) % Impl::MAX_FRAMES;
    return;
  }

  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.waitSemaphoreCount = 1; si.pWaitSemaphores = &impl->image_avail[f];
  si.pWaitDstStageMask = &waitStage;
  si.commandBufferCount = 1; si.pCommandBuffers = &cb;
  si.signalSemaphoreCount = 1; si.pSignalSemaphores = &impl->render_done[imgIdx];
  vkQueueSubmit(impl->queue, 1, &si, impl->in_flight[f]);

  VkPresentInfoKHR pi{};
  pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &impl->render_done[imgIdx];
  pi.swapchainCount = 1; pi.pSwapchains = &impl->swapchain; pi.pImageIndices = &imgIdx;
  VkResult present_res = vkQueuePresentKHR(impl->queue, &pi);
  if (present_res == VK_ERROR_OUT_OF_DATE_KHR || present_res == VK_SUBOPTIMAL_KHR)
    impl->needs_recreate = true;

  impl->frame = (f + 1) % Impl::MAX_FRAMES;
}

} // namespace thalamus
