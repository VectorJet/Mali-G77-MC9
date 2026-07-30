/*
 * PanVK Valhall v9 Vulkan Entry Points Layer for Mali-G77
 * Minimal Vulkan API handles & function prototypes
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

#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO 3
#define VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO 9
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO 10
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 11
#define VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO 12
#define VK_STRUCTURE_TYPE_SUBMIT_INFO 13

typedef int VkResult;
typedef uint64_t VkFlags;

/* Opaque Vulkan Handles */
typedef struct VkInstance_T *VkInstance;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef struct VkDevice_T *VkDevice;
typedef struct VkQueue_T *VkQueue;
typedef struct VkCommandPool_T *VkCommandPool;
typedef struct VkCommandBuffer_T *VkCommandBuffer;

struct VkInstanceCreateInfo {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
};

struct VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    char deviceName[256];
};

struct VkDeviceCreateInfo {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
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

struct VkExtent2D {
    uint32_t width;
    uint32_t height;
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

/* Vulkan API Functions */
VkResult vkCreateInstance(const struct VkInstanceCreateInfo *pCreateInfo, void *pAllocator, VkInstance *pInstance);
void vkDestroyInstance(VkInstance instance, void *pAllocator);

VkResult vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices);
void vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceProperties *pProperties);

VkResult vkCreateDevice(VkPhysicalDevice physicalDevice, const struct VkDeviceCreateInfo *pCreateInfo, void *pAllocator, VkDevice *pDevice);
void vkDestroyDevice(VkDevice device, void *pAllocator);
void vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue);

VkResult vkCreateCommandPool(VkDevice device, const struct VkCommandPoolCreateInfo *pCreateInfo, void *pAllocator, VkCommandPool *pCommandPool);
void vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, void *pAllocator);

VkResult vkAllocateCommandBuffers(VkDevice device, const struct VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers);
void vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers);

VkResult vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const struct VkCommandBufferBeginInfo *pBeginInfo);
VkResult vkEndCommandBuffer(VkCommandBuffer commandBuffer);

void vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const struct VkRenderPassBeginInfo *pRenderPassBegin);
void vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
void vkCmdEndRenderPass(VkCommandBuffer commandBuffer);

VkResult vkQueueSubmit(VkQueue queue, uint32_t submitCount, const struct VkSubmitInfo *pSubmits, void *fence);
VkResult vkQueueWaitIdle(VkQueue queue);

uint32_t panvk_v9_read_pixel(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y);

#ifdef __cplusplus
}
#endif

#endif /* PANVK_V9_ENTRYPOINTS_H */
