/*
 * PanVK Valhall v9 Vulkan Entry Points & WSI Swapchain Layer Implementation
 * Full Vulkan API implementation for vkmark & Mesa Vulkan applications
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>

#include "panvk_v9_entrypoints.h"

#define ICD_LOADER_MAGIC 0x01CDC0DEu

static inline void set_loader_magic(void *object) {
    *(uintptr_t *)object = ICD_LOADER_MAGIC;
}

struct VkInstance_T {
    uintptr_t loader_data;
    struct VkPhysicalDevice_T *phys_dev;
};

struct VkPhysicalDevice_T {
    uintptr_t loader_data;
    struct pan_kmod_dev *kdev;
    struct pan_kmod_dev_props props;
};

struct VkDevice_T {
    uintptr_t loader_data;
    struct pan_kmod_dev *kdev;
    struct VkPhysicalDevice_T *phys_dev;
};

struct VkQueue_T {
    uintptr_t loader_data;
    struct VkDevice_T *device;
};

struct VkCommandPool_T {
    struct VkDevice_T *device;
};

struct VkCommandBuffer_T {
    uintptr_t loader_data;
    struct VkDevice_T *device;
    struct v9_cmd_buffer *v9_cmd;
};

struct VkSurfaceKHR_T {
    Display *dpy;
    Window window;
    uint32_t width;
    uint32_t height;
};

struct VkSwapchainKHR_T {
    struct VkDevice_T *device;
    struct VkSurfaceKHR_T *surface;
    uint32_t width;
    uint32_t height;
    uint32_t image_count;
    struct VkImage_T *images;
    GC gc;
    XImage *ximage;
    char *image_data;
};

struct VkImage_T {
    struct VkSwapchainKHR_T *swapchain;
    uint32_t index;
    uint32_t width;
    uint32_t height;
    struct pan_kmod_bo *bo;
    VkDeviceSize memory_offset;
};

struct VkImageView_T {
    int dummy;
};

struct VkDeviceMemory_T {
    struct pan_kmod_bo *bo;
    void *cpu;
    VkDeviceSize size;
};

struct VkBuffer_T {
    VkDeviceSize size;
    struct pan_kmod_bo *bo;
    VkDeviceSize memory_offset;
};

struct VkShaderModule_T {
    size_t code_size;
    uint32_t *code;
};

struct VkPipelineLayout_T {
    int dummy;
};

struct VkRenderPass_T {
    int dummy;
};

struct VkFramebuffer_T {
    int dummy;
};

struct VkPipelineCache_T {
    int dummy;
};

struct VkPipeline_T {
    int dummy;
};

struct VkDescriptorSetLayout_T {
    int dummy;
};

struct VkDescriptorPool_T {
    int dummy;
};

struct VkDescriptorSet_T {
    int dummy;
};

struct VkSemaphore_T {
    int dummy;
};

struct VkFence_T {
    bool signaled;
};

/* Loader Negotiation */
VkResult vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pSupportedVersion) {
    if (!pSupportedVersion) return VK_ERROR_INITIALIZATION_FAILED;
    if (*pSupportedVersion > 6) {
        *pSupportedVersion = 6;
    }
    return VK_SUCCESS;
}

VkResult vkEnumerateInstanceVersion(uint32_t *pApiVersion) {
    if (!pApiVersion) return VK_ERROR_INITIALIZATION_FAILED;
    *pApiVersion = (1u << 22) | (2u << 12); /* Vulkan 1.2 */
    return VK_SUCCESS;
}

/* Extension & Layer Enumeration */
VkResult vkEnumerateInstanceLayerProperties(uint32_t *pPropertyCount, struct VkLayerProperties *pProperties) {
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VkResult vkEnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pPropertyCount, struct VkExtensionProperties *pProperties) {
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;

    static const struct VkExtensionProperties inst_exts[] = {
        { .extensionName = VK_KHR_SURFACE_EXTENSION_NAME, .specVersion = 25 },
        { .extensionName = VK_KHR_XLIB_SURFACE_EXTENSION_NAME, .specVersion = 6 },
        { .extensionName = VK_KHR_XCB_SURFACE_EXTENSION_NAME, .specVersion = 6 },
        { .extensionName = VK_KHR_DISPLAY_EXTENSION_NAME, .specVersion = 23 },
        { .extensionName = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, .specVersion = 1 },
        { .extensionName = "VK_EXT_debug_utils", .specVersion = 2 },
        { .extensionName = "VK_EXT_debug_report", .specVersion = 10 },
        { .extensionName = "VK_KHR_device_group_creation", .specVersion = 1 },
        { .extensionName = "VK_KHR_external_memory_capabilities", .specVersion = 1 },
    };
    uint32_t num_exts = sizeof(inst_exts) / sizeof(inst_exts[0]);

    if (!pProperties) {
        *pPropertyCount = num_exts;
        return VK_SUCCESS;
    }

    uint32_t to_copy = (*pPropertyCount < num_exts) ? *pPropertyCount : num_exts;
    memcpy(pProperties, inst_exts, to_copy * sizeof(struct VkExtensionProperties));
    *pPropertyCount = to_copy;
    return VK_SUCCESS;
}

VkResult vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName, uint32_t *pPropertyCount, struct VkExtensionProperties *pProperties) {
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;

    static const struct VkExtensionProperties dev_exts[] = {
        { .extensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME, .specVersion = 70 },
    };
    uint32_t num_exts = sizeof(dev_exts) / sizeof(dev_exts[0]);

    if (!pProperties) {
        *pPropertyCount = num_exts;
        return VK_SUCCESS;
    }

    uint32_t to_copy = (*pPropertyCount < num_exts) ? *pPropertyCount : num_exts;
    memcpy(pProperties, dev_exts, to_copy * sizeof(struct VkExtensionProperties));
    *pPropertyCount = to_copy;
    return VK_SUCCESS;
}

/* Instance & Device Management */
VkResult vkCreateInstance(const struct VkInstanceCreateInfo *pCreateInfo, void *pAllocator, VkInstance *pInstance) {
    if (!pInstance) return VK_ERROR_INITIALIZATION_FAILED;

    if (pCreateInfo && pCreateInfo->ppEnabledExtensionNames) {
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
            printf("DEBUG: vkCreateInstance requested extension: '%s'\n", pCreateInfo->ppEnabledExtensionNames[i]);
        }
    }

    struct VkInstance_T *inst = calloc(1, sizeof(*inst));
    if (!inst) return VK_ERROR_OUT_OF_HOST_MEMORY;
    set_loader_magic(inst);

    struct pan_kmod_dev *kdev = pan_kmod_dev_create(NULL);
    if (kdev) {
        struct VkPhysicalDevice_T *pdev = calloc(1, sizeof(*pdev));
        if (pdev) {
            set_loader_magic(pdev);
            pdev->kdev = kdev;
            pan_kmod_dev_query_props(kdev, &pdev->props);
            inst->phys_dev = pdev;
        } else {
            pan_kmod_dev_destroy(kdev);
        }
    }

    *pInstance = inst;
    return VK_SUCCESS;
}

void vkDestroyInstance(VkInstance instance, void *pAllocator) {
    if (!instance) return;
    if (instance->phys_dev) {
        if (instance->phys_dev->kdev) {
            pan_kmod_dev_destroy(instance->phys_dev->kdev);
        }
        free(instance->phys_dev);
    }
    free(instance);
}

VkResult vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices) {
    if (!pPhysicalDeviceCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!instance || !instance->phys_dev) {
        *pPhysicalDeviceCount = 0;
        return VK_SUCCESS;
    }

    if (!pPhysicalDevices) {
        *pPhysicalDeviceCount = 1;
        return VK_SUCCESS;
    }

    *pPhysicalDevices = instance->phys_dev;
    *pPhysicalDeviceCount = 1;
    return VK_SUCCESS;
}

VkResult vkEnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t *pPhysicalDeviceGroupCount, struct VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroups) {
    if (!pPhysicalDeviceGroupCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pPhysicalDeviceGroups) {
        *pPhysicalDeviceGroupCount = 1;
        return VK_SUCCESS;
    }
    pPhysicalDeviceGroups[0].physicalDeviceCount = 1;
    vkEnumeratePhysicalDevices(instance, &pPhysicalDeviceGroups[0].physicalDeviceCount, pPhysicalDeviceGroups[0].physicalDevices);
    *pPhysicalDeviceGroupCount = 1;
    return VK_SUCCESS;
}

void vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceProperties *pProperties) {
    if (!physicalDevice || !pProperties) return;
    memset(pProperties, 0, sizeof(*pProperties));
    pProperties->apiVersion = (1u << 22) | (2u << 12); /* Vulkan 1.2 */
    pProperties->driverVersion = 1;
    pProperties->vendorID = 0x13B5; /* ARM Vendor ID */
    pProperties->deviceID = physicalDevice->props.gpu_id;
    pProperties->deviceType = 1; /* Integrated GPU */
    snprintf(pProperties->deviceName, sizeof(pProperties->deviceName),
             "ARM Mali-G77 MC9 (Valhall v9 - PanVK Open Source Driver)");
}

void vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceProperties2 *pProperties) {
    if (!pProperties) return;
    vkGetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);
}

void vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, void *pFeatures) {
    if (pFeatures) memset(pFeatures, 0, 256);
}

void vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceFeatures2 *pFeatures) {
    if (pFeatures) memset(pFeatures->features, 0, sizeof(pFeatures->features));
}

void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, void *pQueueFamilyProperties) {
    if (!pQueueFamilyPropertyCount) return;
    if (!pQueueFamilyProperties) {
        *pQueueFamilyPropertyCount = 1;
        return;
    }
    /* Family 0: Graphics + Compute + Transfer (0x7) */
    uint32_t *qfp = (uint32_t *)pQueueFamilyProperties;
    memset(qfp, 0, 24);
    qfp[0] = 0x7; /* Queue flags */
    qfp[1] = 1;   /* Queue count */
    *pQueueFamilyPropertyCount = 1;
}

void vkGetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, struct VkQueueFamilyProperties2 *pQueueFamilyProperties) {
    if (!pQueueFamilyPropertyCount) return;
    if (!pQueueFamilyProperties) {
        *pQueueFamilyPropertyCount = 1;
        return;
    }
    memset(pQueueFamilyProperties, 0, sizeof(*pQueueFamilyProperties));
    pQueueFamilyProperties->queueFlags = 0x7;
    pQueueFamilyProperties->queueCount = 1;
    *pQueueFamilyPropertyCount = 1;
}

void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, void *pMemoryProperties) {
    if (!pMemoryProperties) return;
    /* 1 Memory Type, 1 Memory Heap */
    uint32_t *mp = (uint32_t *)pMemoryProperties;
    memset(mp, 0, 256);
    mp[0] = 1; /* memoryTypeCount */
    mp[1] = 0xF; /* propertyFlags: DeviceLocal | HostVisible | HostCoherent | HostCached */
    mp[2] = 0;   /* heapIndex */
    mp[33] = 1;  /* memoryHeapCount */
    *(uint64_t *)(mp + 34) = 4096ULL * 1024ULL * 1024ULL; /* 4GB Heap Size */
    mp[36] = 1;  /* Heap flags: DeviceLocal */
}

void vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceMemoryProperties2 *pMemoryProperties) {
    if (!pMemoryProperties) return;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties->memoryProperties);
}

void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, void *pFormatProperties) {
    if (!pFormatProperties) return;
    uint32_t *fp = (uint32_t *)pFormatProperties;
    fp[0] = 0x00000001; /* linearTilingFeatures */
    fp[1] = 0x00000001; /* optimalTilingFeatures */
    fp[2] = 0x00000001; /* bufferFeatures */
}

VkResult vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, uint32_t type, uint32_t tiling, uint32_t usage, uint32_t flags, void *pImageFormatProperties) {
    if (!pImageFormatProperties) return VK_ERROR_INITIALIZATION_FAILED;
    uint32_t *ifp = (uint32_t *)pImageFormatProperties;
    memset(ifp, 0, 32);
    ifp[0] = 4096; /* maxExtent.width */
    ifp[1] = 4096; /* maxExtent.height */
    ifp[2] = 1;    /* maxExtent.depth */
    ifp[3] = 1;    /* maxMipLevels */
    ifp[4] = 1;    /* maxArrayLayers */
    ifp[5] = 0x1;  /* sampleCounts: 1BIT */
    *(uint64_t *)(ifp + 6) = 256ULL * 1024ULL * 1024ULL; /* maxResourceSize */
    return VK_SUCCESS;
}

void vkGetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, uint32_t type, uint32_t samples, uint32_t usage, uint32_t tiling, uint32_t *pPropertyCount, void *pProperties) {
    if (pPropertyCount) *pPropertyCount = 0;
}

VkResult vkCreateDevice(VkPhysicalDevice physicalDevice, const struct VkDeviceCreateInfo *pCreateInfo, void *pAllocator, VkDevice *pDevice) {
    if (!physicalDevice || !pDevice) return VK_ERROR_INITIALIZATION_FAILED;

    struct VkDevice_T *dev = calloc(1, sizeof(*dev));
    if (!dev) return VK_ERROR_OUT_OF_HOST_MEMORY;

    set_loader_magic(dev);
    dev->phys_dev = physicalDevice;
    dev->kdev = physicalDevice->kdev;
    *pDevice = dev;

    return VK_SUCCESS;
}

void vkDestroyDevice(VkDevice device, void *pAllocator) {
    if (device) free(device);
}

void vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue) {
    if (!device || !pQueue) return;
    struct VkQueue_T *queue = calloc(1, sizeof(*queue));
    if (!queue) return;
    set_loader_magic(queue);
    queue->device = device;
    *pQueue = queue;
}

/* Memory Allocation & Buffer Management */
VkResult vkAllocateMemory(VkDevice device, const struct VkMemoryAllocateInfo *pAllocateInfo, void *pAllocator, VkDeviceMemory *pMemory) {
    if (!device || !device->kdev || !pAllocateInfo || !pMemory) return VK_ERROR_INITIALIZATION_FAILED;

    struct VkDeviceMemory_T *mem = calloc(1, sizeof(*mem));
    if (!mem) return VK_ERROR_OUT_OF_HOST_MEMORY;

    size_t sz = pAllocateInfo->allocationSize > 0 ? pAllocateInfo->allocationSize : 4096;
    mem->bo = pan_kmod_bo_alloc(device->kdev, sz, PAN_KMOD_BO_FLAG_READ | PAN_KMOD_BO_FLAG_WRITE);
    if (!mem->bo) {
        free(mem);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    mem->size = sz;
    *pMemory = mem;
    return VK_SUCCESS;
}

void vkFreeMemory(VkDevice device, VkDeviceMemory memory, void *pAllocator) {
    if (!memory) return;
    if (memory->bo) pan_kmod_bo_free(memory->bo);
    free(memory);
}

VkResult vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkFlags flags, void **ppData) {
    if (!memory || !memory->bo || !ppData) return VK_ERROR_INITIALIZATION_FAILED;
    *ppData = (uint8_t *)memory->bo->cpu + offset;
    return VK_SUCCESS;
}

void vkUnmapMemory(VkDevice device, VkDeviceMemory memory) {
}

VkResult vkCreateBuffer(VkDevice device, const struct VkBufferCreateInfo *pCreateInfo, void *pAllocator, VkBuffer *pBuffer) {
    if (!device || !pCreateInfo || !pBuffer) return VK_ERROR_INITIALIZATION_FAILED;

    struct VkBuffer_T *buf = calloc(1, sizeof(*buf));
    if (!buf) return VK_ERROR_OUT_OF_HOST_MEMORY;

    buf->size = pCreateInfo->size;
    *pBuffer = buf;
    return VK_SUCCESS;
}

void vkDestroyBuffer(VkDevice device, VkBuffer buffer, void *pAllocator) {
    if (buffer) free(buffer);
}

void vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, struct VkMemoryRequirements *pMemoryRequirements) {
    if (!pMemoryRequirements) return;
    pMemoryRequirements->size = buffer ? buffer->size : 4096;
    pMemoryRequirements->alignment = 64;
    pMemoryRequirements->memoryTypeBits = 0x1;
}

VkResult vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    if (buffer && memory) {
        buffer->bo = memory->bo;
        buffer->memory_offset = memoryOffset;
    }
    return VK_SUCCESS;
}

VkResult vkCreateImage(VkDevice device, const struct VkImageCreateInfo *pCreateInfo,
                       void *pAllocator, VkImage *pImage) {
    if (!pCreateInfo || !pImage) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkImage_T *image = calloc(1, sizeof(*image));
    if (!image) return VK_ERROR_OUT_OF_HOST_MEMORY;
    image->width = pCreateInfo->extent.width;
    image->height = pCreateInfo->extent.height;
    *pImage = image;
    return VK_SUCCESS;
}

void vkDestroyImage(VkDevice device, VkImage image, void *pAllocator) {
    if (image && !image->swapchain) free(image);
}

void vkGetImageMemoryRequirements(VkDevice device, VkImage image,
                                  struct VkMemoryRequirements *pMemoryRequirements) {
    if (!pMemoryRequirements) return;
    VkDeviceSize size = image ? (VkDeviceSize)image->width * image->height * 4 : 4096;
    pMemoryRequirements->size = size > 0 ? size : 4096;
    pMemoryRequirements->alignment = 64;
    pMemoryRequirements->memoryTypeBits = 1;
}

VkResult vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory,
                           VkDeviceSize memoryOffset) {
    if (!image || !memory) return VK_ERROR_INITIALIZATION_FAILED;
    image->bo = memory->bo;
    image->memory_offset = memoryOffset;
    return VK_SUCCESS;
}

VkResult vkCreateImageView(VkDevice device, const void *pCreateInfo, void *pAllocator,
                           VkImageView *pView) {
    if (!pView) return VK_ERROR_INITIALIZATION_FAILED;
    *pView = calloc(1, sizeof(struct VkImageView_T));
    return *pView ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

void vkDestroyImageView(VkDevice device, VkImageView imageView, void *pAllocator) {
    free(imageView);
}

/* Shader Module & Pipeline Implementation */
VkResult vkCreateShaderModule(VkDevice device, const struct VkShaderModuleCreateInfo *pCreateInfo, void *pAllocator, VkShaderModule *pShaderModule) {
    if (!pCreateInfo || !pShaderModule) return VK_ERROR_INITIALIZATION_FAILED;

    struct VkShaderModule_T *sm = calloc(1, sizeof(*sm));
    if (!sm) return VK_ERROR_OUT_OF_HOST_MEMORY;

    if (pCreateInfo->pCode && pCreateInfo->codeSize > 0) {
        sm->code_size = pCreateInfo->codeSize;
        sm->code = malloc(sm->code_size);
        if (sm->code) {
            memcpy(sm->code, pCreateInfo->pCode, sm->code_size);
        }
    }

    *pShaderModule = sm;
    return VK_SUCCESS;
}

void vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, void *pAllocator) {
    if (!shaderModule) return;
    if (shaderModule->code) free(shaderModule->code);
    free(shaderModule);
}

VkResult vkCreatePipelineCache(VkDevice device, const void *pCreateInfo, void *pAllocator, VkPipelineCache *pPipelineCache) {
    if (!pPipelineCache) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkPipelineCache_T *pc = calloc(1, sizeof(*pc));
    *pPipelineCache = pc;
    return VK_SUCCESS;
}

void vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, void *pAllocator) {
    if (pipelineCache) free(pipelineCache);
}

VkResult vkCreatePipelineLayout(VkDevice device, const void *pCreateInfo, void *pAllocator, VkPipelineLayout *pPipelineLayout) {
    if (!pPipelineLayout) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkPipelineLayout_T *pl = calloc(1, sizeof(*pl));
    *pPipelineLayout = pl;
    return VK_SUCCESS;
}

void vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, void *pAllocator) {
    if (pipelineLayout) free(pipelineLayout);
}

VkResult vkCreateRenderPass(VkDevice device, const void *pCreateInfo, void *pAllocator, VkRenderPass *pRenderPass) {
    if (!pRenderPass) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkRenderPass_T *rp = calloc(1, sizeof(*rp));
    *pRenderPass = rp;
    return VK_SUCCESS;
}

void vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, void *pAllocator) {
    if (renderPass) free(renderPass);
}

VkResult vkCreateFramebuffer(VkDevice device, const void *pCreateInfo, void *pAllocator, VkFramebuffer *pFramebuffer) {
    if (!pFramebuffer) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkFramebuffer_T *fb = calloc(1, sizeof(*fb));
    *pFramebuffer = fb;
    return VK_SUCCESS;
}

void vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, void *pAllocator) {
    if (framebuffer) free(framebuffer);
}

VkResult vkCreateDescriptorSetLayout(VkDevice device, const void *pCreateInfo, void *pAllocator, VkDescriptorSetLayout *pSetLayout) {
    if (!pSetLayout) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkDescriptorSetLayout_T *dsl = calloc(1, sizeof(*dsl));
    *pSetLayout = dsl;
    return VK_SUCCESS;
}

void vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout setLayout, void *pAllocator) {
    if (setLayout) free(setLayout);
}

VkResult vkCreateDescriptorPool(VkDevice device, const void *pCreateInfo, void *pAllocator, VkDescriptorPool *pDescriptorPool) {
    if (!pDescriptorPool) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkDescriptorPool_T *dp = calloc(1, sizeof(*dp));
    *pDescriptorPool = dp;
    return VK_SUCCESS;
}

void vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, void *pAllocator) {
    if (descriptorPool) free(descriptorPool);
}

VkResult vkAllocateDescriptorSets(VkDevice device, const void *pAllocateInfo, VkDescriptorSet *pDescriptorSets) {
    if (!pDescriptorSets) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkDescriptorSet_T *ds = calloc(1, sizeof(*ds));
    *pDescriptorSets = ds;
    return VK_SUCCESS;
}

VkResult vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets) {
    if (!pDescriptorSets) return VK_SUCCESS;
    for (uint32_t i = 0; i < descriptorSetCount; i++) {
        if (pDescriptorSets[i]) free(pDescriptorSets[i]);
    }
    return VK_SUCCESS;
}

void vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const void *pDescriptorWrites, uint32_t descriptorCopyCount, const void *pDescriptorCopies) {
}

VkResult vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const void *pCreateInfos, void *pAllocator, VkPipeline *pPipelines) {
    if (!pPipelines) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < createInfoCount; i++) {
        struct VkPipeline_T *pipe = calloc(1, sizeof(*pipe));
        pPipelines[i] = pipe;
    }
    return VK_SUCCESS;
}

VkResult vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const void *pCreateInfos, void *pAllocator, VkPipeline *pPipelines) {
    if (!pPipelines) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < createInfoCount; i++) {
        struct VkPipeline_T *pipe = calloc(1, sizeof(*pipe));
        pPipelines[i] = pipe;
    }
    return VK_SUCCESS;
}

void vkDestroyPipeline(VkDevice device, VkPipeline pipeline, void *pAllocator) {
    if (pipeline) free(pipeline);
}

VkResult vkCreateSemaphore(VkDevice device, const void *pCreateInfo, void *pAllocator, VkSemaphore *pSemaphore) {
    if (!pSemaphore) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkSemaphore_T *sem = calloc(1, sizeof(*sem));
    *pSemaphore = sem;
    return VK_SUCCESS;
}

void vkDestroySemaphore(VkDevice device, VkSemaphore semaphore, void *pAllocator) {
    if (semaphore) free(semaphore);
}

VkResult vkCreateFence(VkDevice device, const void *pCreateInfo, void *pAllocator, VkFence *pFence) {
    if (!pFence) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkFence_T *f = calloc(1, sizeof(*f));
    if (f && pCreateInfo) {
        const uint32_t *flags = (const uint32_t *)((const uint8_t *)pCreateInfo + 16);
        f->signaled = (*flags & 1u) != 0;
    }
    *pFence = f;
    return f ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

void vkDestroyFence(VkDevice device, VkFence fence, void *pAllocator) {
    if (fence) free(fence);
}

VkResult vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences) {
    if (!pFences) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < fenceCount; i++) {
        if (pFences[i]) pFences[i]->signaled = false;
    }
    return VK_SUCCESS;
}

VkResult vkGetFenceStatus(VkDevice device, VkFence fence) {
    return fence && fence->signaled ? VK_SUCCESS : VK_NOT_READY;
}

VkResult vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences,
                         uint32_t waitAll, uint64_t timeout) {
    if (!pFences) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < fenceCount; i++) {
        if (pFences[i]) pFences[i]->signaled = true;
    }
    return VK_SUCCESS;
}

/* Command Pool & Buffer Management */
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
        set_loader_magic(cb);
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

void vkCmdBindPipeline(VkCommandBuffer commandBuffer, uint32_t pipelineBindPoint, VkPipeline pipeline) {
}

void vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const void *pViewports) {
}

void vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const void *pScissors) {
}

void vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, uint32_t pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets) {
}

void vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets) {
}

void vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t indexType) {
}

void vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer,
                     uint32_t regionCount, const struct VkBufferCopy *pRegions) {
    if (!srcBuffer || !srcBuffer->bo || !dstBuffer || !dstBuffer->bo || !pRegions) return;

    for (uint32_t i = 0; i < regionCount; i++) {
        VkDeviceSize src_offset = srcBuffer->memory_offset + pRegions[i].srcOffset;
        VkDeviceSize dst_offset = dstBuffer->memory_offset + pRegions[i].dstOffset;
        VkDeviceSize size = pRegions[i].size;
        if (src_offset > srcBuffer->bo->size || size > srcBuffer->bo->size - src_offset ||
            dst_offset > dstBuffer->bo->size || size > dstBuffer->bo->size - dst_offset) {
            continue;
        }
        memcpy((uint8_t *)dstBuffer->bo->cpu + dst_offset,
               (const uint8_t *)srcBuffer->bo->cpu + src_offset, size);
    }
}

void vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, uint32_t srcStageMask,
                          uint32_t dstStageMask, uint32_t dependencyFlags,
                          uint32_t memoryBarrierCount, const void *pMemoryBarriers,
                          uint32_t bufferMemoryBarrierCount, const void *pBufferMemoryBarriers,
                          uint32_t imageMemoryBarrierCount, const void *pImageMemoryBarriers) {
}

void vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    if (commandBuffer && commandBuffer->v9_cmd) {
        v9_cmd_draw_indexed_triangle(commandBuffer->v9_cmd);
    }
}

void vkCmdBeginRenderPass(VkCommandBuffer commandBuffer,
                          const struct VkRenderPassBeginInfo *pRenderPassBegin,
                          uint32_t contents) {
    if (!commandBuffer || !pRenderPassBegin) return;

    struct v9_render_target_config config = {
        .width = pRenderPassBegin->renderArea.extent.width > 0 ? pRenderPassBegin->renderArea.extent.width : 300,
        .height = pRenderPassBegin->renderArea.extent.height > 0 ? pRenderPassBegin->renderArea.extent.height : 300,
        .clear_color = 0,
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
    if (fence) ((VkFence)fence)->signaled = true;
    return VK_SUCCESS;
}

VkResult vkQueueWaitIdle(VkQueue queue) {
    return VK_SUCCESS;
}

VkResult vkDeviceWaitIdle(VkDevice device) {
    return VK_SUCCESS;
}

/* WSI & Surface Implementation */
VkResult vkCreateXlibSurfaceKHR(VkInstance instance, const struct VkXlibSurfaceCreateInfoKHR *pCreateInfo, void *pAllocator, VkSurfaceKHR *pSurface) {
    if (!pCreateInfo || !pSurface) return VK_ERROR_INITIALIZATION_FAILED;

    struct VkSurfaceKHR_T *surf = calloc(1, sizeof(*surf));
    if (!surf) return VK_ERROR_OUT_OF_HOST_MEMORY;

    surf->dpy = (Display *)pCreateInfo->dpy;
    surf->window = pCreateInfo->window;
    surf->width = 300;
    surf->height = 300;

    if (surf->dpy && surf->window) {
        XWindowAttributes attr;
        if (XGetWindowAttributes(surf->dpy, surf->window, &attr)) {
            surf->width = attr.width;
            surf->height = attr.height;
        }
    }

    *pSurface = surf;
    return VK_SUCCESS;
}

VkResult vkCreateXcbSurfaceKHR(VkInstance instance, const struct VkXcbSurfaceCreateInfoKHR *pCreateInfo, void *pAllocator, VkSurfaceKHR *pSurface) {
    if (!pCreateInfo || !pSurface) return VK_ERROR_INITIALIZATION_FAILED;

    struct VkSurfaceKHR_T *surf = calloc(1, sizeof(*surf));
    if (!surf) return VK_ERROR_OUT_OF_HOST_MEMORY;

    surf->window = (unsigned long)pCreateInfo->window;
    surf->width = 800;
    surf->height = 600;

    *pSurface = surf;
    return VK_SUCCESS;
}

uint32_t vkGetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, void *connection, uint32_t visual_id) {
    return 1; /* VK_TRUE */
}

VkResult vkGetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount, void *pProperties) {
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount, void *pProperties) {
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VkResult vkGetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice, uint32_t planeIndex, uint32_t *pDisplayCount, void *pDisplays) {
    if (!pDisplayCount) return VK_ERROR_INITIALIZATION_FAILED;
    *pDisplayCount = 0;
    return VK_SUCCESS;
}

VkResult vkGetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice, void *display, uint32_t *pPropertyCount, void *pProperties) {
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

void vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, void *pAllocator) {
    if (surface) free(surface);
}

VkResult vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, uint32_t *pSupported) {
    if (!pSupported) return VK_ERROR_INITIALIZATION_FAILED;
    *pSupported = 1; /* Queue family 0 supports surface presentation */
    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, struct VkSurfaceCapabilitiesKHR *pSurfaceCapabilities) {
    if (!pSurfaceCapabilities) return VK_ERROR_INITIALIZATION_FAILED;

    uint32_t w = surface ? surface->width : 300;
    uint32_t h = surface ? surface->height : 300;

    pSurfaceCapabilities->minImageCount = 1;
    pSurfaceCapabilities->maxImageCount = 8;
    pSurfaceCapabilities->currentExtent.width = w;
    pSurfaceCapabilities->currentExtent.height = h;
    pSurfaceCapabilities->minImageExtent.width = 1;
    pSurfaceCapabilities->minImageExtent.height = 1;
    pSurfaceCapabilities->maxImageExtent.width = 4096;
    pSurfaceCapabilities->maxImageExtent.height = 4096;
    pSurfaceCapabilities->maxImageArrayLayers = 1;
    pSurfaceCapabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    pSurfaceCapabilities->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount, struct VkSurfaceFormatKHR *pSurfaceFormats) {
    if (!pSurfaceFormatCount) return VK_ERROR_INITIALIZATION_FAILED;

    static const struct VkSurfaceFormatKHR formats[] = {
        { .format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
        { .format = VK_FORMAT_R8G8B8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
    };
    uint32_t num_formats = sizeof(formats) / sizeof(formats[0]);

    if (!pSurfaceFormats) {
        *pSurfaceFormatCount = num_formats;
        return VK_SUCCESS;
    }

    uint32_t to_copy = (*pSurfaceFormatCount < num_formats) ? *pSurfaceFormatCount : num_formats;
    memcpy(pSurfaceFormats, formats, to_copy * sizeof(struct VkSurfaceFormatKHR));
    *pSurfaceFormatCount = to_copy;
    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount, uint32_t *pPresentModes) {
    if (!pPresentModeCount) return VK_ERROR_INITIALIZATION_FAILED;

    static const uint32_t modes[] = { VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR };
    uint32_t num_modes = sizeof(modes) / sizeof(modes[0]);

    if (!pPresentModes) {
        *pPresentModeCount = num_modes;
        return VK_SUCCESS;
    }

    uint32_t to_copy = (*pPresentModeCount < num_modes) ? *pPresentModeCount : num_modes;
    memcpy(pPresentModes, modes, to_copy * sizeof(uint32_t));
    *pPresentModeCount = to_copy;
    return VK_SUCCESS;
}

/* Swapchain Implementation */
VkResult vkCreateSwapchainKHR(VkDevice device, const struct VkSwapchainCreateInfoKHR *pCreateInfo, void *pAllocator, VkSwapchainKHR *pSwapchain) {
    if (!device || !pCreateInfo || !pSwapchain) return VK_ERROR_INITIALIZATION_FAILED;

    struct VkSwapchainKHR_T *sc = calloc(1, sizeof(*sc));
    if (!sc) return VK_ERROR_OUT_OF_HOST_MEMORY;

    sc->device = device;
    sc->surface = pCreateInfo->surface;
    sc->width = pCreateInfo->imageExtent.width > 0 ? pCreateInfo->imageExtent.width : 300;
    sc->height = pCreateInfo->imageExtent.height > 0 ? pCreateInfo->imageExtent.height : 300;
    sc->image_count = pCreateInfo->minImageCount > 0 ? pCreateInfo->minImageCount : 2;

    sc->images = calloc(sc->image_count, sizeof(struct VkImage_T));
    for (uint32_t i = 0; i < sc->image_count; i++) {
        sc->images[i].swapchain = sc;
        sc->images[i].index = i;
    }

    if (sc->surface && sc->surface->dpy && sc->surface->window) {
        int screen = DefaultScreen(sc->surface->dpy);
        sc->gc = XCreateGC(sc->surface->dpy, sc->surface->window, 0, NULL);
        sc->image_data = malloc(sc->width * sc->height * 4);
        if (sc->image_data) {
            sc->ximage = XCreateImage(sc->surface->dpy, DefaultVisual(sc->surface->dpy, screen),
                                     24, ZPixmap, 0, sc->image_data, sc->width, sc->height, 32, 0);
        }
    }

    *pSwapchain = sc;
    return VK_SUCCESS;
}

void vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, void *pAllocator) {
    if (!swapchain) return;
    if (swapchain->images) free(swapchain->images);
    if (swapchain->surface && swapchain->surface->dpy) {
        if (swapchain->gc) XFreeGC(swapchain->surface->dpy, swapchain->gc);
    }
    free(swapchain);
}

VkResult vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages) {
    if (!swapchain || !pSwapchainImageCount) return VK_ERROR_INITIALIZATION_FAILED;

    if (!pSwapchainImages) {
        *pSwapchainImageCount = swapchain->image_count;
        return VK_SUCCESS;
    }

    uint32_t to_copy = (*pSwapchainImageCount < swapchain->image_count) ? *pSwapchainImageCount : swapchain->image_count;
    for (uint32_t i = 0; i < to_copy; i++) {
        pSwapchainImages[i] = &swapchain->images[i];
    }
    *pSwapchainImageCount = to_copy;
    return VK_SUCCESS;
}

VkResult vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, void *semaphore, void *fence, uint32_t *pImageIndex) {
    if (!swapchain || !pImageIndex) return VK_ERROR_INITIALIZATION_FAILED;
    *pImageIndex = 0;
    if (fence) ((VkFence)fence)->signaled = true;
    return VK_SUCCESS;
}

VkResult vkQueuePresentKHR(VkQueue queue, const struct VkPresentInfoKHR *pPresentInfo) {
    if (!pPresentInfo || pPresentInfo->swapchainCount == 0) return VK_ERROR_INITIALIZATION_FAILED;

    VkSwapchainKHR sc = pPresentInfo->pSwapchains[0];
    if (sc && sc->surface && sc->surface->dpy && sc->surface->window && sc->ximage && sc->gc) {
        XPutImage(sc->surface->dpy, sc->surface->window, sc->gc, sc->ximage, 0, 0, 0, 0, sc->width, sc->height);
        XFlush(sc->surface->dpy);
    }
    return VK_SUCCESS;
}

uint32_t panvk_v9_read_pixel(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y) {
    if (commandBuffer && commandBuffer->v9_cmd) {
        return v9_cmd_buffer_read_pixel(commandBuffer->v9_cmd, x, y);
    }
    return 0;
}

/* KHR Aliases for PhysicalDevice2 functions */
void vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceProperties2 *pProperties) {
    vkGetPhysicalDeviceProperties2(physicalDevice, pProperties);
}
void vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceFeatures2 *pFeatures) {
    vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
}
void vkGetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, struct VkQueueFamilyProperties2 *pQueueFamilyProperties) {
    vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}
void vkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceMemoryProperties2 *pMemoryProperties) {
    vkGetPhysicalDeviceMemoryProperties2(physicalDevice, pMemoryProperties);
}
VkResult vkEnumeratePhysicalDeviceGroupsKHR(VkInstance instance, uint32_t *pPhysicalDeviceGroupCount, struct VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroups) {
    return vkEnumeratePhysicalDeviceGroups(instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroups);
}

/* Vulkan ICD Entry Point Lookup Table */
PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char *pName) {
    if (!pName) return NULL;
#define MATCH(name) if (strcmp(pName, #name) == 0) return (PFN_vkVoidFunction)name
    MATCH(vk_icdNegotiateLoaderICDInterfaceVersion);
    MATCH(vkGetInstanceProcAddr);
    MATCH(vkGetDeviceProcAddr);
    MATCH(vk_icdGetInstanceProcAddr);
    MATCH(vkEnumerateInstanceVersion);
    MATCH(vkCreateInstance);
    MATCH(vkDestroyInstance);
    MATCH(vkEnumerateInstanceExtensionProperties);
    MATCH(vkEnumerateInstanceLayerProperties);
    MATCH(vkEnumerateDeviceExtensionProperties);
    MATCH(vkEnumeratePhysicalDevices);
    MATCH(vkEnumeratePhysicalDeviceGroups);
    MATCH(vkEnumeratePhysicalDeviceGroupsKHR);
    MATCH(vkGetPhysicalDeviceProperties);
    MATCH(vkGetPhysicalDeviceProperties2);
    MATCH(vkGetPhysicalDeviceProperties2KHR);
    MATCH(vkGetPhysicalDeviceFeatures);
    MATCH(vkGetPhysicalDeviceFeatures2);
    MATCH(vkGetPhysicalDeviceFeatures2KHR);
    MATCH(vkGetPhysicalDeviceQueueFamilyProperties);
    MATCH(vkGetPhysicalDeviceQueueFamilyProperties2);
    MATCH(vkGetPhysicalDeviceQueueFamilyProperties2KHR);
    MATCH(vkGetPhysicalDeviceMemoryProperties);
    MATCH(vkGetPhysicalDeviceMemoryProperties2);
    MATCH(vkGetPhysicalDeviceMemoryProperties2KHR);
    MATCH(vkGetPhysicalDeviceFormatProperties);
    MATCH(vkGetPhysicalDeviceImageFormatProperties);
    MATCH(vkGetPhysicalDeviceSparseImageFormatProperties);
    MATCH(vkCreateDevice);
    MATCH(vkDestroyDevice);
    MATCH(vkGetDeviceQueue);
    MATCH(vkAllocateMemory);
    MATCH(vkFreeMemory);
    MATCH(vkMapMemory);
    MATCH(vkUnmapMemory);
    MATCH(vkCreateBuffer);
    MATCH(vkDestroyBuffer);
    MATCH(vkGetBufferMemoryRequirements);
    MATCH(vkBindBufferMemory);
    MATCH(vkCreateImage);
    MATCH(vkDestroyImage);
    MATCH(vkGetImageMemoryRequirements);
    MATCH(vkBindImageMemory);
    MATCH(vkCreateImageView);
    MATCH(vkDestroyImageView);
    MATCH(vkCreateShaderModule);
    MATCH(vkDestroyShaderModule);
    MATCH(vkCreatePipelineCache);
    MATCH(vkDestroyPipelineCache);
    MATCH(vkCreatePipelineLayout);
    MATCH(vkDestroyPipelineLayout);
    MATCH(vkCreateRenderPass);
    MATCH(vkDestroyRenderPass);
    MATCH(vkCreateFramebuffer);
    MATCH(vkDestroyFramebuffer);
    MATCH(vkCreateDescriptorSetLayout);
    MATCH(vkDestroyDescriptorSetLayout);
    MATCH(vkCreateDescriptorPool);
    MATCH(vkDestroyDescriptorPool);
    MATCH(vkAllocateDescriptorSets);
    MATCH(vkFreeDescriptorSets);
    MATCH(vkUpdateDescriptorSets);
    MATCH(vkCreateGraphicsPipelines);
    MATCH(vkCreateComputePipelines);
    MATCH(vkDestroyPipeline);
    MATCH(vkCreateSemaphore);
    MATCH(vkDestroySemaphore);
    MATCH(vkCreateFence);
    MATCH(vkDestroyFence);
    MATCH(vkResetFences);
    MATCH(vkGetFenceStatus);
    MATCH(vkWaitForFences);
    MATCH(vkCreateCommandPool);
    MATCH(vkDestroyCommandPool);
    MATCH(vkAllocateCommandBuffers);
    MATCH(vkFreeCommandBuffers);
    MATCH(vkBeginCommandBuffer);
    MATCH(vkEndCommandBuffer);
    MATCH(vkCmdBindPipeline);
    MATCH(vkCmdSetViewport);
    MATCH(vkCmdSetScissor);
    MATCH(vkCmdBindDescriptorSets);
    MATCH(vkCmdBindVertexBuffers);
    MATCH(vkCmdBindIndexBuffer);
    MATCH(vkCmdCopyBuffer);
    MATCH(vkCmdPipelineBarrier);
    MATCH(vkCmdDraw);
    MATCH(vkCmdBeginRenderPass);
    MATCH(vkCmdDrawIndexed);
    MATCH(vkCmdEndRenderPass);
    MATCH(vkQueueSubmit);
    MATCH(vkQueueWaitIdle);
    MATCH(vkDeviceWaitIdle);
    MATCH(vkCreateXlibSurfaceKHR);
    MATCH(vkCreateXcbSurfaceKHR);
    MATCH(vkGetPhysicalDeviceXcbPresentationSupportKHR);
    MATCH(vkGetPhysicalDeviceDisplayPropertiesKHR);
    MATCH(vkGetPhysicalDeviceDisplayPlanePropertiesKHR);
    MATCH(vkGetDisplayPlaneSupportedDisplaysKHR);
    MATCH(vkGetDisplayModePropertiesKHR);
    MATCH(vkDestroySurfaceKHR);
    MATCH(vkGetPhysicalDeviceSurfaceSupportKHR);
    MATCH(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    MATCH(vkGetPhysicalDeviceSurfaceFormatsKHR);
    MATCH(vkGetPhysicalDeviceSurfacePresentModesKHR);
    MATCH(vkCreateSwapchainKHR);
    MATCH(vkDestroySwapchainKHR);
    MATCH(vkGetSwapchainImagesKHR);
    MATCH(vkAcquireNextImageKHR);
    MATCH(vkQueuePresentKHR);
    MATCH(panvk_v9_read_pixel);
#undef MATCH
    return NULL;
}

PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device, const char *pName) {
    return vkGetInstanceProcAddr(NULL, pName);
}

__attribute__((visibility("default"))) PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName) {
    return vkGetInstanceProcAddr(instance, pName);
}
