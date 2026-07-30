/*
 * PanVK Valhall v9 Vulkan Entry Points Layer Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "panvk_v9_entrypoints.h"

struct VkInstance_T {
    int dummy;
};

struct VkPhysicalDevice_T {
    struct pan_kmod_dev *kdev;
    struct pan_kmod_dev_props props;
};

struct VkDevice_T {
    struct pan_kmod_dev *kdev;
    struct VkPhysicalDevice_T *phys_dev;
};

struct VkQueue_T {
    struct VkDevice_T *device;
};

struct VkCommandPool_T {
    struct VkDevice_T *device;
};

struct VkCommandBuffer_T {
    struct VkDevice_T *device;
    struct v9_cmd_buffer *v9_cmd;
};

VkResult vkCreateInstance(const struct VkInstanceCreateInfo *pCreateInfo, void *pAllocator, VkInstance *pInstance) {
    if (!pInstance) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkInstance_T *inst = calloc(1, sizeof(*inst));
    if (!inst) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *pInstance = inst;
    return VK_SUCCESS;
}

void vkDestroyInstance(VkInstance instance, void *pAllocator) {
    if (instance) free(instance);
}

VkResult vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices) {
    if (!pPhysicalDeviceCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pPhysicalDevices) {
        *pPhysicalDeviceCount = 1;
        return VK_SUCCESS;
    }

    struct pan_kmod_dev *kdev = pan_kmod_dev_create(NULL);
    if (!kdev) return VK_ERROR_INITIALIZATION_FAILED;

    struct VkPhysicalDevice_T *pdev = calloc(1, sizeof(*pdev));
    if (!pdev) {
        pan_kmod_dev_destroy(kdev);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    pdev->kdev = kdev;
    pan_kmod_dev_query_props(kdev, &pdev->props);
    *pPhysicalDevices = pdev;
    *pPhysicalDeviceCount = 1;

    return VK_SUCCESS;
}

void vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceProperties *pProperties) {
    if (!physicalDevice || !pProperties) return;
    pProperties->apiVersion = (1u << 22) | (2u << 12); /* Vulkan 1.2 */
    pProperties->driverVersion = 1;
    pProperties->vendorID = 0x13B5; /* ARM Vendor ID */
    pProperties->deviceID = physicalDevice->props.gpu_id;
    snprintf(pProperties->deviceName, sizeof(pProperties->deviceName),
             "ARM Mali-G77 MC9 (Valhall v9 - PanVK Open Source Driver)");
}

VkResult vkCreateDevice(VkPhysicalDevice physicalDevice, const struct VkDeviceCreateInfo *pCreateInfo, void *pAllocator, VkDevice *pDevice) {
    if (!physicalDevice || !pDevice) return VK_ERROR_INITIALIZATION_FAILED;

    struct VkDevice_T *dev = calloc(1, sizeof(*dev));
    if (!dev) return VK_ERROR_OUT_OF_HOST_MEMORY;

    dev->phys_dev = physicalDevice;
    dev->kdev = physicalDevice->kdev;
    *pDevice = dev;

    return VK_SUCCESS;
}

void vkDestroyDevice(VkDevice device, void *pAllocator) {
    if (!device) return;
    if (device->phys_dev && device->phys_dev->kdev) {
        pan_kmod_dev_destroy(device->phys_dev->kdev);
        free(device->phys_dev);
    }
    free(device);
}

void vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue) {
    if (!device || !pQueue) return;
    struct VkQueue_T *queue = calloc(1, sizeof(*queue));
    if (!queue) return;
    queue->device = device;
    *pQueue = queue;
}

VkResult vkCreateCommandPool(VkDevice device, const struct VkCommandPoolCreateInfo *pCreateInfo, void *pAllocator, VkCommandPool *pCommandPool) {
    if (!device || !pCommandPool) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkCommandPool_T *pool = calloc(1, sizeof(*pool));
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->device = device;
    *pCommandPool = pool;
    return VK_SUCCESS;
}

void vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, void *pAllocator) {
    if (commandPool) free(commandPool);
}

VkResult vkAllocateCommandBuffers(VkDevice device, const struct VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers) {
    if (!device || !pAllocateInfo || !pCommandBuffers) return VK_ERROR_INITIALIZATION_FAILED;

    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) {
        struct VkCommandBuffer_T *cb = calloc(1, sizeof(*cb));
        if (!cb) return VK_ERROR_OUT_OF_HOST_MEMORY;
        cb->device = device;
        pCommandBuffers[i] = cb;
    }
    return VK_SUCCESS;
}

void vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers) {
    if (!pCommandBuffers) return;
    for (uint32_t i = 0; i < commandBufferCount; i++) {
        if (pCommandBuffers[i]) {
            if (pCommandBuffers[i]->v9_cmd) {
                v9_cmd_buffer_destroy(pCommandBuffers[i]->v9_cmd);
            }
            free(pCommandBuffers[i]);
        }
    }
}

VkResult vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const struct VkCommandBufferBeginInfo *pBeginInfo) {
    if (!commandBuffer) return VK_ERROR_INITIALIZATION_FAILED;
    return VK_SUCCESS;
}

void vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const struct VkRenderPassBeginInfo *pRenderPassBegin) {
    if (!commandBuffer || !pRenderPassBegin) return;

    struct v9_render_target_config config = {
        .width = pRenderPassBegin->renderAreaExtent.width,
        .height = pRenderPassBegin->renderAreaExtent.height,
        .clear_color = pRenderPassBegin->clearColor,
    };

    if (commandBuffer->v9_cmd) {
        v9_cmd_buffer_destroy(commandBuffer->v9_cmd);
    }
    commandBuffer->v9_cmd = v9_cmd_buffer_create(commandBuffer->device->kdev, &config);
    if (commandBuffer->v9_cmd) {
        v9_cmd_buffer_begin(commandBuffer->v9_cmd);
    }
}

void vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (commandBuffer && commandBuffer->v9_cmd) {
        v9_cmd_draw_indexed_triangle(commandBuffer->v9_cmd);
    }
}

void vkCmdEndRenderPass(VkCommandBuffer commandBuffer) {
    if (commandBuffer && commandBuffer->v9_cmd) {
        v9_cmd_buffer_end(commandBuffer->v9_cmd);
    }
}

VkResult vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
    if (!commandBuffer) return VK_ERROR_INITIALIZATION_FAILED;
    return VK_SUCCESS;
}

VkResult vkQueueSubmit(VkQueue queue, uint32_t submitCount, const struct VkSubmitInfo *pSubmits, void *fence) {
    if (!queue || !pSubmits) return VK_ERROR_INITIALIZATION_FAILED;

    for (uint32_t s = 0; s < submitCount; s++) {
        for (uint32_t cb = 0; cb < pSubmits[s].commandBufferCount; cb++) {
            VkCommandBuffer cmd = pSubmits[s].pCommandBuffers[cb];
            if (cmd && cmd->v9_cmd) {
                int ret = v9_cmd_buffer_submit(cmd->v9_cmd);
                if (ret != 0) return VK_ERROR_INITIALIZATION_FAILED;
            }
        }
    }
    return VK_SUCCESS;
}

VkResult vkQueueWaitIdle(VkQueue queue) {
    return VK_SUCCESS;
}

uint32_t panvk_v9_read_pixel(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y) {
    if (commandBuffer && commandBuffer->v9_cmd) {
        return v9_cmd_buffer_read_pixel(commandBuffer->v9_cmd, x, y);
    }
    return 0;
}

PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char *pName) {
    if (!pName) return NULL;
#define MATCH(name) if (strcmp(pName, #name) == 0) return (PFN_vkVoidFunction)name
    MATCH(vkCreateInstance);
    MATCH(vkDestroyInstance);
    MATCH(vkEnumeratePhysicalDevices);
    MATCH(vkGetPhysicalDeviceProperties);
    MATCH(vkCreateDevice);
    MATCH(vkDestroyDevice);
    MATCH(vkGetDeviceQueue);
    MATCH(vkCreateCommandPool);
    MATCH(vkDestroyCommandPool);
    MATCH(vkAllocateCommandBuffers);
    MATCH(vkFreeCommandBuffers);
    MATCH(vkBeginCommandBuffer);
    MATCH(vkEndCommandBuffer);
    MATCH(vkCmdBeginRenderPass);
    MATCH(vkCmdDrawIndexed);
    MATCH(vkCmdEndRenderPass);
    MATCH(vkQueueSubmit);
    MATCH(vkQueueWaitIdle);
    MATCH(panvk_v9_read_pixel);
#undef MATCH
    return NULL;
}

PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName) {
    return vkGetInstanceProcAddr(instance, pName);
}
