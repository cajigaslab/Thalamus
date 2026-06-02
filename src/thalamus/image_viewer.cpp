#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <opencv2/opencv.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <thalamus/image_viewer.hpp>
#include <thalamus/vulkan_instance.hpp>
#include <thalamus/assert.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
#include <texture.vert.spv.h>
#include <texture.frag.spv.h>

namespace thalamus {

// --- Vulkan helpers ---

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
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  vkCreateBuffer(dev, &bi, nullptr, &buf);
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(dev, buf, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = findMemType(phys, req.memoryTypeBits, props);
  vkAllocateMemory(dev, &ai, nullptr, &mem);
  vkBindBufferMemory(dev, buf, mem, 0);
}

static VkCommandBuffer beginOneShot(VkDevice dev, VkCommandPool pool) {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
  VkCommandBuffer cb;
  vkAllocateCommandBuffers(dev, &ai, &cb);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cb, &bi);
  return cb;
}

static void endOneShot(VkDevice dev, VkCommandPool pool, VkQueue queue, VkCommandBuffer cb) {
  vkEndCommandBuffer(cb);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1; si.pCommandBuffers = &cb;
  vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue);
  vkFreeCommandBuffers(dev, pool, 1, &cb);
}

static void transitionLayout(VkDevice dev, VkCommandPool pool, VkQueue queue,
                              VkImage image, VkImageLayout from, VkImageLayout to) {
  VkCommandBuffer cb = beginOneShot(dev, pool);
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
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

// --- Format conversion ---

static cv::Mat toRGBA(ImageNode* node) {
  auto w = static_cast<int>(node->width());
  auto h = static_cast<int>(node->height());
  cv::Mat rgba;

  switch (node->format()) {
    case ImageNode::Format::Gray: {
      cv::Mat src(h, w, CV_8UC1, const_cast<unsigned char*>(node->plane(0).data()));
      cv::cvtColor(src, rgba, cv::COLOR_GRAY2RGBA);
      break;
    }
    case ImageNode::Format::RGB: {
      cv::Mat src(h, w, CV_8UC3, const_cast<unsigned char*>(node->plane(0).data()));
      cv::cvtColor(src, rgba, cv::COLOR_RGB2RGBA);
      break;
    }
    case ImageNode::Format::YUYV422: {
      cv::Mat src(h, w, CV_8UC2, const_cast<unsigned char*>(node->plane(0).data()));
      cv::cvtColor(src, rgba, cv::COLOR_YUV2RGBA_YUYV);
      break;
    }
    case ImageNode::Format::YUV420P:
    case ImageNode::Format::YUVJ420P: {
      cv::Mat yuv(h * 3 / 2, w, CV_8UC1);
      std::memcpy(yuv.data,                          node->plane(0).data(), static_cast<size_t>(w * h));
      std::memcpy(yuv.data + w * h,                  node->plane(1).data(), static_cast<size_t>(w * h / 4));
      std::memcpy(yuv.data + w * h + w * h / 4,      node->plane(2).data(), static_cast<size_t>(w * h / 4));
      cv::cvtColor(yuv, rgba, cv::COLOR_YUV2RGBA_I420);
      break;
    }
  }
  return rgba;
}

// --- Impl ---

struct ImageViewer::Impl {
  // Borrowed from NodeGraph
  VkInstance instance = VK_NULL_HANDLE;

  SDL_Window* window = nullptr;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkSurfaceFormatKHR surf_fmt{};
  uint32_t gfx_family = UINT32_MAX;
  VkDevice dev = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  VkCommandPool cmd_pool = VK_NULL_HANDLE;

  // Swapchain (recreatable)
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkExtent2D extent{};
  std::vector<VkImage> sc_images;
  std::vector<VkImageView> sc_views;
  std::vector<VkFramebuffer> framebuffers;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  bool needs_recreate = false;

  // Texture
  uint32_t tex_w = 0, tex_h = 0;
  VkImage tex_image = VK_NULL_HANDLE;
  VkDeviceMemory tex_mem = VK_NULL_HANDLE;
  VkImageView tex_view = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  VkBuffer stage_buf = VK_NULL_HANDLE;
  VkDeviceMemory stage_mem = VK_NULL_HANDLE;
  void* stage_mapped = nullptr;
  VkDeviceSize stage_size = 0;

  // Pipeline
  VkDescriptorSetLayout desc_layout = VK_NULL_HANDLE;
  VkDescriptorPool desc_pool = VK_NULL_HANDLE;
  VkDescriptorSet desc_set = VK_NULL_HANDLE;
  VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;

  // Sync
  static constexpr int MAX_FRAMES = 2;
  VkCommandBuffer cmd_bufs[MAX_FRAMES]{};
  VkSemaphore image_avail[MAX_FRAMES]{};
  std::vector<VkSemaphore> render_done;  // one per swapchain image, not per frame
  VkFence in_flight[MAX_FRAMES]{};
  int frame = 0;

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

    VkSwapchainCreateInfoKHR scCI{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
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
      VkImageViewCreateInfo ivCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      ivCI.image = sc_images[i]; ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
      ivCI.format = surf_fmt.format;
      ivCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCreateImageView(dev, &ivCI, nullptr, &sc_views[i]);
    }

    framebuffers.resize(nImg);
    for (uint32_t i = 0; i < nImg; i++) {
      VkFramebufferCreateInfo fbCI{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      fbCI.renderPass = render_pass;
      fbCI.attachmentCount = 1; fbCI.pAttachments = &sc_views[i];
      fbCI.width = extent.width; fbCI.height = extent.height; fbCI.layers = 1;
      vkCreateFramebuffer(dev, &fbCI, nullptr, &framebuffers[i]);
    }

    render_done.resize(nImg);
    VkSemaphoreCreateInfo rdSemCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < nImg; i++)
      vkCreateSemaphore(dev, &rdSemCI, nullptr, &render_done[i]);

    return true;
  }

  void recreateSwapchain() {
    vkDeviceWaitIdle(dev);
    destroySwapchainDeps();
    buildSwapchain();
  }

  void destroyTexture() {
    if (stage_mapped) { vkUnmapMemory(dev, stage_mem); stage_mapped = nullptr; }
    if (stage_buf  != VK_NULL_HANDLE) { vkDestroyBuffer(dev, stage_buf, nullptr);  stage_buf  = VK_NULL_HANDLE; }
    if (stage_mem  != VK_NULL_HANDLE) { vkFreeMemory(dev, stage_mem, nullptr);      stage_mem  = VK_NULL_HANDLE; }
    if (tex_view   != VK_NULL_HANDLE) { vkDestroyImageView(dev, tex_view, nullptr); tex_view   = VK_NULL_HANDLE; }
    if (tex_image  != VK_NULL_HANDLE) { vkDestroyImage(dev, tex_image, nullptr);    tex_image  = VK_NULL_HANDLE; }
    if (tex_mem    != VK_NULL_HANDLE) { vkFreeMemory(dev, tex_mem, nullptr);         tex_mem    = VK_NULL_HANDLE; }
  }

  void buildTexture(uint32_t w, uint32_t h) {
    destroyTexture();
    tex_w = w; tex_h = h;
    VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * 4;

    makeBuffer(dev, phys, size,
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stage_buf, stage_mem);
    vkMapMemory(dev, stage_mem, 0, size, 0, &stage_mapped);
    stage_size = size;

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1; ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(dev, &ici, nullptr, &tex_image);

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, tex_image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemType(phys, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(dev, &ai, nullptr, &tex_mem);
    vkBindImageMemory(dev, tex_image, tex_mem, 0);

    transitionLayout(dev, cmd_pool, queue, tex_image,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VkImageViewCreateInfo ivCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivCI.image = tex_image; ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivCI.format = VK_FORMAT_R8G8B8A8_UNORM;
    ivCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(dev, &ivCI, nullptr, &tex_view);

    // Update descriptor
    VkDescriptorImageInfo dii{sampler, tex_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet dset{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    dset.dstSet = desc_set; dset.dstBinding = 0; dset.descriptorCount = 1;
    dset.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dset.pImageInfo = &dii;
    vkUpdateDescriptorSets(dev, 1, &dset, 0, nullptr);
  }

  void uploadTexture(const cv::Mat& rgba) {
    auto w = static_cast<uint32_t>(rgba.cols);
    auto h = static_cast<uint32_t>(rgba.rows);
    if (w != tex_w || h != tex_h) buildTexture(w, h);

    std::memcpy(stage_mapped, rgba.data, static_cast<size_t>(w) * h * 4);

    transitionLayout(dev, cmd_pool, queue, tex_image,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkCommandBuffer cb = beginOneShot(dev, cmd_pool);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cb, stage_buf, tex_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    endOneShot(dev, cmd_pool, queue, cb);
    transitionLayout(dev, cmd_pool, queue, tex_image,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
};

// --- Constructor ---

ImageViewer::ImageViewer()
    : impl(std::make_unique<Impl>()) {
  impl->instance = *get_vk_instance();

  // SDL window
  if (!SDL_Init(SDL_INIT_VIDEO))
    THALAMUS_ABORT("SDL_Init: %s", SDL_GetError());
  impl->window = SDL_CreateWindow("Image Viewer", 800, 600,
                                  SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (!impl->window) THALAMUS_ABORT("%s", SDL_GetError());

  // Surface
  if (!SDL_Vulkan_CreateSurface(impl->window, impl->instance, nullptr, &impl->surface))
    THALAMUS_ABORT("%s", SDL_GetError());

  // Physical device
  uint32_t nPhys = 0;
  vkEnumeratePhysicalDevices(impl->instance, &nPhys, nullptr);
  std::vector<VkPhysicalDevice> physDevs(nPhys);
  vkEnumeratePhysicalDevices(impl->instance, &nPhys, physDevs.data());
  impl->phys = physDevs[0];

  // Surface format
  uint32_t nFmt = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(impl->phys, impl->surface, &nFmt, nullptr);
  std::vector<VkSurfaceFormatKHR> fmts(nFmt);
  vkGetPhysicalDeviceSurfaceFormatsKHR(impl->phys, impl->surface, &nFmt, fmts.data());
  impl->surf_fmt = fmts[0];
  for (auto& f : fmts)
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM) { impl->surf_fmt = f; break; }

  // Queue family
  uint32_t nQF = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(impl->phys, &nQF, nullptr);
  std::vector<VkQueueFamilyProperties> qfProps(nQF);
  vkGetPhysicalDeviceQueueFamilyProperties(impl->phys, &nQF, qfProps.data());
  for (uint32_t i = 0; i < nQF; i++) {
    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(impl->phys, i, impl->surface, &present);
    if ((qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
      impl->gfx_family = i; break;
    }
  }
  if (impl->gfx_family == UINT32_MAX)
    THALAMUS_ABORT("No suitable Vulkan queue family");

  // Logical device
  float pri = 1.0f;
  VkDeviceQueueCreateInfo qCI{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qCI.queueFamilyIndex = impl->gfx_family; qCI.queueCount = 1; qCI.pQueuePriorities = &pri;
  const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo devCI{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  devCI.queueCreateInfoCount = 1; devCI.pQueueCreateInfos = &qCI;
  devCI.enabledExtensionCount = 1; devCI.ppEnabledExtensionNames = devExts;
  if (vkCreateDevice(impl->phys, &devCI, nullptr, &impl->dev) != VK_SUCCESS)
    THALAMUS_ABORT("vkCreateDevice failed");
  vkGetDeviceQueue(impl->dev, impl->gfx_family, 0, &impl->queue);

  // Command pool
  VkCommandPoolCreateInfo cpCI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpCI.queueFamilyIndex = impl->gfx_family;
  cpCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  vkCreateCommandPool(impl->dev, &cpCI, nullptr, &impl->cmd_pool);

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
  VkRenderPassCreateInfo rpCI{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpCI.attachmentCount = 1; rpCI.pAttachments = &att;
  rpCI.subpassCount = 1; rpCI.pSubpasses = &sub;
  rpCI.dependencyCount = 1; rpCI.pDependencies = &dep;
  vkCreateRenderPass(impl->dev, &rpCI, nullptr, &impl->render_pass);

  // Sampler
  VkSamplerCreateInfo sampCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampCI.magFilter = VK_FILTER_LINEAR; sampCI.minFilter = VK_FILTER_LINEAR;
  sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  vkCreateSampler(impl->dev, &sampCI, nullptr, &impl->sampler);

  // Descriptor layout + pool + set
  VkDescriptorSetLayoutBinding bind{};
  bind.binding = 0; bind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bind.descriptorCount = 1; bind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo dslCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dslCI.bindingCount = 1; dslCI.pBindings = &bind;
  vkCreateDescriptorSetLayout(impl->dev, &dslCI, nullptr, &impl->desc_layout);

  VkDescriptorPoolSize psz{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
  VkDescriptorPoolCreateInfo dpCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpCI.maxSets = 1; dpCI.poolSizeCount = 1; dpCI.pPoolSizes = &psz;
  vkCreateDescriptorPool(impl->dev, &dpCI, nullptr, &impl->desc_pool);

  VkDescriptorSetAllocateInfo dsAI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dsAI.descriptorPool = impl->desc_pool; dsAI.descriptorSetCount = 1;
  dsAI.pSetLayouts = &impl->desc_layout;
  vkAllocateDescriptorSets(impl->dev, &dsAI, &impl->desc_set);

  // Pipeline
  auto makeShader = [&](std::span<const uint32_t> code) {
    VkShaderModuleCreateInfo smCI{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
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

  VkPipelineVertexInputStateCreateInfo   viState{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo iaState{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  iaState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynState.dynamicStateCount = 2; dynState.pDynamicStates = dynStates;
  VkPipelineViewportStateCreateInfo vpState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vpState.viewportCount = 1; vpState.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rsState{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rsState.polygonMode = VK_POLYGON_MODE_FILL; rsState.cullMode = VK_CULL_MODE_NONE;
  rsState.frontFace = VK_FRONT_FACE_CLOCKWISE; rsState.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo msState{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  msState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState blendAtt{};
  blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cbState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cbState.attachmentCount = 1; cbState.pAttachments = &blendAtt;

  VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plCI.setLayoutCount = 1; plCI.pSetLayouts = &impl->desc_layout;
  vkCreatePipelineLayout(impl->dev, &plCI, nullptr, &impl->pipe_layout);

  VkGraphicsPipelineCreateInfo gpCI{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
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
  VkCommandBufferAllocateInfo cbAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cbAI.commandPool = impl->cmd_pool;
  cbAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbAI.commandBufferCount = Impl::MAX_FRAMES;
  vkAllocateCommandBuffers(impl->dev, &cbAI, impl->cmd_bufs);

  // Sync objects
  VkSemaphoreCreateInfo semCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fenCI{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fenCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (int i = 0; i < Impl::MAX_FRAMES; i++) {
    vkCreateSemaphore(impl->dev, &semCI, nullptr, &impl->image_avail[i]);
    vkCreateFence(impl->dev, &fenCI, nullptr, &impl->in_flight[i]);
  }

  // Initial swapchain
  impl->buildSwapchain();
}

// --- Destructor ---

ImageViewer::~ImageViewer() {
  if (!impl->dev) return;
  vkDeviceWaitIdle(impl->dev);

  impl->destroyTexture();
  for (int i = 0; i < Impl::MAX_FRAMES; i++) {
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
  vkDestroyDevice(impl->dev, nullptr);
  if (impl->window) SDL_DestroyWindow(impl->window);
  SDL_Quit();
}

// --- update ---

void ImageViewer::poll_events() {
  SDL_Event event;
  if (!SDL_PollEvent(&event)) return;
  do {
    if (event.type == SDL_EVENT_WINDOW_RESIZED) impl->needs_recreate = true;
  } while (SDL_PollEvent(&event));
}

void ImageViewer::update(ImageNode* node) {
  if (impl->needs_recreate) {
    impl->recreateSwapchain();
    impl->needs_recreate = false;
  }
  if (impl->extent.width == 0 || impl->extent.height == 0) return;

  // Upload new image if provided
  if (node && node->has_image_data()) {
    cv::Mat rgba = toRGBA(node);
    if (!rgba.empty()) impl->uploadTexture(rgba);
  }

  // Skip render if no texture yet
  if (impl->tex_image == VK_NULL_HANDLE) return;

  int f = impl->frame;
  vkWaitForFences(impl->dev, 1, &impl->in_flight[f], VK_TRUE, UINT64_MAX);

  uint32_t imgIdx;
  VkResult res = vkAcquireNextImageKHR(impl->dev, impl->swapchain, UINT64_MAX,
                                        impl->image_avail[f], VK_NULL_HANDLE, &imgIdx);
  if (res == VK_ERROR_OUT_OF_DATE_KHR) { impl->recreateSwapchain(); return; }
  if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) return;

  vkResetFences(impl->dev, 1, &impl->in_flight[f]);

  VkCommandBuffer cb = impl->cmd_bufs[f];
  vkResetCommandBuffer(cb, 0);
  VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkBeginCommandBuffer(cb, &cbbi);

  VkClearValue clear{};
  VkRenderPassBeginInfo rpBI{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rpBI.renderPass = impl->render_pass;
  rpBI.framebuffer = impl->framebuffers[imgIdx];
  rpBI.renderArea = {{0, 0}, impl->extent};
  rpBI.clearValueCount = 1; rpBI.pClearValues = &clear;
  vkCmdBeginRenderPass(cb, &rpBI, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, impl->pipeline);

  // Aspect-ratio preserving viewport
  float imgAspect = static_cast<float>(impl->tex_w) / static_cast<float>(impl->tex_h);
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
                           impl->pipe_layout, 0, 1, &impl->desc_set, 0, nullptr);
  vkCmdDraw(cb, 3, 1, 0, 0);
  vkCmdEndRenderPass(cb);
  vkEndCommandBuffer(cb);

  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.waitSemaphoreCount = 1; si.pWaitSemaphores = &impl->image_avail[f];
  si.pWaitDstStageMask = &waitStage;
  si.commandBufferCount = 1; si.pCommandBuffers = &cb;
  si.signalSemaphoreCount = 1; si.pSignalSemaphores = &impl->render_done[imgIdx];
  vkQueueSubmit(impl->queue, 1, &si, impl->in_flight[f]);

  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &impl->render_done[imgIdx];
  pi.swapchainCount = 1; pi.pSwapchains = &impl->swapchain; pi.pImageIndices = &imgIdx;
  res = vkQueuePresentKHR(impl->queue, &pi);
  if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
    impl->needs_recreate = true;

  impl->frame = (f + 1) % Impl::MAX_FRAMES;
}

} // namespace thalamus
