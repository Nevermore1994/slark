//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AndroidVulkanContext.h"

#include "Log.hpp"
#include "VulkanGLUtil.h"
#include "VulkanShaders.h"

#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#include <algorithm>
#include <android/hardware_buffer.h>
#include <array>
#include <cmath>
#include <functional>
#include <set>
#include <vector>

namespace slark {
AndroidVulkanContext::~AndroidVulkanContext() noexcept {
    release();
}

bool AndroidVulkanContext::init(ANativeWindow* window,
                                int32_t width,
                                int32_t height,
                                void* sharedEGLContext) noexcept {
    if (!window) {
        LogE("AndroidVulkanContext::init: window is null");
        return false;
    }
    frameWidth_ = width;
    frameHeight_ = height;

    using Step = std::function<bool()>;
    // clang-format off
    const std::initializer_list<Step> steps = {
        [&] { return createInstance();            },
        [&] { return createSurface(window);       },
        [&] { return selectPhysicalDevice();      },
        [&] { return createLogicalDevice();       },
        [&] { return createSwapchain(width, height); },
        [&] { return createRenderPass();          },
        [&] { return createDescriptorSetLayout(); },
        [&] { return createPipeline();            },
        [&] { return createFramebuffers();        },
        [&] { return createCommandPool();         },
        [&] { return createCommandBuffers();      },
        [&] { return createSyncObjects();         },
        [&] { return createDescriptorPool();      },
        [&] { return createSampler();             },
        [&] { return initEGLInterop(sharedEGLContext); },
    };
    // clang-format on
    isInit_ = std::all_of(steps.begin(), steps.end(), [](const Step& step) {
        return step();
    });
    if (!isInit_) {
        return false;
    }
    LogI("AndroidVulkanContext initialized: {}x{}", width, height);
    return true;
}

bool AndroidVulkanContext::recreateSwapchain(ANativeWindow* window,
                                             int32_t width,
                                             int32_t height) noexcept {
    if (!isInit_) {
        return false;
    }
    vkDeviceWaitIdle(device_);

    destroySwapchainResources();

    if (window) {
        // Surface 重建：先销毁旧 surface，再用新 window 创建
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
        if (!createSurface(window)) {
            return false;
        }
    }

    int32_t newW = (width > 0) ? width : frameWidth_;
    int32_t newH = (height > 0) ? height : frameHeight_;
    frameWidth_ = newW;
    frameHeight_ = newH;

    if (!createSwapchain(newW, newH)) {
        return false;
    }
    if (!createFramebuffers()) {
        return false;
    }
    if (!createCommandBuffers()) {
        return false;
    }

    // 重建时同步清理 EGL 侧资源（fbo/fboTexture/eglImage 绑定了旧 hardwareBuffer）
    if (hardwareBuffer_ || fbo_ || eglImage_ != EGL_NO_IMAGE_KHR) {
        if (fbo_) {
            glDeleteFramebuffers(1, &fbo_);
            fbo_ = 0;
        }
        if (fboTexture_) {
            glDeleteTextures(1, &fboTexture_);
            fboTexture_ = 0;
        }
        if (eglImage_ != EGL_NO_IMAGE_KHR && fnEGLDestroyImage_ && eglContext_) {
            fnEGLDestroyImage_(eglContext_->getDisplay(), eglImage_);
            eglImage_ = EGL_NO_IMAGE_KHR;
        }
        destroyHardwareBufferResources();
    }

    LogI("AndroidVulkanContext swapchain recreated: {}x{}", newW, newH);
    return true;
}

void AndroidVulkanContext::release() noexcept {
    if (!isInit_ && instance_ == VK_NULL_HANDLE) {
        return;
    }
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    destroyEGLInterop();
    destroyHardwareBufferResources();
    destroySwapchainResources();
    destroyDeviceResources();
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    isInit_ = false;
}

bool AndroidVulkanContext::createInstance() noexcept {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Slark";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    const std::array<const char*, 2> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS) {
        LogE("vkCreateInstance failed");
        return false;
    }
    return true;
}

bool AndroidVulkanContext::createSurface(ANativeWindow* window) noexcept {
    VkAndroidSurfaceCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    ci.window = window;
    if (vkCreateAndroidSurfaceKHR(instance_, &ci, nullptr, &surface_) != VK_SUCCESS) {
        LogE("vkCreateAndroidSurfaceKHR failed");
        return false;
    }
    return true;
}

bool AndroidVulkanContext::selectPhysicalDevice() noexcept {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        LogE("no Vulkan physical device found");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    auto findQueues = [&](VkPhysicalDevice dev, uint32_t& gfx, uint32_t& pres) {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qProps.data());
        for (uint32_t i = 0; i < qCount; ++i) {
            if (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                gfx = i;
            }
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &presentSupport);
            if (presentSupport) {
                pres = i;
            }
            if (gfx != UINT32_MAX && pres != UINT32_MAX) {
                return;
            }
        }
    };

    for (auto& dev : devices) {
        uint32_t gfx = UINT32_MAX, pres = UINT32_MAX;
        findQueues(dev, gfx, pres);
        if (gfx != UINT32_MAX && pres != UINT32_MAX) {
            physicalDevice_ = dev;
            graphicsQueueFamily_ = gfx;
            presentQueueFamily_ = pres;
            return true;
        }
    }
    LogE("no suitable Vulkan physical device");
    return false;
}

bool AndroidVulkanContext::createLogicalDevice() noexcept {
    const float priority = 1.0f;
    // 用 set 去重，避免为同一 family 创建重复的 QueueCreateInfo
    const std::set<uint32_t> uniqueFamilies = {graphicsQueueFamily_, presentQueueFamily_};
    std::vector<VkDeviceQueueCreateInfo> queueCIs;
    queueCIs.reserve(uniqueFamilies.size());
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        queueCIs.push_back(qi);
    }

    const std::array<const char*, 3> devExts = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
    };

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = static_cast<uint32_t>(queueCIs.size());
    ci.pQueueCreateInfos = queueCIs.data();
    ci.enabledExtensionCount = static_cast<uint32_t>(devExts.size());
    ci.ppEnabledExtensionNames = devExts.data();

    if (vkCreateDevice(physicalDevice_, &ci, nullptr, &device_) != VK_SUCCESS) {
        LogE("vkCreateDevice failed");
        return false;
    }
    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentQueueFamily_, 0, &presentQueue_);
    return true;
}

bool AndroidVulkanContext::createSwapchain(int32_t width, int32_t height) noexcept {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, nullptr);
    if (fmtCount == 0) {
        LogE("no surface formats available");
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, formats.data());

    // 优先选 BGRA8_SRGB，否则取第一个
    VkSurfaceFormatKHR chosen = formats[0];
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    swapchainFormat_ = chosen.format;

    swapchainExtent_.width = static_cast<uint32_t>(
        std::max(static_cast<int32_t>(caps.minImageExtent.width),
                 std::min(width, static_cast<int32_t>(caps.maxImageExtent.width))));
    swapchainExtent_.height = static_cast<uint32_t>(
        std::max(static_cast<int32_t>(caps.minImageExtent.height),
                 std::min(height, static_cast<int32_t>(caps.maxImageExtent.height))));

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) {
        imgCount = std::min(imgCount, caps.maxImageCount);
    }

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface_;
    ci.minImageCount = imgCount;
    ci.imageFormat = swapchainFormat_;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = swapchainExtent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;

    if (graphicsQueueFamily_ != presentQueueFamily_) {
        std::array<uint32_t, 2> families = {graphicsQueueFamily_, presentQueueFamily_};
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = families.data();
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS) {
        LogE("vkCreateSwapchainKHR failed");
        return false;
    }

    uint32_t cnt = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &cnt, nullptr);
    swapchainImages_.resize(cnt);
    vkGetSwapchainImagesKHR(device_, swapchain_, &cnt, swapchainImages_.data());

    swapchainImageViews_.resize(cnt);
    for (uint32_t i = 0; i < cnt; ++i) {
        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = swapchainImages_[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = swapchainFormat_;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_, &ivci, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS) {
            LogE("vkCreateImageView failed at index {}", i);
            return false;
        }
    }
    return true;
}

bool AndroidVulkanContext::createRenderPass() noexcept {
    VkAttachmentDescription colorAttach{};
    colorAttach.format = swapchainFormat_;
    colorAttach.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttach.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &colorAttach;
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;

    if (vkCreateRenderPass(device_, &ci, nullptr, &renderPass_) != VK_SUCCESS) {
        LogE("vkCreateRenderPass failed");
        return false;
    }
    return true;
}

bool AndroidVulkanContext::createDescriptorSetLayout() noexcept {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 1;
    ci.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(device_, &ci, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        LogE("vkCreateDescriptorSetLayout failed");
        return false;
    }
    return true;
}

bool AndroidVulkanContext::createPipeline() noexcept {
    auto vertMod = createShaderModule(device_, kVertSpirv, sizeof(kVertSpirv));
    auto fragMod = createShaderModule(device_, kFragSpirv, sizeof(kFragSpirv));
    if (!vertMod || !fragMod) {
        LogE("failed to create shader modules");
        if (vertMod) {
            vkDestroyShaderModule(device_, vertMod, nullptr);
        }
        if (fragMod) {
            vkDestroyShaderModule(device_, fragMod, nullptr);
        }
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertInput{};
    vertInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{0,
                        0,
                        static_cast<float>(swapchainExtent_.width),
                        static_cast<float>(swapchainExtent_.height),
                        0.0f,
                        1.0f};
    VkRect2D scissor{{0, 0}, swapchainExtent_};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttach;

    VkPipelineLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCI.setLayoutCount = 1;
    layoutCI.pSetLayouts = &descriptorSetLayout_;

    if (vkCreatePipelineLayout(device_, &layoutCI, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        LogE("vkCreatePipelineLayout failed");
        vkDestroyShaderModule(device_, vertMod, nullptr);
        vkDestroyShaderModule(device_, fragMod, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.stageCount = static_cast<uint32_t>(stages.size());
    pipelineCI.pStages = stages.data();
    pipelineCI.pVertexInputState = &vertInput;
    pipelineCI.pInputAssemblyState = &inputAssembly;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pRasterizationState = &raster;
    pipelineCI.pMultisampleState = &msaa;
    pipelineCI.pColorBlendState = &blend;
    pipelineCI.layout = pipelineLayout_;
    pipelineCI.renderPass = renderPass_;
    pipelineCI.subpass = 0;

    auto result =
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline_);
    vkDestroyShaderModule(device_, vertMod, nullptr);
    vkDestroyShaderModule(device_, fragMod, nullptr);
    if (result != VK_SUCCESS) {
        LogE("vkCreateGraphicsPipelines failed");
        return false;
    }
    return true;
}

bool AndroidVulkanContext::createFramebuffers() noexcept {
    swapchainFramebuffers_.resize(swapchainImageViews_.size());
    for (size_t i = 0; i < swapchainImageViews_.size(); ++i) {
        VkFramebufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass = renderPass_;
        ci.attachmentCount = 1;
        ci.pAttachments = &swapchainImageViews_[i];
        ci.width = swapchainExtent_.width;
        ci.height = swapchainExtent_.height;
        ci.layers = 1;
        if (vkCreateFramebuffer(device_, &ci, nullptr, &swapchainFramebuffers_[i]) != VK_SUCCESS) {
            LogE("vkCreateFramebuffer failed at index {}", i);
            return false;
        }
    }
    return true;
}

bool AndroidVulkanContext::createCommandPool() noexcept {
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = graphicsQueueFamily_;
    if (vkCreateCommandPool(device_, &ci, nullptr, &commandPool_) != VK_SUCCESS) {
        LogE("vkCreateCommandPool failed");
        return false;
    }
    return true;
}

bool AndroidVulkanContext::createCommandBuffers() noexcept {
    commandBuffers_.resize(swapchainImages_.size());
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = commandPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    if (vkAllocateCommandBuffers(device_, &ai, commandBuffers_.data()) != VK_SUCCESS) {
        LogE("vkAllocateCommandBuffers failed");
        return false;
    }
    return true;
}

bool AndroidVulkanContext::createSyncObjects() noexcept {
    imageAvailableSemaphores_.resize(kMaxFramesInFlight);
    renderFinishedSemaphores_.resize(kMaxFramesInFlight);
    inFlightFences_.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (vkCreateSemaphore(device_, &sci, nullptr, &imageAvailableSemaphores_[i]) !=
                VK_SUCCESS ||
            vkCreateSemaphore(device_, &sci, nullptr, &renderFinishedSemaphores_[i]) !=
                VK_SUCCESS ||
            vkCreateFence(device_, &fci, nullptr, &inFlightFences_[i]) != VK_SUCCESS) {
            LogE("failed to create sync objects at index {}", i);
            return false;
        }
    }
    return true;
}

bool AndroidVulkanContext::createDescriptorPool() noexcept {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets = 1;
    ci.poolSizeCount = 1;
    ci.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(device_, &ci, nullptr, &descriptorPool_) != VK_SUCCESS) {
        LogE("vkCreateDescriptorPool failed");
        return false;
    }

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = descriptorPool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &descriptorSetLayout_;
    if (vkAllocateDescriptorSets(device_, &ai, &descriptorSet_) != VK_SUCCESS) {
        LogE("vkAllocateDescriptorSets failed");
        return false;
    }
    return true;
}

bool AndroidVulkanContext::createSampler() noexcept {
    VkSamplerCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter = VK_FILTER_LINEAR;
    ci.minFilter = VK_FILTER_LINEAR;
    ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device_, &ci, nullptr, &sampler_) != VK_SUCCESS) {
        LogE("vkCreateSampler failed");
        return false;
    }
    return true;
}

bool AndroidVulkanContext::initEGLInterop(void* sharedEGLContext) noexcept {
    eglContext_ = std::make_shared<AndroidEGLContext>();
    if (!eglContext_->init(sharedEGLContext)) {
        LogE("EGL interop context init failed (sharedEGLContext={})",
             sharedEGLContext ? "provided" : "nullptr");
        eglContext_.reset();
        return false;
    }
    // 创建离屏 pbuffer surface 用于 EGL 上下文激活
    auto pbuffer = eglContext_->createOffscreenSurface(1, 1);
    if (!pbuffer) {
        LogE("EGL pbuffer surface creation failed");
        return false;
    }
    eglPbuffer_ = pbuffer;
    // 绑定到当前线程以便后续 loadEGLExtensions 和 GL 初始化
    eglContext_->attachContext(eglPbuffer_);
    if (!loadEGLExtensions()) {
        return false;
    }

    oesProgram_ = createGLProgram(kOESVertexShader, kOESFragmentShader);
    if (!oesProgram_) {
        LogE("OES GL program creation failed");
        return false;
    }
    oesPosAttr_ = glGetAttribLocation(oesProgram_, "aPosition");
    oesTexCoordAttr_ = glGetAttribLocation(oesProgram_, "aTexCoord");
    oesTexUniform_ = glGetUniformLocation(oesProgram_, "uTexture");
    return true;
}

bool AndroidVulkanContext::loadEGLExtensions() noexcept {
    fnEGLGetNativeClientBuffer_ = reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
        eglGetProcAddress("eglGetNativeClientBufferANDROID"));
    fnEGLCreateImage_ =
        reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    fnEGLDestroyImage_ =
        reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    fnGLEGLImageTargetTexture2D_ = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    fnVkGetHWBProperties_ = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
        vkGetDeviceProcAddr(device_, "vkGetAndroidHardwareBufferPropertiesANDROID"));

    if (!fnEGLGetNativeClientBuffer_ || !fnEGLCreateImage_ || !fnEGLDestroyImage_ ||
        !fnGLEGLImageTargetTexture2D_ || !fnVkGetHWBProperties_) {
        LogE("required EGL/Vulkan extensions not available");
        return false;
    }
    return true;
}

bool AndroidVulkanContext::allocateHardwareBuffer(int32_t width, int32_t height) noexcept {
    AHardwareBuffer_Desc desc{};
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                 AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;
    if (AHardwareBuffer_allocate(&desc, &hardwareBuffer_) != 0) {
        LogE("AHardwareBuffer_allocate failed: {}x{}", width, height);
        return false;
    }
    return true;
}

bool AndroidVulkanContext::createEGLImageFromHWB() noexcept {
    EGLClientBuffer clientBuffer = fnEGLGetNativeClientBuffer_(hardwareBuffer_);
    const EGLint attrs[] = {EGL_NONE};
    eglImage_ = fnEGLCreateImage_(eglContext_->getDisplay(),
                                  EGL_NO_CONTEXT,
                                  EGL_NATIVE_BUFFER_ANDROID,
                                  clientBuffer,
                                  attrs);
    if (eglImage_ == EGL_NO_IMAGE_KHR) {
        LogE("eglCreateImageKHR failed");
        return false;
    }
    return true;
}

bool AndroidVulkanContext::createFBOForEGLImage() noexcept {
    glGenTextures(1, &fboTexture_);
    glBindTexture(GL_TEXTURE_2D, fboTexture_);
    fnGLEGLImageTargetTexture2D_(GL_TEXTURE_2D, eglImage_);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTexture_, 0);
    const bool complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        LogE("FBO for AHardwareBuffer is not complete");
        return false;
    }
    return true;
}

bool AndroidVulkanContext::createHWBVkImage() noexcept {
    VkAndroidHardwareBufferPropertiesANDROID hwbProps{};
    hwbProps.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    VkAndroidHardwareBufferFormatPropertiesANDROID hwbFmtProps{};
    hwbFmtProps.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;
    hwbProps.pNext = &hwbFmtProps;

    if (fnVkGetHWBProperties_(device_, hardwareBuffer_, &hwbProps) != VK_SUCCESS) {
        LogE("vkGetAndroidHardwareBufferPropertiesANDROID failed");
        return false;
    }
    // 部分驱动对 RGBA8 格式返回 VK_FORMAT_UNDEFINED，fallback 到 R8G8B8A8_UNORM
    hwbVkFormat_ =
        (hwbFmtProps.format != VK_FORMAT_UNDEFINED) ? hwbFmtProps.format : VK_FORMAT_R8G8B8A8_UNORM;
    hwbMemoryTypeBits_ = hwbProps.memoryTypeBits;
    hwbAllocationSize_ = hwbProps.allocationSize;

    AHardwareBuffer_Desc hwbDesc{};
    AHardwareBuffer_describe(hardwareBuffer_, &hwbDesc);

    VkExternalMemoryImageCreateInfo extMemCI{};
    extMemCI.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    extMemCI.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

    VkImageCreateInfo imgCI{};
    imgCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCI.pNext = &extMemCI;
    imgCI.imageType = VK_IMAGE_TYPE_2D;
    imgCI.format = hwbVkFormat_;
    imgCI.extent = {hwbDesc.width, hwbDesc.height, 1};
    imgCI.mipLevels = 1;
    imgCI.arrayLayers = 1;
    imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device_, &imgCI, nullptr, &hwbImage_) != VK_SUCCESS) {
        LogE("vkCreateImage for AHardwareBuffer failed");
        return false;
    }
    return true;
}

bool AndroidVulkanContext::bindHWBMemory() noexcept {
    VkImportAndroidHardwareBufferInfoANDROID importInfo{};
    importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
    importInfo.buffer = hardwareBuffer_;

    VkMemoryDedicatedAllocateInfo dedicatedInfo{};
    dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicatedInfo.pNext = &importInfo;
    dedicatedInfo.image = hwbImage_;

    VkMemoryAllocateInfo memAI{};
    memAI.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAI.pNext = &dedicatedInfo;
    memAI.allocationSize = hwbAllocationSize_;
    const uint32_t memTypeIndex = findMemoryType(static_cast<uint32_t>(hwbMemoryTypeBits_),
                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memTypeIndex == UINT32_MAX) {
        LogE("bindHWBMemory: no suitable memory type");
        return false;
    }
    memAI.memoryTypeIndex = memTypeIndex;

    if (vkAllocateMemory(device_, &memAI, nullptr, &hwbMemory_) != VK_SUCCESS) {
        LogE("vkAllocateMemory for AHardwareBuffer failed");
        return false;
    }
    if (vkBindImageMemory(device_, hwbImage_, hwbMemory_, 0) != VK_SUCCESS) {
        LogE("vkBindImageMemory for AHardwareBuffer failed");
        return false;
    }

    VkImageViewCreateInfo ivCI{};
    ivCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivCI.image = hwbImage_;
    ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivCI.format = hwbVkFormat_;
    ivCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivCI.subresourceRange.levelCount = 1;
    ivCI.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &ivCI, nullptr, &hwbImageView_) != VK_SUCCESS) {
        LogE("vkCreateImageView for AHardwareBuffer failed");
        return false;
    }
    return true;
}

void AndroidVulkanContext::updateHWBDescriptor() noexcept {
    // clearHWBDescriptor 重置 pool 后需要重新分配 descriptor set
    if (descriptorSet_ == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = descriptorPool_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &descriptorSetLayout_;
        if (vkAllocateDescriptorSets(device_, &ai, &descriptorSet_) != VK_SUCCESS) {
            LogE("updateHWBDescriptor: vkAllocateDescriptorSets failed");
            return;
        }
    }

    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler = sampler_;
    imgInfo.imageView = hwbImageView_;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void AndroidVulkanContext::doBlitOES(int32_t textureId, int32_t surfaceW, int32_t surfaceH, int32_t contentW, int32_t contentH) noexcept {
    // 与 GLPreviewRenderer 一致的宽高比计算：content 在 surface 内保持比例居中显示
    const float contentAspect = static_cast<float>(contentW) / static_cast<float>(contentH);
    const float surfaceAspect = static_cast<float>(surfaceW) / static_cast<float>(surfaceH);
    float normX, normY;
    if (contentAspect > surfaceAspect) {
        // content 更宽 → 撑满宽度，上下 letterbox
        normX = 1.0f;
        normY = surfaceAspect / contentAspect;
    } else {
        // surface 更宽 → 撑满高度，左右 letterbox
        normX = contentAspect / surfaceAspect;
        normY = 1.0f;
    }
    const float kVerts[] = {-normX, -normY, normX, -normY, -normX, normY, normX, normY};

    // 根据 rotation_ 计算纹理坐标，与 GLPreviewRenderer 一致
    // TRIANGLE_STRIP: BL, BR, TL, TR
    // 注意：GL 坐标系 V 轴朝上(0=底,1=顶)，Vulkan 坐标系 V 轴朝下(0=顶,1=底)。
    // AHardwareBuffer 经 GL 写入后由 Vulkan shader 采样，需翻转 V 轴补偿坐标系差异。
    float kUVs[8];
    switch (rotation_) {
        case 90:
            kUVs[0] = 1; kUVs[1] = 1; kUVs[2] = 1; kUVs[3] = 0;
            kUVs[4] = 0; kUVs[5] = 1; kUVs[6] = 0; kUVs[7] = 0;
            break;
        case 180:
            kUVs[0] = 1; kUVs[1] = 0; kUVs[2] = 0; kUVs[3] = 0;
            kUVs[4] = 1; kUVs[5] = 1; kUVs[6] = 0; kUVs[7] = 1;
            break;
        case 270:
            kUVs[0] = 0; kUVs[1] = 0; kUVs[2] = 0; kUVs[3] = 1;
            kUVs[4] = 1; kUVs[5] = 0; kUVs[6] = 1; kUVs[7] = 1;
            break;
        default:
            kUVs[0] = 0; kUVs[1] = 1; kUVs[2] = 1; kUVs[3] = 1;
            kUVs[4] = 0; kUVs[5] = 0; kUVs[6] = 1; kUVs[7] = 0;
            break;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, surfaceW, surfaceH);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(oesProgram_);
    glVertexAttribPointer(static_cast<GLuint>(oesPosAttr_), 2, GL_FLOAT, GL_FALSE, 0, kVerts);
    glVertexAttribPointer(static_cast<GLuint>(oesTexCoordAttr_), 2, GL_FLOAT, GL_FALSE, 0, kUVs);
    glEnableVertexAttribArray(static_cast<GLuint>(oesPosAttr_));
    glEnableVertexAttribArray(static_cast<GLuint>(oesTexCoordAttr_));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textureId));
    glUniform1i(oesTexUniform_, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(static_cast<GLuint>(oesPosAttr_));
    glDisableVertexAttribArray(static_cast<GLuint>(oesTexCoordAttr_));
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glFlush();
}

bool AndroidVulkanContext::rebuildHWBResources(int32_t width, int32_t height) noexcept {
    destroyHardwareBufferResources();
    if (!allocateHardwareBuffer(width, height) || !createEGLImageFromHWB() ||
        !createFBOForEGLImage() || !createHWBVkImage() || !bindHWBMemory()) {
        return false;
    }
    updateHWBDescriptor();
    return true;
}

void AndroidVulkanContext::blitOESToHardwareBuffer(int32_t textureId,
                                                   int32_t surfaceW,
                                                   int32_t surfaceH,
                                                   int32_t contentW,
                                                   int32_t contentH) noexcept {
    // 尺寸变化或 Vulkan 侧资源已销毁时重建（hwbImage_ 可能被 destroyHardwareBufferResources
    // 单独清理）
    bool needRebuild = !hardwareBuffer_ || hwbImage_ == VK_NULL_HANDLE;
    if (!needRebuild) {
        AHardwareBuffer_Desc desc{};
        AHardwareBuffer_describe(hardwareBuffer_, &desc);
        needRebuild = (static_cast<int32_t>(desc.width) != surfaceW ||
                       static_cast<int32_t>(desc.height) != surfaceH);
    }
    if (needRebuild && !rebuildHWBResources(surfaceW, surfaceH)) {
        return;
    }

    doBlitOES(textureId, surfaceW, surfaceH, contentW, contentH);
    // EGL/GL 写入后 Vulkan 侧 layout 失效，重置为 UNDEFINED 确保下帧 barrier 做完整 transition
    hwbImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    // GL 错误时销毁重建后重试一次
    GLenum glErr = glGetError();
    if (glErr != GL_NO_ERROR) {
        LogW("blitOESToHardwareBuffer: GL error 0x{:x}, rebuilding and retrying", glErr);
        if (!rebuildHWBResources(surfaceW, surfaceH)) {
            return;
        }
        doBlitOES(textureId, surfaceW, surfaceH, contentW, contentH);
        hwbImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
        glErr = glGetError();
        if (glErr != GL_NO_ERROR) {
            LogW("blitOESToHardwareBuffer: retry also failed, GL error 0x{:x}", glErr);
        }
    }
}

void AndroidVulkanContext::drawFrame(int32_t textureId, int32_t width, int32_t height) noexcept {
    if (!isInit_) {
        return;
    }

    // EGL context 是线程绑定的，每帧重新绑定（无需缓存标志，避免多线程/重建后的 stale 状态）
    if (eglContext_) {
        eglContext_->attachContext(eglPbuffer_);
    }

    // 90/270 旋转需要交换宽高，保持内容宽高比
    const bool swapWH = (rotation_ == 90 || rotation_ == 270);
    const int32_t renderW = swapWH ? height : width;
    const int32_t renderH = swapWH ? width : height;

    // HWB 用屏幕尺寸，避免 Vulkan 侧拉伸（HWB 与 swapchain 同尺寸）。
    // doBlitOES 内根据内容尺寸 renderW/renderH 做 letterbox。
    blitOESToHardwareBuffer(textureId, frameWidth_, frameHeight_, renderW, renderH);
    // blit 失败（hwbImage_ 无效）时跳过本帧渲染，避免向 Vulkan 提交非法 image barrier
    if (hwbImage_ == VK_NULL_HANDLE) {
        return;
    }

    vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(device_,
                                            swapchain_,
                                            UINT64_MAX,
                                            imageAvailableSemaphores_[currentFrame_],
                                            VK_NULL_HANDLE,
                                            &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain(nullptr, frameWidth_, frameHeight_);
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LogE("vkAcquireNextImageKHR failed: {}", static_cast<int32_t>(result));
        return;
    }

    vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);
    vkResetCommandBuffer(commandBuffers_[imageIndex], 0);
    renderHardwareBufferToSwapchain(imageIndex);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphores_[currentFrame_];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphores_[currentFrame_];

    if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]) !=
        VK_SUCCESS) {
        LogE("vkQueueSubmit failed");
        // fence 已 reset 但 submit 失败不会 signal，提交空 submit signal fence 防止下帧
        // WaitForFences 死锁
        vkQueueWaitIdle(graphicsQueue_);
        VkSubmitInfo emptySubmit{};
        emptySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        vkQueueSubmit(graphicsQueue_, 1, &emptySubmit, inFlightFences_[currentFrame_]);
        return;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphores_[currentFrame_];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueue_, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain(nullptr, frameWidth_, frameHeight_);
        return;
    }
    if (result != VK_SUCCESS) {
        LogE("vkQueuePresentKHR failed: {}", static_cast<int32_t>(result));
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
}

void AndroidVulkanContext::renderHardwareBufferToSwapchain(uint32_t imageIndex) noexcept {
    auto& cmd = commandBuffers_[imageIndex];

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = hwbImageLayout_;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = hwbImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);

    VkClearValue clearColor{{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = renderPass_;
    rpBegin.framebuffer = swapchainFramebuffers_[imageIndex];
    rpBegin.renderArea.extent = swapchainExtent_;
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_,
                            0,
                            1,
                            &descriptorSet_,
                            0,
                            nullptr);
    vkCmdDraw(cmd, 6, 1, 0, 0); // 全屏两个三角形
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

uint32_t AndroidVulkanContext::findMemoryType(uint32_t typeFilter,
                                              VkMemoryPropertyFlags properties) const noexcept {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    LogE("findMemoryType: no suitable memory type found");
    return UINT32_MAX;
}

void AndroidVulkanContext::destroySwapchainResources() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (auto fb : swapchainFramebuffers_) {
        vkDestroyFramebuffer(device_, fb, nullptr);
    }
    swapchainFramebuffers_.clear();
    if (!commandBuffers_.empty() && commandPool_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device_,
                             commandPool_,
                             static_cast<uint32_t>(commandBuffers_.size()),
                             commandBuffers_.data());
        commandBuffers_.clear();
    }
    for (auto iv : swapchainImageViews_) {
        vkDestroyImageView(device_, iv, nullptr);
    }
    swapchainImageViews_.clear();
    swapchainImages_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void AndroidVulkanContext::destroyEGLInterop() noexcept {
    if (!eglContext_) {
        return;
    }
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    if (fboTexture_) {
        glDeleteTextures(1, &fboTexture_);
        fboTexture_ = 0;
    }
    if (oesProgram_) {
        glDeleteProgram(oesProgram_);
        oesProgram_ = 0;
    }
    if (eglImage_ != EGL_NO_IMAGE_KHR && fnEGLDestroyImage_) {
        fnEGLDestroyImage_(eglContext_->getDisplay(), eglImage_);
        eglImage_ = EGL_NO_IMAGE_KHR;
    }
    eglContext_->detachContext();
    if (eglPbuffer_ != EGL_NO_SURFACE) {
        eglContext_->releaseSurface(eglPbuffer_);
        eglPbuffer_ = EGL_NO_SURFACE;
    }
    eglContext_.reset();
}

void AndroidVulkanContext::clearHWBDescriptor() noexcept {
    if (descriptorPool_ == VK_NULL_HANDLE) {
        return;
    }
    // hwbImageView_ 已销毁，reset pool 使 descriptor set 回到未绑定状态，避免悬空引用
    // vkUpdateDescriptorSets 写入 VK_NULL_HANDLE imageView 违反 Vulkan spec
    vkResetDescriptorPool(device_, descriptorPool_, 0);
    descriptorSet_ = VK_NULL_HANDLE;
}

void AndroidVulkanContext::destroyHardwareBufferResources() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        if (hwbImageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, hwbImageView_, nullptr);
            hwbImageView_ = VK_NULL_HANDLE;
        }
        if (hwbImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, hwbImage_, nullptr);
            hwbImage_ = VK_NULL_HANDLE;
        }
        if (hwbMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, hwbMemory_, nullptr);
            hwbMemory_ = VK_NULL_HANDLE;
        }
        clearHWBDescriptor();
    }
    if (hardwareBuffer_) {
        AHardwareBuffer_release(hardwareBuffer_);
        hardwareBuffer_ = nullptr;
    }
    hwbImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
}

void AndroidVulkanContext::destroyDeviceResources() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (auto s : imageAvailableSemaphores_) {
        vkDestroySemaphore(device_, s, nullptr);
    }
    for (auto s : renderFinishedSemaphores_) {
        vkDestroySemaphore(device_, s, nullptr);
    }
    for (auto f : inFlightFences_) {
        vkDestroyFence(device_, f, nullptr);
    }
    imageAvailableSemaphores_.clear();
    renderFinishedSemaphores_.clear();
    inFlightFences_.clear();

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
}

} // namespace slark
