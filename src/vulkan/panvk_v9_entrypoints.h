/*
 * PanVK Valhall v9 Vulkan Entry Points Layer for Mali-G77
 * Full Vulkan API handles, Pipeline, Shader, Memory, Xlib and XCB WSI declarations
 */

#ifndef PANVK_V9_ENTRYPOINTS_H
#define PANVK_V9_ENTRYPOINTS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "pan_kmod_kbase.h"
#include "v9_cmd_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VK_SUCCESS 0
#define VK_NOT_READY 1
#define VK_TIMEOUT 2
#define VK_ERROR_INITIALIZATION_FAILED -3
#define VK_ERROR_OUT_OF_HOST_MEMORY -1
#define VK_ERROR_OUT_OF_DEVICE_MEMORY -2
#define VK_ERROR_SURFACE_LOST_KHR -1000000000
#define VK_ERROR_NATIVE_WINDOW_IN_USE_KHR -1000000001

#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO 3
#define VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO 9
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO 10
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 11
#define VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO 12
#define VK_STRUCTURE_TYPE_SUBMIT_INFO 13
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 5
#define VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO 6
#define VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO 14
#define VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO 15
#define VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO 16
#define VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO 17
#define VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO 30
#define VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO 38
#define VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO 37
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO 32
#define VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO 33
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO 34
#define VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET 35
#define VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO 28
#define VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO 29
#define VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO 9
#define VK_STRUCTURE_TYPE_FENCE_CREATE_INFO 8
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 1000059001
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 1000059002
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_QUEUE_FAMILY_PROPERTIES_2 1000059005
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 1000059006
#define VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR 1000004000
#define VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR 1000005000
#define VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR 1000001000

#define VK_KHR_SURFACE_EXTENSION_NAME "VK_KHR_surface"
#define VK_KHR_XLIB_SURFACE_EXTENSION_NAME "VK_KHR_xlib_surface"
#define VK_KHR_XCB_SURFACE_EXTENSION_NAME "VK_KHR_xcb_surface"
#define VK_KHR_DISPLAY_EXTENSION_NAME "VK_KHR_display"
#define VK_KHR_SWAPCHAIN_EXTENSION_NAME "VK_KHR_swapchain"
#define VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME "VK_KHR_get_physical_device_properties2"

#define VK_FORMAT_R8G8B8A8_UNORM 37
#define VK_FORMAT_B8G8R8A8_UNORM 44
#define VK_COLOR_SPACE_SRGB_NONLINEAR_KHR 0
#define VK_PRESENT_MODE_IMMEDIATE_KHR 0
#define VK_PRESENT_MODE_MAILBOX_KHR 1
#define VK_PRESENT_MODE_FIFO_KHR 2

#define VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR 0x00000001
#define VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR 0x00000001
#define VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 0x00000010

typedef int VkResult;
typedef uint64_t VkFlags;
typedef uint64_t VkDeviceSize;

/* Opaque Vulkan Handles */
typedef struct VkInstance_T *VkInstance;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef struct VkDevice_T *VkDevice;
typedef struct VkQueue_T *VkQueue;
typedef struct VkCommandPool_T *VkCommandPool;
typedef struct VkCommandBuffer_T *VkCommandBuffer;
typedef struct VkSurfaceKHR_T *VkSurfaceKHR;
typedef struct VkSwapchainKHR_T *VkSwapchainKHR;
typedef struct VkImage_T *VkImage;
typedef struct VkImageView_T *VkImageView;
typedef struct VkDeviceMemory_T *VkDeviceMemory;
typedef struct VkBuffer_T *VkBuffer;
typedef struct VkBufferView_T *VkBufferView;
typedef struct VkShaderModule_T *VkShaderModule;
typedef struct VkPipelineCache_T *VkPipelineCache;
typedef struct VkPipelineLayout_T *VkPipelineLayout;
typedef struct VkRenderPass_T *VkRenderPass;
typedef struct VkFramebuffer_T *VkFramebuffer;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;
typedef struct VkDescriptorPool_T *VkDescriptorPool;
typedef struct VkDescriptorSet_T *VkDescriptorSet;
typedef struct VkPipeline_T *VkPipeline;
typedef struct VkSampler_T *VkSampler;
typedef struct VkSemaphore_T *VkSemaphore;
typedef struct VkFence_T *VkFence;

struct VkExtensionProperties {
    char extensionName[256];
    uint32_t specVersion;
};

struct VkLayerProperties {
    char layerName[256];
    uint32_t specVersion;
    uint32_t implementationVersion;
    char description[256];
};

struct VkInstanceCreateInfo {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
    const void *pApplicationInfo;
    uint32_t enabledLayerCount;
    const char * const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char * const *ppEnabledExtensionNames;
};

struct VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char deviceName[256];
    uint8_t pipelineCacheUUID[16];
    uint8_t limits[500];
    uint8_t sparseProperties[32];
};

struct VkPhysicalDeviceProperties2 {
    uint32_t sType;
    void *pNext;
    struct VkPhysicalDeviceProperties properties;
};

struct VkPhysicalDeviceFeatures2 {
    uint32_t sType;
    void *pNext;
    uint8_t features[256];
};

struct VkExtent2D {
    uint32_t width;
    uint32_t height;
};

struct VkQueueFamilyProperties2 {
    uint32_t sType;
    void *pNext;
    uint32_t queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    struct VkExtent2D minImageTransferGranularity;
};

struct VkPhysicalDeviceMemoryProperties2 {
    uint32_t sType;
    void *pNext;
    uint8_t memoryProperties[512];
};

struct VkPhysicalDeviceGroupProperties {
    uint32_t sType;
    void *pNext;
    uint32_t physicalDeviceCount;
    VkPhysicalDevice physicalDevices[32];
    uint32_t subsetAllocation;
};

struct VkDeviceCreateInfo {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
    uint32_t queueCreateInfoCount;
    const void *pQueueCreateInfos;
    uint32_t enabledLayerCount;
    const char * const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char * const *ppEnabledExtensionNames;
};

struct VkMemoryAllocateInfo {
    uint32_t sType;
    const void *pNext;
    VkDeviceSize allocationSize;
    uint32_t memoryTypeIndex;
};

struct VkMemoryRequirements {
    VkDeviceSize size;
    VkDeviceSize alignment;
    uint32_t memoryTypeBits;
};

struct VkBufferCreateInfo {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
    VkDeviceSize size;
    uint32_t usage;
    uint32_t sharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
};

struct VkShaderModuleCreateInfo {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
    size_t codeSize;
    const uint32_t *pCode;
};

struct VkCommandPoolCreateInfo {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
    uint32_t queueFamilyIndex;
};

struct VkCommandBufferAllocateInfo {
    uint32_t sType;
    const void *pNext;
    VkCommandPool commandPool;
    uint32_t level;
    uint32_t commandBufferCount;
};

struct VkCommandBufferBeginInfo {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
};

struct VkSurfaceCapabilitiesKHR {
    uint32_t minImageCount;
    uint32_t maxImageCount;
    struct VkExtent2D currentExtent;
    struct VkExtent2D minImageExtent;
    struct VkExtent2D maxImageExtent;
    uint32_t maxImageArrayLayers;
    uint32_t supportedTransforms;
    uint32_t currentTransform;
    uint32_t supportedCompositeAlpha;
    uint32_t supportedUsageFlags;
};

struct VkSurfaceFormatKHR {
    uint32_t format;
    uint32_t colorSpace;
};

struct VkXlibSurfaceCreateInfoKHR {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
    void *dpy;
    unsigned long window;
};

struct VkXcbSurfaceCreateInfoKHR {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
    void *connection;
    uint32_t window;
};

struct VkSwapchainCreateInfoKHR {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
    VkSurfaceKHR surface;
    uint32_t minImageCount;
    uint32_t imageFormat;
    uint32_t imageColorSpace;
    struct VkExtent2D imageExtent;
    uint32_t imageArrayLayers;
    uint32_t imageUsage;
    uint32_t imageSharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
    uint32_t preTransform;
    uint32_t compositeAlpha;
    uint32_t presentMode;
    uint32_t clipped;
    VkSwapchainKHR oldSwapchain;
};

struct VkPresentInfoKHR {
    uint32_t sType;
    const void *pNext;
    uint32_t waitSemaphoreCount;
    const void *pWaitSemaphores;
    uint32_t swapchainCount;
    const VkSwapchainKHR *pSwapchains;
    const uint32_t *pImageIndices;
    VkResult *pResults;
};

struct VkRenderPassBeginInfo {
    uint32_t sType;
    const void *pNext;
    struct VkExtent2D renderAreaExtent;
    uint32_t clearColor;
};

struct VkSubmitInfo {
    uint32_t sType;
    const void *pNext;
    uint32_t commandBufferCount;
    const VkCommandBuffer *pCommandBuffers;
};

/* Core Vulkan Entry Points */
VkResult vkCreateInstance(const struct VkInstanceCreateInfo *pCreateInfo, void *pAllocator, VkInstance *pInstance);
void vkDestroyInstance(VkInstance instance, void *pAllocator);

VkResult vkEnumerateInstanceVersion(uint32_t *pApiVersion);

VkResult vkEnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pPropertyCount, struct VkExtensionProperties *pProperties);
VkResult vkEnumerateInstanceLayerProperties(uint32_t *pPropertyCount, struct VkLayerProperties *pProperties);
VkResult vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName, uint32_t *pPropertyCount, struct VkExtensionProperties *pProperties);

VkResult vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices);
VkResult vkEnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t *pPhysicalDeviceGroupCount, struct VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroups);
VkResult vkEnumeratePhysicalDeviceGroupsKHR(VkInstance instance, uint32_t *pPhysicalDeviceGroupCount, struct VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroups);

void vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceProperties *pProperties);
void vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceProperties2 *pProperties);
void vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceProperties2 *pProperties);

void vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, void *pFeatures);
void vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceFeatures2 *pFeatures);
void vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceFeatures2 *pFeatures);

void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, void *pQueueFamilyProperties);
void vkGetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, struct VkQueueFamilyProperties2 *pQueueFamilyProperties);
void vkGetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, struct VkQueueFamilyProperties2 *pQueueFamilyProperties);

void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, void *pMemoryProperties);
void vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceMemoryProperties2 *pMemoryProperties);
void vkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceMemoryProperties2 *pMemoryProperties);

void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, void *pFormatProperties);
VkResult vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, uint32_t type, uint32_t tiling, uint32_t usage, uint32_t flags, void *pImageFormatProperties);
void vkGetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, uint32_t type, uint32_t samples, uint32_t usage, uint32_t tiling, uint32_t *pPropertyCount, void *pProperties);

VkResult vkCreateDevice(VkPhysicalDevice physicalDevice, const struct VkDeviceCreateInfo *pCreateInfo, void *pAllocator, VkDevice *pDevice);
void vkDestroyDevice(VkDevice device, void *pAllocator);
void vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue);

VkResult vkAllocateMemory(VkDevice device, const struct VkMemoryAllocateInfo *pAllocateInfo, void *pAllocator, VkDeviceMemory *pMemory);
void vkFreeMemory(VkDevice device, VkDeviceMemory memory, void *pAllocator);
VkResult vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkFlags flags, void **ppData);
void vkUnmapMemory(VkDevice device, VkDeviceMemory memory);

VkResult vkCreateBuffer(VkDevice device, const struct VkBufferCreateInfo *pCreateInfo, void *pAllocator, VkBuffer *pBuffer);
void vkDestroyBuffer(VkDevice device, VkBuffer buffer, void *pAllocator);
void vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, struct VkMemoryRequirements *pMemoryRequirements);
VkResult vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset);

VkResult vkCreateShaderModule(VkDevice device, const struct VkShaderModuleCreateInfo *pCreateInfo, void *pAllocator, VkShaderModule *pShaderModule);
void vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, void *pAllocator);

VkResult vkCreatePipelineCache(VkDevice device, const void *pCreateInfo, void *pAllocator, VkPipelineCache *pPipelineCache);
void vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, void *pAllocator);

VkResult vkCreatePipelineLayout(VkDevice device, const void *pCreateInfo, void *pAllocator, VkPipelineLayout *pPipelineLayout);
void vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, void *pAllocator);

VkResult vkCreateRenderPass(VkDevice device, const void *pCreateInfo, void *pAllocator, VkRenderPass *pRenderPass);
void vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, void *pAllocator);

VkResult vkCreateFramebuffer(VkDevice device, const void *pCreateInfo, void *pAllocator, VkFramebuffer *pFramebuffer);
void vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, void *pAllocator);

VkResult vkCreateDescriptorSetLayout(VkDevice device, const void *pCreateInfo, void *pAllocator, VkDescriptorSetLayout *pSetLayout);
void vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout setLayout, void *pAllocator);

VkResult vkCreateDescriptorPool(VkDevice device, const void *pCreateInfo, void *pAllocator, VkDescriptorPool *pDescriptorPool);
void vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, void *pAllocator);

VkResult vkAllocateDescriptorSets(VkDevice device, const void *pAllocateInfo, VkDescriptorSet *pDescriptorSets);
VkResult vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets);
void vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const void *pDescriptorWrites, uint32_t descriptorCopyCount, const void *pDescriptorCopies);

VkResult vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const void *pCreateInfos, void *pAllocator, VkPipeline *pPipelines);
VkResult vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const void *pCreateInfos, void *pAllocator, VkPipeline *pPipelines);
void vkDestroyPipeline(VkDevice device, VkPipeline pipeline, void *pAllocator);

VkResult vkCreateSemaphore(VkDevice device, const void *pCreateInfo, void *pAllocator, VkSemaphore *pSemaphore);
void vkDestroySemaphore(VkDevice device, VkSemaphore semaphore, void *pAllocator);

VkResult vkCreateFence(VkDevice device, const void *pCreateInfo, void *pAllocator, VkFence *pFence);
void vkDestroyFence(VkDevice device, VkFence fence, void *pAllocator);

VkResult vkCreateCommandPool(VkDevice device, const struct VkCommandPoolCreateInfo *pCreateInfo, void *pAllocator, VkCommandPool *pCommandPool);
void vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, void *pAllocator);

VkResult vkAllocateCommandBuffers(VkDevice device, const struct VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers);
void vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers);

VkResult vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const struct VkCommandBufferBeginInfo *pBeginInfo);
VkResult vkEndCommandBuffer(VkCommandBuffer commandBuffer);

void vkCmdBindPipeline(VkCommandBuffer commandBuffer, uint32_t pipelineBindPoint, VkPipeline pipeline);
void vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const void *pViewports);
void vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const void *pScissors);
void vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, uint32_t pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets);
void vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets);
void vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t indexType);
void vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);

void vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const struct VkRenderPassBeginInfo *pRenderPassBegin);
void vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
void vkCmdEndRenderPass(VkCommandBuffer commandBuffer);

VkResult vkQueueSubmit(VkQueue queue, uint32_t submitCount, const struct VkSubmitInfo *pSubmits, void *fence);
VkResult vkQueueWaitIdle(VkQueue queue);

/* WSI & Surface Functions */
VkResult vkCreateXlibSurfaceKHR(VkInstance instance, const struct VkXlibSurfaceCreateInfoKHR *pCreateInfo, void *pAllocator, VkSurfaceKHR *pSurface);
VkResult vkCreateXcbSurfaceKHR(VkInstance instance, const struct VkXcbSurfaceCreateInfoKHR *pCreateInfo, void *pAllocator, VkSurfaceKHR *pSurface);
void vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, void *pAllocator);
VkResult vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, uint32_t *pSupported);
uint32_t vkGetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, void *connection, uint32_t visual_id);
VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, struct VkSurfaceCapabilitiesKHR *pSurfaceCapabilities);
VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount, struct VkSurfaceFormatKHR *pSurfaceFormats);
VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount, uint32_t *pPresentModes);

/* Swapchain Functions */
VkResult vkCreateSwapchainKHR(VkDevice device, const struct VkSwapchainCreateInfoKHR *pCreateInfo, void *pAllocator, VkSwapchainKHR *pSwapchain);
void vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, void *pAllocator);
VkResult vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages);
VkResult vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, void *semaphore, void *fence, uint32_t *pImageIndex);
VkResult vkQueuePresentKHR(VkQueue queue, const struct VkPresentInfoKHR *pPresentInfo);

uint32_t panvk_v9_read_pixel(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y);

typedef void (*PFN_vkVoidFunction)(void);
PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char *pName);
PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device, const char *pName);
PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName);
VkResult vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pSupportedVersion);

#ifdef __cplusplus
}
#endif

#endif /* PANVK_V9_ENTRYPOINTS_H */
