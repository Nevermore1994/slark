//
// Created by Nevermore- on 2025/5/6.
//

#pragma once

#include "AndroidEGLContext.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

namespace slark {

/// Vulkan 渲染上下文，管理 VkInstance/VkDevice/VkSwapchain 生命周期。
/// 内部通过 EGL + AHardwareBuffer 桥接 MediaCodec OES 纹理到 Vulkan swapchain。
class AndroidVulkanContext {
public:
    AndroidVulkanContext() = default;
    ~AndroidVulkanContext() noexcept;

    /// 初始化 Vulkan 实例、设备、交换链，以及 EGL interop 上下文
    bool init(ANativeWindow* window, int32_t width, int32_t height) noexcept;

    /// 重建交换链（Surface 尺寸变化或 Surface 重建时调用）
    bool recreateSwapchain(ANativeWindow* window, int32_t width, int32_t height) noexcept;

    /// 将 OES textureId 渲染到 swapchain（EGL blit → AHardwareBuffer → Vulkan present）
    void drawFrame(int32_t textureId, int32_t width, int32_t height) noexcept;

    void setRotation(int32_t rotation) noexcept {
        rotation_ = rotation;
    }

    void release() noexcept;

    [[nodiscard]] bool isValid() const noexcept {
        return isInit_;
    }

private:
    bool createInstance() noexcept;
    bool createSurface(ANativeWindow* window) noexcept;
    bool selectPhysicalDevice() noexcept;
    bool createLogicalDevice() noexcept;
    bool createSwapchain(int32_t width, int32_t height) noexcept;
    bool createRenderPass() noexcept;
    bool createDescriptorSetLayout() noexcept;
    bool createSampler() noexcept;
    bool createPipeline() noexcept;
    bool createFramebuffers() noexcept;
    bool createCommandPool() noexcept;
    bool createCommandBuffers() noexcept;
    bool createSyncObjects() noexcept;
    bool createDescriptorPool() noexcept;

    bool initEGLInterop() noexcept;
    bool loadEGLExtensions() noexcept;
    bool allocateHardwareBuffer(int32_t width, int32_t height) noexcept;
    bool createEGLImageFromHWB() noexcept;
    bool createFBOForEGLImage() noexcept;
    bool createHWBVkImage() noexcept;
    bool bindHWBMemory() noexcept;
    void updateHWBDescriptor() noexcept;

    void blitOESToHardwareBuffer(int32_t textureId, int32_t width, int32_t height) noexcept;
    void renderHardwareBufferToSwapchain(uint32_t imageIndex) noexcept;

    void destroyEGLInterop() noexcept;
    void destroyHardwareBufferResources() noexcept;
    void destroySwapchainResources() noexcept;
    void destroyDeviceResources() noexcept;

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const noexcept;

private:
    bool isInit_ = false;
    int32_t rotation_ = 0;
    int32_t frameWidth_ = 0;
    int32_t frameHeight_ = 0;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = UINT32_MAX;
    uint32_t presentQueueFamily_ = UINT32_MAX;

    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    std::vector<VkFramebuffer> swapchainFramebuffers_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    static constexpr uint32_t kMaxFramesInFlight = 2;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    uint32_t currentFrame_ = 0;

    AHardwareBuffer* hardwareBuffer_ = nullptr;
    VkImage hwbImage_ = VK_NULL_HANDLE;
    VkDeviceMemory hwbMemory_ = VK_NULL_HANDLE;
    VkImageView hwbImageView_ = VK_NULL_HANDLE;
    VkFormat hwbVkFormat_ = VK_FORMAT_UNDEFINED;
    uint64_t hwbMemoryTypeBits_ = 0;
    uint64_t hwbAllocationSize_ = 0;
    VkImageLayout hwbImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    std::shared_ptr<AndroidEGLContext> eglContext_;
    EGLSurface eglPbuffer_ = EGL_NO_SURFACE;
    EGLImageKHR eglImage_ = EGL_NO_IMAGE_KHR;
    GLuint fbo_ = 0;
    GLuint fboTexture_ = 0;
    GLuint oesProgram_ = 0;
    GLint oesPosAttr_ = -1;
    GLint oesTexCoordAttr_ = -1;
    GLint oesTexUniform_ = -1;

    // 缓存 EGL/GL 扩展函数指针，避免每帧重复查询
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC fnEGLGetNativeClientBuffer_ = nullptr;
    PFNEGLCREATEIMAGEKHRPROC fnEGLCreateImage_ = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC fnEGLDestroyImage_ = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC fnGLEGLImageTargetTexture2D_ = nullptr;
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID fnVkGetHWBProperties_ = nullptr;
};

} // namespace slark
