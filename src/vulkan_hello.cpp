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

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <texture.vert.spv.h>
#include <texture.frag.spv.h>

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    const char* prefix = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)   ? "ERROR"   :
                         (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? "WARNING" :
                         (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)    ? "INFO"    : "VERBOSE";
    std::cerr << "[Vulkan " << prefix << "] " << data->pMessage << '\n';
    return VK_FALSE;
}

static bool layerAvailable(const char* name) {
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> layers(n);
    vkEnumerateInstanceLayerProperties(&n, layers.data());
    for (auto& l : layers)
        if (std::strcmp(l.layerName, name) == 0) return true;
    return false;
}

static uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("No suitable memory type");
}

static void createBuffer(VkDevice dev, VkPhysicalDevice phys, VkDeviceSize size,
                         VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                         VkBuffer& buf, VkDeviceMemory& mem) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(dev, &bi, nullptr, &buf);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, buf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(phys, req.memoryTypeBits, props);
    vkAllocateMemory(dev, &ai, nullptr, &mem);
    vkBindBufferMemory(dev, buf, mem, 0);
}

static VkCommandBuffer beginOneShot(VkDevice dev, VkCommandPool pool) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
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
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(dev, pool, 1, &cb);
}

static void transitionLayout(VkDevice dev, VkCommandPool pool, VkQueue queue,
                              VkImage img, VkImageLayout from, VkImageLayout to) {
    VkCommandBuffer cb = beginOneShot(dev, pool);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkPipelineStageFlags src, dst;
    if (from == VK_IMAGE_LAYOUT_UNDEFINED) {
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    vkCmdPipelineBarrier(cb, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
    endOneShot(dev, pool, queue, cb);
}

int main(int, char*[]) {
    // Load texture
    cv::Mat img = cv::imread("texture.jpg");
    if (img.empty()) throw std::runtime_error("Failed to load texture.jpg");
    cv::cvtColor(img, img, cv::COLOR_BGR2RGBA);
    uint32_t texW = static_cast<uint32_t>(img.cols);
    uint32_t texH = static_cast<uint32_t>(img.rows);

    // SDL
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
    SDL_Window* window = SDL_CreateWindow("Vulkan Texture", 800, 600, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) { SDL_Quit(); throw std::runtime_error(SDL_GetError()); }

    // Instance
    Uint32 extCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&extCount);
    std::vector<const char*> extensions(sdlExts, sdlExts + extCount);

    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    bool validationEnabled = layerAvailable(validationLayer);
    if (validationEnabled) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        std::cerr << "[Vulkan] Validation layer enabled\n";
    } else {
        std::cerr << "[Vulkan] Validation layer not available\n";
    }

    VkDebugUtilsMessengerCreateInfoEXT messengerCI{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    messengerCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
    messengerCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerCI.pfnUserCallback = debugCallback;

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo instCI{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instCI.pApplicationInfo = &appInfo;
    instCI.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instCI.ppEnabledExtensionNames = extensions.data();
    if (validationEnabled) {
        instCI.enabledLayerCount = 1;
        instCI.ppEnabledLayerNames = &validationLayer;
        instCI.pNext = &messengerCI;  // catch messages during instance create/destroy
    }
    VkInstance instance;
    if (vkCreateInstance(&instCI, nullptr, &instance) != VK_SUCCESS)
        throw std::runtime_error("vkCreateInstance failed");

    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    if (validationEnabled) {
        auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (fn) fn(instance, &messengerCI, nullptr, &messenger);
    }

    VkSurfaceKHR surface;
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
        throw std::runtime_error(SDL_GetError());

    // Physical device
    uint32_t nPhys = 0;
    vkEnumeratePhysicalDevices(instance, &nPhys, nullptr);
    std::vector<VkPhysicalDevice> physDevs(nPhys);
    vkEnumeratePhysicalDevices(instance, &nPhys, physDevs.data());
    VkPhysicalDevice phys = physDevs[0];

    // Surface format
    uint32_t nFmt = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &nFmt, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(nFmt);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &nFmt, fmts.data());
    VkSurfaceFormatKHR surfFmt = fmts[0];
    for (auto& f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM) { surfFmt = f; break; }

    // Queue family (graphics + present)
    uint32_t nQF = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nQF, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(nQF);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nQF, qfProps.data());
    uint32_t gfxFamily = UINT32_MAX;
    for (uint32_t i = 0; i < nQF; i++) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &present);
        if ((qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { gfxFamily = i; break; }
    }
    if (gfxFamily == UINT32_MAX) throw std::runtime_error("No suitable queue family");

    // Logical device
    float qPri = 1.0f;
    VkDeviceQueueCreateInfo qCI{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qCI.queueFamilyIndex = gfxFamily;
    qCI.queueCount = 1;
    qCI.pQueuePriorities = &qPri;
    const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo devCI{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    devCI.queueCreateInfoCount = 1;
    devCI.pQueueCreateInfos = &qCI;
    devCI.enabledExtensionCount = 1;
    devCI.ppEnabledExtensionNames = devExts;
    VkDevice dev;
    if (vkCreateDevice(phys, &devCI, nullptr, &dev) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDevice failed");
    VkQueue queue;
    vkGetDeviceQueue(dev, gfxFamily, 0, &queue);

    // Command pool
    VkCommandPoolCreateInfo cpCI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpCI.queueFamilyIndex = gfxFamily;
    cpCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool cmdPool;
    vkCreateCommandPool(dev, &cpCI, nullptr, &cmdPool);

    // Upload texture
    VkDeviceSize imgBytes = static_cast<VkDeviceSize>(img.total() * img.elemSize());
    VkBuffer stageBuf; VkDeviceMemory stageMem;
    createBuffer(dev, phys, imgBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stageBuf, stageMem);
    void* mapped;
    vkMapMemory(dev, stageMem, 0, imgBytes, 0, &mapped);
    std::memcpy(mapped, img.data, imgBytes);
    vkUnmapMemory(dev, stageMem);

    VkImageCreateInfo texCI{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    texCI.imageType = VK_IMAGE_TYPE_2D;
    texCI.format = VK_FORMAT_R8G8B8A8_UNORM;
    texCI.extent = {texW, texH, 1};
    texCI.mipLevels = 1;
    texCI.arrayLayers = 1;
    texCI.samples = VK_SAMPLE_COUNT_1_BIT;
    texCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    texCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    texCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage texImage; VkDeviceMemory texMem;
    vkCreateImage(dev, &texCI, nullptr, &texImage);
    VkMemoryRequirements texReq;
    vkGetImageMemoryRequirements(dev, texImage, &texReq);
    VkMemoryAllocateInfo texAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    texAlloc.allocationSize = texReq.size;
    texAlloc.memoryTypeIndex = findMemoryType(phys, texReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(dev, &texAlloc, nullptr, &texMem);
    vkBindImageMemory(dev, texImage, texMem, 0);

    transitionLayout(dev, cmdPool, queue, texImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkCommandBuffer copyCB = beginOneShot(dev, cmdPool);
    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageExtent = {texW, texH, 1};
    vkCmdCopyBufferToImage(copyCB, stageBuf, texImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    endOneShot(dev, cmdPool, queue, copyCB);
    transitionLayout(dev, cmdPool, queue, texImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(dev, stageBuf, nullptr);
    vkFreeMemory(dev, stageMem, nullptr);

    VkImageViewCreateInfo texIVCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    texIVCI.image = texImage;
    texIVCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    texIVCI.format = VK_FORMAT_R8G8B8A8_UNORM;
    texIVCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView texView;
    vkCreateImageView(dev, &texIVCI, nullptr, &texView);

    VkSamplerCreateInfo sampCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampCI.magFilter = VK_FILTER_LINEAR;
    sampCI.minFilter = VK_FILTER_LINEAR;
    sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler sampler;
    vkCreateSampler(dev, &sampCI, nullptr, &sampler);

    // Descriptor layout + pool + set
    VkDescriptorSetLayoutBinding dslBinding{};
    dslBinding.binding = 0;
    dslBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dslBinding.descriptorCount = 1;
    dslBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslCI.bindingCount = 1;
    dslCI.pBindings = &dslBinding;
    VkDescriptorSetLayout descLayout;
    vkCreateDescriptorSetLayout(dev, &dslCI, nullptr, &descLayout);

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo dpCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpCI.maxSets = 1;
    dpCI.poolSizeCount = 1;
    dpCI.pPoolSizes = &poolSize;
    VkDescriptorPool descPool;
    vkCreateDescriptorPool(dev, &dpCI, nullptr, &descPool);

    VkDescriptorSetAllocateInfo dsAI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsAI.descriptorPool = descPool;
    dsAI.descriptorSetCount = 1;
    dsAI.pSetLayouts = &descLayout;
    VkDescriptorSet descSet;
    vkAllocateDescriptorSets(dev, &dsAI, &descSet);

    VkDescriptorImageInfo descImgInfo{sampler, texView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet dsWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    dsWrite.dstSet = descSet;
    dsWrite.dstBinding = 0;
    dsWrite.descriptorCount = 1;
    dsWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dsWrite.pImageInfo = &descImgInfo;
    vkUpdateDescriptorSets(dev, 1, &dsWrite, 0, nullptr);

    // Render pass
    VkAttachmentDescription colorAtt{};
    colorAtt.format = surfFmt.format;
    colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpCI{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpCI.attachmentCount = 1;
    rpCI.pAttachments = &colorAtt;
    rpCI.subpassCount = 1;
    rpCI.pSubpasses = &subpass;
    rpCI.dependencyCount = 1;
    rpCI.pDependencies = &dep;
    VkRenderPass renderPass;
    vkCreateRenderPass(dev, &rpCI, nullptr, &renderPass);

    // Swapchain state — rebuilt on resize
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent{};
    std::vector<VkImage> scImages;
    std::vector<VkImageView> scViews;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkSemaphore> renderDone;  // one per swapchain image, not per frame

    auto destroySwapchainDeps = [&]() {
        for (auto fb : framebuffers) vkDestroyFramebuffer(dev, fb, nullptr);
        framebuffers.clear();
        for (auto iv : scViews) vkDestroyImageView(dev, iv, nullptr);
        scViews.clear();
        for (auto s : renderDone) vkDestroySemaphore(dev, s, nullptr);
        renderDone.clear();
    };

    auto buildSwapchain = [&]() -> bool {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
        if (caps.currentExtent.width == UINT32_MAX) {
            // Surface has flexible extent — ask the window for its pixel size
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            extent.width  = std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width,  caps.maxImageExtent.width);
            extent.height = std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height);
        } else {
            extent = caps.currentExtent;
        }
        if (extent.width == 0 || extent.height == 0)
            return false;

        VkSwapchainCreateInfoKHR scCI{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        scCI.surface = surface;
        scCI.minImageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && scCI.minImageCount > caps.maxImageCount)
            scCI.minImageCount = caps.maxImageCount;
        scCI.imageFormat = surfFmt.format;
        scCI.imageColorSpace = surfFmt.colorSpace;
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
        if (vkCreateSwapchainKHR(dev, &scCI, nullptr, &newSC) != VK_SUCCESS)
            return false;
        if (swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(dev, swapchain, nullptr);
        swapchain = newSC;

        uint32_t nImg = 0;
        vkGetSwapchainImagesKHR(dev, swapchain, &nImg, nullptr);
        scImages.resize(nImg);
        vkGetSwapchainImagesKHR(dev, swapchain, &nImg, scImages.data());

        scViews.resize(nImg);
        for (uint32_t i = 0; i < nImg; i++) {
            VkImageViewCreateInfo ivCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            ivCI.image = scImages[i];
            ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ivCI.format = surfFmt.format;
            ivCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCreateImageView(dev, &ivCI, nullptr, &scViews[i]);
        }

        framebuffers.resize(nImg);
        for (uint32_t i = 0; i < nImg; i++) {
            VkFramebufferCreateInfo fbCI{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fbCI.renderPass = renderPass;
            fbCI.attachmentCount = 1;
            fbCI.pAttachments = &scViews[i];
            fbCI.width = extent.width;
            fbCI.height = extent.height;
            fbCI.layers = 1;
            vkCreateFramebuffer(dev, &fbCI, nullptr, &framebuffers[i]);
        }

        renderDone.resize(nImg);
        VkSemaphoreCreateInfo rdSemCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (uint32_t i = 0; i < nImg; i++)
            vkCreateSemaphore(dev, &rdSemCI, nullptr, &renderDone[i]);

        return true;
    };

    auto recreateSwapchain = [&]() {
        vkDeviceWaitIdle(dev);
        destroySwapchainDeps();
        buildSwapchain();
    };

    buildSwapchain();

    // Pipeline with dynamic viewport/scissor
    auto makeShader = [&](auto&& code) {
        VkShaderModuleCreateInfo smCI{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        smCI.codeSize = std::size(code) * sizeof(uint32_t);
        smCI.pCode = code;
        VkShaderModule sm;
        vkCreateShaderModule(dev, &smCI, nullptr, &sm);
        return sm;
    };
    VkShaderModule vertSM = makeShader(vert_shader_data);
    VkShaderModule fragSM = makeShader(frag_shader_data);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertSM;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragSM;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo viState{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo iaState{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    iaState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dynStates;

    VkPipelineViewportStateCreateInfo vpState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vpState.viewportCount = 1;
    vpState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rsState{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rsState.polygonMode = VK_POLYGON_MODE_FILL;
    rsState.cullMode = VK_CULL_MODE_NONE;
    rsState.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rsState.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo msState{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    msState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cbState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cbState.attachmentCount = 1;
    cbState.pAttachments = &blendAtt;

    VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plCI.setLayoutCount = 1;
    plCI.pSetLayouts = &descLayout;
    VkPipelineLayout pipeLayout;
    vkCreatePipelineLayout(dev, &plCI, nullptr, &pipeLayout);

    VkGraphicsPipelineCreateInfo gpCI{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpCI.stageCount = 2;
    gpCI.pStages = stages;
    gpCI.pVertexInputState = &viState;
    gpCI.pInputAssemblyState = &iaState;
    gpCI.pViewportState = &vpState;
    gpCI.pRasterizationState = &rsState;
    gpCI.pMultisampleState = &msState;
    gpCI.pColorBlendState = &cbState;
    gpCI.pDynamicState = &dynState;
    gpCI.layout = pipeLayout;
    gpCI.renderPass = renderPass;
    VkPipeline pipeline;
    vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpCI, nullptr, &pipeline);

    vkDestroyShaderModule(dev, vertSM, nullptr);
    vkDestroyShaderModule(dev, fragSM, nullptr);

    // Command buffers (one per frame in flight)
    constexpr int MAX_FRAMES = 2;
    VkCommandBuffer cmdBufs[MAX_FRAMES];
    VkCommandBufferAllocateInfo cbAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbAI.commandPool = cmdPool;
    cbAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAI.commandBufferCount = MAX_FRAMES;
    vkAllocateCommandBuffers(dev, &cbAI, cmdBufs);

    // Sync objects
    VkSemaphore imageAvail[MAX_FRAMES];
    VkFence inFlight[MAX_FRAMES];
    VkSemaphoreCreateInfo semCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenCI{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < MAX_FRAMES; i++) {
        vkCreateSemaphore(dev, &semCI, nullptr, &imageAvail[i]);
        vkCreateFence(dev, &fenCI, nullptr, &inFlight[i]);
    }

    // Render loop
    int frame = 0;
    bool running = true;
    bool needsRecreate = false;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) running = false;
            if (event.type == SDL_EVENT_WINDOW_RESIZED) needsRecreate = true;
        }

        if (needsRecreate) {
            recreateSwapchain();
            needsRecreate = false;
        }

        // Skip rendering if window is minimized
        if (extent.width == 0 || extent.height == 0) continue;

        vkWaitForFences(dev, 1, &inFlight[frame], VK_TRUE, UINT64_MAX);

        uint32_t imgIdx;
        VkResult acquireRes = vkAcquireNextImageKHR(dev, swapchain, UINT64_MAX,
                                                     imageAvail[frame], VK_NULL_HANDLE, &imgIdx);
        if (acquireRes == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            continue;
        }
        if (acquireRes != VK_SUCCESS && acquireRes != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("vkAcquireNextImageKHR failed");

        vkResetFences(dev, 1, &inFlight[frame]);

        VkCommandBuffer cb = cmdBufs[frame];
        vkResetCommandBuffer(cb, 0);
        VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(cb, &cbbi);

        VkClearValue clear{};
        VkRenderPassBeginInfo rpBI{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpBI.renderPass = renderPass;
        rpBI.framebuffer = framebuffers[imgIdx];
        rpBI.renderArea = {{0, 0}, extent};
        rpBI.clearValueCount = 1;
        rpBI.pClearValues = &clear;
        vkCmdBeginRenderPass(cb, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        float imgAspect = static_cast<float>(texW) / static_cast<float>(texH);
        float winAspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        float vpW, vpH, vpX, vpY;
        if (winAspect > imgAspect) {
            vpH = static_cast<float>(extent.height);
            vpW = vpH * imgAspect;
            vpX = (static_cast<float>(extent.width) - vpW) / 2.0f;
            vpY = 0.0f;
        } else {
            vpW = static_cast<float>(extent.width);
            vpH = vpW / imgAspect;
            vpX = 0.0f;
            vpY = (static_cast<float>(extent.height) - vpH) / 2.0f;
        }
        VkViewport vp{vpX, vpY, vpW, vpH, 0.0f, 1.0f};
        VkRect2D scissor{{static_cast<int32_t>(vpX), static_cast<int32_t>(vpY)},
                         {static_cast<uint32_t>(vpW), static_cast<uint32_t>(vpH)}};
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &scissor);

        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout, 0, 1, &descSet, 0, nullptr);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRenderPass(cb);
        vkEndCommandBuffer(cb);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &imageAvail[frame];
        si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &renderDone[imgIdx];
        vkQueueSubmit(queue, 1, &si, inFlight[frame]);

        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &renderDone[imgIdx];
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain;
        pi.pImageIndices = &imgIdx;
        VkResult presentRes = vkQueuePresentKHR(queue, &pi);
        if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR)
            needsRecreate = true;
        else if (presentRes != VK_SUCCESS)
            throw std::runtime_error("vkQueuePresentKHR failed");

        frame = (frame + 1) % MAX_FRAMES;
    }

    vkDeviceWaitIdle(dev);

    for (int i = 0; i < MAX_FRAMES; i++) {
        vkDestroySemaphore(dev, imageAvail[i], nullptr);
        vkDestroyFence(dev, inFlight[i], nullptr);
    }
    vkDestroyPipeline(dev, pipeline, nullptr);
    vkDestroyPipelineLayout(dev, pipeLayout, nullptr);
    vkDestroyDescriptorPool(dev, descPool, nullptr);
    vkDestroyDescriptorSetLayout(dev, descLayout, nullptr);
    vkDestroySampler(dev, sampler, nullptr);
    vkDestroyImageView(dev, texView, nullptr);
    vkDestroyImage(dev, texImage, nullptr);
    vkFreeMemory(dev, texMem, nullptr);
    destroySwapchainDeps();
    vkDestroyRenderPass(dev, renderPass, nullptr);
    vkDestroyCommandPool(dev, cmdPool, nullptr);
    vkDestroySwapchainKHR(dev, swapchain, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDevice(dev, nullptr);
    if (messenger != VK_NULL_HANDLE) {
        auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (fn) fn(instance, messenger, nullptr);
    }
    vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
