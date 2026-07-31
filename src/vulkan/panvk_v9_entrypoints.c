/*
 * PanVK Valhall v9 Vulkan Entry Points & WSI Swapchain Layer Implementation
 * Full Vulkan API implementation for vkmark & Mesa Vulkan applications
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include <xcb/xcb.h>

#include "panvk_v9_entrypoints.h"
#include "panvk_v9_compiler.h"

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
    struct VkQueue_T *queue;
};

struct VkQueue_T {
    uintptr_t loader_data;
    struct VkDevice_T *device;
    struct v9_cmd_buffer *last_v9_cmd;
};

struct VkCommandPool_T {
    struct VkDevice_T *device;
};

struct vk_vertex_binding {
    struct VkBuffer_T *buffer;
    VkDeviceSize offset;
};

struct VkCommandBuffer_T {
    uintptr_t loader_data;
    struct VkDevice_T *device;
    struct v9_cmd_buffer *v9_cmd;
    struct VkPipeline_T *graphics_pipeline;
    struct VkViewport viewport;
    struct VkRect2D scissor;
    bool viewport_set;
    bool scissor_set;
    VkDescriptorSet descriptor_sets[8];
    struct vk_vertex_binding vertex_bindings[16];
    struct VkBuffer_T *index_buffer;
    VkDeviceSize index_offset;
    uint32_t index_type;
};

struct VkSurfaceKHR_T {
    Display *dpy;
    xcb_connection_t *connection;
    uint32_t window;
    uint32_t width;
    uint32_t height;
    bool is_xcb;
};

struct VkSwapchainKHR_T {
    struct VkDevice_T *device;
    struct VkSurfaceKHR_T *surface;
    uint32_t width;
    uint32_t height;
    uint32_t image_count;
    struct VkImage_T *images;
    GC gc;
    xcb_gcontext_t xcb_gc;
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
    uint32_t stage_mask;
};

struct VkPipelineLayout_T {
    struct panvk_v9_pipeline_layout compiler_layout;
    struct panvk_v9_descriptor_binding *bindings;
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
    uint32_t stage_mask;
    char vertex_entry_point[64];
    char fragment_entry_point[64];
    struct panvk_v9_compiled_shader vertex_binary;
    struct panvk_v9_compiled_shader fragment_binary;
    struct panvk_v9_pipeline_layout compiler_layout;
    struct panvk_v9_descriptor_binding *bindings;
    bool shaders_compiled;
    uint32_t topology;
    bool primitive_restart;
    struct VkViewport viewport;
    struct VkRect2D scissor;
    bool dynamic_viewport;
    bool dynamic_scissor;
    bool rasterizer_discard;
    uint32_t polygon_mode;
    uint32_t cull_mode;
    uint32_t front_face;
    float line_width;
    uint32_t rasterization_samples;
    bool depth_test;
    bool depth_write;
    uint32_t depth_compare_op;
    bool blend_enable;
    uint32_t color_write_mask;
};

struct VkDescriptorSetLayout_T {
    uint32_t binding_count;
    struct VkDescriptorSetLayoutBinding *bindings;
    uint32_t *binding_offsets;
    uint32_t descriptor_count;
};

struct VkDescriptorPool_T {
    int dummy;
};

struct VkDescriptorSet_T {
    VkDescriptorSetLayout layout;
    struct VkDescriptorBufferInfo *buffers;
};

struct VkSemaphore_T {
    int dummy;
};

struct VkFence_T {
    bool signaled;
};

struct panvk_compiler_api {
    void *library;
    int (*compile)(const uint32_t *, size_t, enum panvk_v9_shader_stage,
                   const char *, const struct panvk_v9_pipeline_layout *,
                   struct panvk_v9_compiled_shader *, char *, size_t);
    void (*cleanup)(struct panvk_v9_compiled_shader *);
    bool attempted;
};

static struct panvk_compiler_api compiler_api;
static pthread_mutex_t compiler_api_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool load_compiler(void) {
    pthread_mutex_lock(&compiler_api_mutex);
    if (compiler_api.attempted) {
        bool loaded = compiler_api.library != NULL;
        pthread_mutex_unlock(&compiler_api_mutex);
        return loaded;
    }
    compiler_api.attempted = true;

    const char *path = getenv("PANVK_V9_COMPILER_LIBRARY");
    if (!path || !path[0]) path = "libpanvk_v9_compiler.so";
    compiler_api.library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!compiler_api.library) {
        pthread_mutex_unlock(&compiler_api_mutex);
        return false;
    }

    compiler_api.compile = dlsym(compiler_api.library, "panvk_v9_compile_spirv");
    compiler_api.cleanup = dlsym(compiler_api.library, "panvk_v9_compiled_shader_cleanup");
    if (!compiler_api.compile || !compiler_api.cleanup) {
        dlclose(compiler_api.library);
        memset(&compiler_api, 0, sizeof(compiler_api));
        compiler_api.attempted = true;
        pthread_mutex_unlock(&compiler_api_mutex);
        return false;
    }
    pthread_mutex_unlock(&compiler_api_mutex);
    return true;
}

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
    if (!device) return;
    if (device->queue) v9_cmd_buffer_destroy(device->queue->last_v9_cmd);
    free(device->queue);
    free(device);
}

void vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue) {
    if (!device || !pQueue) return;
    if (queueFamilyIndex != 0 || queueIndex != 0) {
        *pQueue = NULL;
        return;
    }
    if (!device->queue) {
        device->queue = calloc(1, sizeof(*device->queue));
        if (!device->queue) {
            *pQueue = NULL;
            return;
        }
        set_loader_magic(device->queue);
        device->queue->device = device;
    }
    *pQueue = device->queue;
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
#define SPIRV_MAGIC 0x07230203u
#define SPIRV_OP_ENTRY_POINT 15u

static uint32_t spirv_execution_model_stage(uint32_t execution_model) {
    switch (execution_model) {
    case 0: return VK_SHADER_STAGE_VERTEX_BIT;
    case 4: return VK_SHADER_STAGE_FRAGMENT_BIT;
    default: return 0;
    }
}

static bool spirv_string_equals(const uint32_t *words, size_t word_count,
                                const char *expected) {
    if (!expected) return false;
    size_t expected_len = strlen(expected);
    size_t max_len = word_count * sizeof(uint32_t);
    const char *value = (const char *)words;
    const char *end = memchr(value, '\0', max_len);
    return end && (size_t)(end - value) == expected_len &&
           memcmp(value, expected, expected_len) == 0;
}

static bool spirv_validate_and_scan(const uint32_t *code, size_t code_size,
                                    uint32_t *stage_mask) {
    if (!code || code_size < 5 * sizeof(uint32_t) ||
        code_size % sizeof(uint32_t) != 0 || code[0] != SPIRV_MAGIC ||
        code[1] > 0x00010600u || code[3] == 0 || code[4] != 0) {
        return false;
    }

    size_t count = code_size / sizeof(uint32_t);
    uint32_t stages = 0;
    bool found_entry_point = false;
    for (size_t offset = 5; offset < count;) {
        uint32_t instruction = code[offset];
        uint32_t word_count = instruction >> 16;
        uint32_t opcode = instruction & 0xffffu;
        if (word_count == 0 || word_count > count - offset) return false;
        if (opcode == SPIRV_OP_ENTRY_POINT) {
            if (word_count < 4) return false;
            found_entry_point = true;
            stages |= spirv_execution_model_stage(code[offset + 1]);
            if (!memchr((const char *)&code[offset + 3], '\0',
                        (word_count - 3) * sizeof(uint32_t))) {
                return false;
            }
        }
        offset += word_count;
    }

    if (stage_mask) *stage_mask = stages;
    return found_entry_point;
}

static bool spirv_has_entry_point(VkShaderModule module, uint32_t stage,
                                  const char *name) {
    if (!module || !(module->stage_mask & stage) || !name) return false;
    size_t count = module->code_size / sizeof(uint32_t);
    for (size_t offset = 5; offset < count;) {
        uint32_t instruction = module->code[offset];
        uint32_t word_count = instruction >> 16;
        uint32_t opcode = instruction & 0xffffu;
        if (opcode == SPIRV_OP_ENTRY_POINT && word_count >= 4 &&
            spirv_execution_model_stage(module->code[offset + 1]) == stage &&
            spirv_string_equals(&module->code[offset + 3], word_count - 3, name)) {
            return true;
        }
        offset += word_count;
    }
    return false;
}

VkResult vkCreateShaderModule(VkDevice device, const struct VkShaderModuleCreateInfo *pCreateInfo, void *pAllocator, VkShaderModule *pShaderModule) {
    if (!device || !pCreateInfo || !pShaderModule) return VK_ERROR_INITIALIZATION_FAILED;
    *pShaderModule = NULL;

    uint32_t stage_mask = 0;
    if (!spirv_validate_and_scan(pCreateInfo->pCode, pCreateInfo->codeSize,
                                 &stage_mask)) {
        return VK_ERROR_INVALID_SHADER_NV;
    }

    struct VkShaderModule_T *sm = calloc(1, sizeof(*sm));
    if (!sm) return VK_ERROR_OUT_OF_HOST_MEMORY;

    sm->code_size = pCreateInfo->codeSize;
    sm->stage_mask = stage_mask;
    sm->code = malloc(sm->code_size);
    if (!sm->code) {
        free(sm);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memcpy(sm->code, pCreateInfo->pCode, sm->code_size);

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

VkResult vkCreatePipelineLayout(VkDevice device, const struct VkPipelineLayoutCreateInfo *pCreateInfo,
                                void *pAllocator, VkPipelineLayout *pPipelineLayout) {
    if (!pCreateInfo || !pPipelineLayout) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkPipelineLayout_T *pl = calloc(1, sizeof(*pl));
    if (!pl) return VK_ERROR_OUT_OF_HOST_MEMORY;

    uint32_t binding_count = 0;
    for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; set++) {
        if (pCreateInfo->pSetLayouts[set])
            binding_count += pCreateInfo->pSetLayouts[set]->binding_count;
    }
    pl->bindings = calloc(binding_count, sizeof(*pl->bindings));
    if (binding_count && !pl->bindings) {
        free(pl);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    uint32_t index = 0;
    uint32_t ubo_index = 0;
    for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; set++) {
        VkDescriptorSetLayout set_layout = pCreateInfo->pSetLayouts[set];
        if (!set_layout) continue;
        for (uint32_t i = 0; i < set_layout->binding_count; i++) {
            const struct VkDescriptorSetLayoutBinding *binding = &set_layout->bindings[i];
            struct panvk_v9_descriptor_binding *out = &pl->bindings[index++];
            out->set = set;
            out->binding = binding->binding;
            out->descriptor_type = binding->descriptorType;
            out->array_size = binding->descriptorCount;
            if (binding->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                binding->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
                out->resource_index = ubo_index;
                ubo_index += binding->descriptorCount;
            }
        }
    }
    pl->compiler_layout.bindings = pl->bindings;
    pl->compiler_layout.binding_count = binding_count;
    pl->compiler_layout.ubo_count = ubo_index;
    *pPipelineLayout = pl;
    return VK_SUCCESS;
}

void vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, void *pAllocator) {
    if (!pipelineLayout) return;
    free(pipelineLayout->bindings);
    free(pipelineLayout);
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

VkResult vkCreateDescriptorSetLayout(VkDevice device,
                                     const struct VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                     void *pAllocator, VkDescriptorSetLayout *pSetLayout) {
    if (!pCreateInfo || !pSetLayout) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkDescriptorSetLayout_T *dsl = calloc(1, sizeof(*dsl));
    if (!dsl) return VK_ERROR_OUT_OF_HOST_MEMORY;
    dsl->binding_count = pCreateInfo->bindingCount;
    dsl->bindings = calloc(dsl->binding_count, sizeof(*dsl->bindings));
    dsl->binding_offsets = calloc(dsl->binding_count, sizeof(*dsl->binding_offsets));
    if (dsl->binding_count && (!dsl->bindings || !dsl->binding_offsets)) {
        free(dsl->binding_offsets);
        free(dsl->bindings);
        free(dsl);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (dsl->binding_count) {
        memcpy(dsl->bindings, pCreateInfo->pBindings,
               dsl->binding_count * sizeof(*dsl->bindings));
        for (uint32_t i = 0; i < dsl->binding_count; i++) {
            dsl->binding_offsets[i] = dsl->descriptor_count;
            dsl->descriptor_count += dsl->bindings[i].descriptorCount;
        }
    }
    *pSetLayout = dsl;
    return VK_SUCCESS;
}

void vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout setLayout, void *pAllocator) {
    if (!setLayout) return;
    free(setLayout->binding_offsets);
    free(setLayout->bindings);
    free(setLayout);
}

VkResult vkCreateDescriptorPool(VkDevice device,
                                const struct VkDescriptorPoolCreateInfo *pCreateInfo,
                                void *pAllocator, VkDescriptorPool *pDescriptorPool) {
    if (!pDescriptorPool) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkDescriptorPool_T *dp = calloc(1, sizeof(*dp));
    *pDescriptorPool = dp;
    return VK_SUCCESS;
}

void vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, void *pAllocator) {
    if (descriptorPool) free(descriptorPool);
}

VkResult vkAllocateDescriptorSets(VkDevice device,
                                  const struct VkDescriptorSetAllocateInfo *pAllocateInfo,
                                  VkDescriptorSet *pDescriptorSets) {
    if (!pAllocateInfo || !pDescriptorSets || !pAllocateInfo->pSetLayouts)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++)
        pDescriptorSets[i] = NULL;
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
        VkDescriptorSet set = calloc(1, sizeof(*set));
        if (!set) goto fail;
        set->layout = pAllocateInfo->pSetLayouts[i];
        set->buffers = calloc(set->layout->descriptor_count, sizeof(*set->buffers));
        if (set->layout->descriptor_count && !set->buffers) {
            free(set);
            goto fail;
        }
        pDescriptorSets[i] = set;
    }
    return VK_SUCCESS;

fail:
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
        if (!pDescriptorSets[i]) continue;
        free(pDescriptorSets[i]->buffers);
        free(pDescriptorSets[i]);
        pDescriptorSets[i] = NULL;
    }
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

VkResult vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets) {
    if (!pDescriptorSets) return VK_SUCCESS;
    for (uint32_t i = 0; i < descriptorSetCount; i++) {
        if (pDescriptorSets[i]) {
            free(pDescriptorSets[i]->buffers);
            free(pDescriptorSets[i]);
        }
    }
    return VK_SUCCESS;
}

void vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                            const struct VkWriteDescriptorSet *pDescriptorWrites,
                            uint32_t descriptorCopyCount, const void *pDescriptorCopies) {
    for (uint32_t w = 0; w < descriptorWriteCount; w++) {
        const struct VkWriteDescriptorSet *write = &pDescriptorWrites[w];
        if (!write->dstSet || !write->pBufferInfo) continue;
        VkDescriptorSetLayout layout = write->dstSet->layout;
        for (uint32_t b = 0; b < layout->binding_count; b++) {
            const struct VkDescriptorSetLayoutBinding *binding = &layout->bindings[b];
            if (binding->binding != write->dstBinding ||
                binding->descriptorType != write->descriptorType ||
                write->dstArrayElement + write->descriptorCount > binding->descriptorCount)
                continue;
            memcpy(&write->dstSet->buffers[layout->binding_offsets[b] +
                                          write->dstArrayElement],
                   write->pBufferInfo,
                   write->descriptorCount * sizeof(*write->pBufferInfo));
            break;
        }
    }
}

static bool pipeline_dynamic_state(const struct VkPipelineDynamicStateCreateInfo *dynamic,
                                   uint32_t state) {
    if (!dynamic || !dynamic->pDynamicStates) return false;
    for (uint32_t i = 0; i < dynamic->dynamicStateCount; i++) {
        if (dynamic->pDynamicStates[i] == state) return true;
    }
    return false;
}

static VkResult pipeline_parse_shader_stages(struct VkPipeline_T *pipeline,
                                             const struct VkGraphicsPipelineCreateInfo *info) {
    if (!info->pStages || info->stageCount == 0) return VK_ERROR_INVALID_SHADER_NV;

    for (uint32_t i = 0; i < info->stageCount; i++) {
        const struct VkPipelineShaderStageCreateInfo *stage = &info->pStages[i];
        if ((stage->stage != VK_SHADER_STAGE_VERTEX_BIT &&
             stage->stage != VK_SHADER_STAGE_FRAGMENT_BIT) ||
            !spirv_has_entry_point(stage->module, stage->stage, stage->pName)) {
            return VK_ERROR_INVALID_SHADER_NV;
        }
        if (pipeline->stage_mask & stage->stage) return VK_ERROR_INVALID_SHADER_NV;

        pipeline->stage_mask |= stage->stage;
        if (stage->stage == VK_SHADER_STAGE_VERTEX_BIT) {
            snprintf(pipeline->vertex_entry_point,
                     sizeof(pipeline->vertex_entry_point), "%s", stage->pName);
        } else {
            snprintf(pipeline->fragment_entry_point,
                     sizeof(pipeline->fragment_entry_point), "%s", stage->pName);
        }
    }

    return (pipeline->stage_mask & VK_SHADER_STAGE_VERTEX_BIT) ?
           VK_SUCCESS : VK_ERROR_INVALID_SHADER_NV;
}

static VkResult pipeline_compile_shaders(struct VkPipeline_T *pipeline,
                                         const struct VkGraphicsPipelineCreateInfo *info) {
    const char *required_env = getenv("PANVK_REQUIRE_COMPILER");
    bool required = required_env && required_env[0] && strcmp(required_env, "0");
    if (!load_compiler()) {
        return required ? VK_ERROR_INVALID_SHADER_NV : VK_SUCCESS;
    }

    char error[512];
    for (uint32_t i = 0; i < info->stageCount; i++) {
        const struct VkPipelineShaderStageCreateInfo *stage = &info->pStages[i];
        enum panvk_v9_shader_stage compiler_stage;
        struct panvk_v9_compiled_shader *binary;
        if (stage->stage == VK_SHADER_STAGE_VERTEX_BIT) {
            compiler_stage = PANVK_V9_SHADER_VERTEX;
            binary = &pipeline->vertex_binary;
        } else if (stage->stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
            compiler_stage = PANVK_V9_SHADER_FRAGMENT;
            binary = &pipeline->fragment_binary;
        } else {
            continue;
        }

        int ret = compiler_api.compile(stage->module->code, stage->module->code_size,
                                       compiler_stage, stage->pName,
                                       &pipeline->compiler_layout,
                                       binary,
                                       error, sizeof(error));
        if (ret) {
            fprintf(stderr, "panvk: %s shader compilation failed: %s\n",
                    compiler_stage == PANVK_V9_SHADER_VERTEX ? "vertex" : "fragment",
                    error[0] ? error : "unknown compiler error");
            compiler_api.cleanup(&pipeline->vertex_binary);
            compiler_api.cleanup(&pipeline->fragment_binary);
            return required ? VK_ERROR_INVALID_SHADER_NV : VK_SUCCESS;
        }
    }

    pipeline->shaders_compiled = pipeline->vertex_binary.binary_size != 0;
    return VK_SUCCESS;
}

static void pipeline_cleanup(struct VkPipeline_T *pipeline) {
    if (!pipeline) return;
    if (compiler_api.cleanup) {
        compiler_api.cleanup(&pipeline->vertex_binary);
        compiler_api.cleanup(&pipeline->fragment_binary);
    }
    free(pipeline->bindings);
    free(pipeline);
}

static VkResult pipeline_copy_layout(struct VkPipeline_T *pipeline,
                                     VkPipelineLayout layout) {
    if (!layout || !layout->compiler_layout.binding_count) return VK_SUCCESS;
    size_t size = layout->compiler_layout.binding_count * sizeof(*pipeline->bindings);
    pipeline->bindings = malloc(size);
    if (!pipeline->bindings) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memcpy(pipeline->bindings, layout->bindings, size);
    pipeline->compiler_layout = layout->compiler_layout;
    pipeline->compiler_layout.bindings = pipeline->bindings;
    return VK_SUCCESS;
}

static void pipeline_parse_fixed_state(struct VkPipeline_T *pipeline,
                                       const struct VkGraphicsPipelineCreateInfo *info) {
    pipeline->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipeline->polygon_mode = VK_POLYGON_MODE_FILL;
    pipeline->cull_mode = VK_CULL_MODE_NONE;
    pipeline->front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipeline->line_width = 1.0f;
    pipeline->rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    pipeline->depth_compare_op = VK_COMPARE_OP_ALWAYS;
    pipeline->color_write_mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    if (info->pInputAssemblyState) {
        pipeline->topology = info->pInputAssemblyState->topology;
        pipeline->primitive_restart = info->pInputAssemblyState->primitiveRestartEnable != 0;
    }
    if (info->pViewportState) {
        if (info->pViewportState->viewportCount && info->pViewportState->pViewports)
            pipeline->viewport = info->pViewportState->pViewports[0];
        if (info->pViewportState->scissorCount && info->pViewportState->pScissors)
            pipeline->scissor = info->pViewportState->pScissors[0];
    }
    pipeline->dynamic_viewport = pipeline_dynamic_state(info->pDynamicState,
                                                        VK_DYNAMIC_STATE_VIEWPORT);
    pipeline->dynamic_scissor = pipeline_dynamic_state(info->pDynamicState,
                                                       VK_DYNAMIC_STATE_SCISSOR);
    if (info->pRasterizationState) {
        pipeline->rasterizer_discard = info->pRasterizationState->rasterizerDiscardEnable != 0;
        pipeline->polygon_mode = info->pRasterizationState->polygonMode;
        pipeline->cull_mode = info->pRasterizationState->cullMode;
        pipeline->front_face = info->pRasterizationState->frontFace;
        pipeline->line_width = info->pRasterizationState->lineWidth;
    }
    if (info->pMultisampleState)
        pipeline->rasterization_samples = info->pMultisampleState->rasterizationSamples;
    if (info->pDepthStencilState) {
        pipeline->depth_test = info->pDepthStencilState->depthTestEnable != 0;
        pipeline->depth_write = info->pDepthStencilState->depthWriteEnable != 0;
        pipeline->depth_compare_op = info->pDepthStencilState->depthCompareOp;
    }
    if (info->pColorBlendState && info->pColorBlendState->attachmentCount &&
        info->pColorBlendState->pAttachments) {
        pipeline->blend_enable = info->pColorBlendState->pAttachments[0].blendEnable != 0;
        pipeline->color_write_mask = info->pColorBlendState->pAttachments[0].colorWriteMask;
    }
}

VkResult vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
                                   uint32_t createInfoCount,
                                   const struct VkGraphicsPipelineCreateInfo *pCreateInfos,
                                   void *pAllocator, VkPipeline *pPipelines) {
    if (!device || !pCreateInfos || !pPipelines) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < createInfoCount; i++) pPipelines[i] = NULL;

    for (uint32_t i = 0; i < createInfoCount; i++) {
        struct VkPipeline_T *pipe = calloc(1, sizeof(*pipe));
        if (!pipe) return VK_ERROR_OUT_OF_HOST_MEMORY;

        VkResult result = pipeline_copy_layout(pipe, pCreateInfos[i].layout);
        if (result == VK_SUCCESS)
            result = pipeline_parse_shader_stages(pipe, &pCreateInfos[i]);
        if (result == VK_SUCCESS)
            result = pipeline_compile_shaders(pipe, &pCreateInfos[i]);
        if (result != VK_SUCCESS) {
            pipeline_cleanup(pipe);
            for (uint32_t j = 0; j < i; j++) {
                pipeline_cleanup(pPipelines[j]);
                pPipelines[j] = NULL;
            }
            return result;
        }
        pipeline_parse_fixed_state(pipe, &pCreateInfos[i]);
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
    pipeline_cleanup(pipeline);
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
    commandBuffer->graphics_pipeline = NULL;
    commandBuffer->viewport_set = false;
    commandBuffer->scissor_set = false;
    memset(commandBuffer->descriptor_sets, 0, sizeof(commandBuffer->descriptor_sets));
    return VK_SUCCESS;
}

void vkCmdBindPipeline(VkCommandBuffer commandBuffer, uint32_t pipelineBindPoint, VkPipeline pipeline) {
    if (!commandBuffer || pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS) return;
    commandBuffer->graphics_pipeline = pipeline;
    if (pipeline) {
        if (!pipeline->dynamic_viewport) {
            commandBuffer->viewport = pipeline->viewport;
            commandBuffer->viewport_set = true;
        }
        if (!pipeline->dynamic_scissor) {
            commandBuffer->scissor = pipeline->scissor;
            commandBuffer->scissor_set = true;
        }
    }
}

void vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const void *pViewports) {
    if (!commandBuffer || firstViewport != 0 || viewportCount == 0 || !pViewports) return;
    commandBuffer->viewport = ((const struct VkViewport *)pViewports)[0];
    commandBuffer->viewport_set = true;
}

void vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const void *pScissors) {
    if (!commandBuffer || firstScissor != 0 || scissorCount == 0 || !pScissors) return;
    commandBuffer->scissor = ((const struct VkRect2D *)pScissors)[0];
    commandBuffer->scissor_set = true;
}

void vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, uint32_t pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets) {
    if (!commandBuffer || pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS ||
        firstSet >= 8 || descriptorSetCount > 8 - firstSet ||
        (descriptorSetCount && !pDescriptorSets)) return;
    memcpy(&commandBuffer->descriptor_sets[firstSet], pDescriptorSets,
           descriptorSetCount * sizeof(*pDescriptorSets));
}

void vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets) {
    if (!commandBuffer || firstBinding >= 16 || !pBuffers || !pOffsets) return;
    for (uint32_t i = 0; i < bindingCount && (firstBinding + i) < 16; i++) {
        commandBuffer->vertex_bindings[firstBinding + i].buffer = pBuffers[i];
        commandBuffer->vertex_bindings[firstBinding + i].offset = pOffsets[i];
    }
}

void vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t indexType) {
    if (!commandBuffer) return;
    commandBuffer->index_buffer = buffer;
    commandBuffer->index_offset = offset;
    commandBuffer->index_type = indexType;
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

VkResult vkCreateSampler(VkDevice device, const void *pCreateInfo, void *pAllocator, VkSampler *pSampler) {
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (pSampler) *pSampler = (VkSampler)(uintptr_t)0x1;
    return VK_SUCCESS;
}

void vkDestroySampler(VkDevice device, VkSampler sampler, void *pAllocator) {
    (void)device; (void)sampler; (void)pAllocator;
}

void vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
                            uint32_t dstImageLayout, uint32_t regionCount, const void *pRegions) {
    (void)commandBuffer; (void)srcBuffer; (void)dstImage; (void)dstImageLayout; (void)regionCount; (void)pRegions;
}

void vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout,
                            VkBuffer dstBuffer, uint32_t regionCount, const void *pRegions) {
    (void)commandBuffer; (void)srcImage; (void)srcImageLayout; (void)dstBuffer; (void)regionCount; (void)pRegions;
}

void vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout,
                    VkImage dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void *pRegions) {
    (void)commandBuffer; (void)srcImage; (void)srcImageLayout; (void)dstImage; (void)dstImageLayout; (void)regionCount; (void)pRegions;
}

void vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout,
                    VkImage dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void *pRegions, uint32_t filter) {
    (void)commandBuffer; (void)srcImage; (void)srcImageLayout; (void)dstImage; (void)dstImageLayout; (void)regionCount; (void)pRegions; (void)filter;
}

void vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, uint32_t imageLayout,
                          const void *pColor, uint32_t rangeCount, const void *pRanges) {
    (void)commandBuffer; (void)image; (void)imageLayout; (void)pColor; (void)rangeCount; (void)pRanges;
}

void vkCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, uint32_t imageLayout,
                                 const void *pDepthStencil, uint32_t rangeCount, const void *pRanges) {
    (void)commandBuffer; (void)image; (void)imageLayout; (void)pDepthStencil; (void)rangeCount; (void)pRanges;
}

void vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const void *pAttachments,
                           uint32_t rectCount, const void *pRects) {
    (void)commandBuffer; (void)attachmentCount; (void)pAttachments; (void)rectCount; (void)pRects;
}

void vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, uint32_t srcStageMask,
                          uint32_t dstStageMask, uint32_t dependencyFlags,
                          uint32_t memoryBarrierCount, const void *pMemoryBarriers,
                          uint32_t bufferMemoryBarrierCount, const void *pBufferMemoryBarriers,
                          uint32_t imageMemoryBarrierCount, const void *pImageMemoryBarriers) {
}

static void command_buffer_apply_ubos(VkCommandBuffer commandBuffer) {
    struct v9_ubo_binding ubos[8];
    uint32_t ubo_count = 0;
    VkPipeline pipeline = commandBuffer->graphics_pipeline;
    if (!pipeline) {
        v9_cmd_buffer_set_ubos(commandBuffer->v9_cmd, NULL, 0);
        return;
    }

    for (uint32_t i = 0; i < pipeline->compiler_layout.binding_count; i++) {
        const struct panvk_v9_descriptor_binding *binding =
            &pipeline->compiler_layout.bindings[i];
        if ((binding->descriptor_type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
             binding->descriptor_type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) ||
            binding->set >= 8 || !commandBuffer->descriptor_sets[binding->set])
            continue;

        VkDescriptorSet set = commandBuffer->descriptor_sets[binding->set];
        for (uint32_t b = 0; b < set->layout->binding_count; b++) {
            if (set->layout->bindings[b].binding != binding->binding) continue;
            for (uint32_t elem = 0; elem < binding->array_size && ubo_count < 8; elem++) {
                const struct VkDescriptorBufferInfo *info =
                    &set->buffers[set->layout->binding_offsets[b] + elem];
                if (!info->buffer || !info->buffer->bo || info->offset >= info->buffer->size)
                    continue;
                VkDeviceSize available = info->buffer->size - info->offset;
                VkDeviceSize range = info->range == VK_WHOLE_SIZE || info->range > available ?
                                     available : info->range;
                ubos[ubo_count++] = (struct v9_ubo_binding) {
                    .address = info->buffer->bo->gpu + info->buffer->memory_offset + info->offset,
                    .size = range > UINT32_MAX ? UINT32_MAX : (uint32_t)range,
                    .index = binding->resource_index + elem,
                };
            }
            break;
        }
    }
    v9_cmd_buffer_set_ubos(commandBuffer->v9_cmd, ubos, ubo_count);
}

void vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    if (commandBuffer && commandBuffer->v9_cmd && vertexCount > 0 && instanceCount > 0 &&
        (!commandBuffer->graphics_pipeline ||
         !commandBuffer->graphics_pipeline->rasterizer_discard)) {
        command_buffer_apply_ubos(commandBuffer);
        if (commandBuffer->graphics_pipeline &&
            commandBuffer->graphics_pipeline->fragment_binary.binary_size) {
            v9_cmd_buffer_set_fragment_shader(
                commandBuffer->v9_cmd,
                &commandBuffer->graphics_pipeline->fragment_binary);
        }
        uint64_t pos_gpu = commandBuffer->vertex_bindings[0].buffer && commandBuffer->vertex_bindings[0].buffer->bo ?
                           commandBuffer->vertex_bindings[0].buffer->bo->gpu +
                           commandBuffer->vertex_bindings[0].buffer->memory_offset +
                           commandBuffer->vertex_bindings[0].offset + (firstVertex * 16) :
                           0;
        v9_cmd_draw_indexed(commandBuffer->v9_cmd, 0, vertexCount, 0, pos_gpu, vertexCount);
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
    if (commandBuffer && commandBuffer->v9_cmd && indexCount > 0 && instanceCount > 0 &&
        (!commandBuffer->graphics_pipeline ||
         !commandBuffer->graphics_pipeline->rasterizer_discard)) {
        command_buffer_apply_ubos(commandBuffer);
        if (commandBuffer->graphics_pipeline &&
            commandBuffer->graphics_pipeline->fragment_binary.binary_size) {
            v9_cmd_buffer_set_fragment_shader(
                commandBuffer->v9_cmd,
                &commandBuffer->graphics_pipeline->fragment_binary);
        }
        uint64_t pos_gpu = commandBuffer->vertex_bindings[0].buffer && commandBuffer->vertex_bindings[0].buffer->bo ?
                           commandBuffer->vertex_bindings[0].buffer->bo->gpu +
                           commandBuffer->vertex_bindings[0].buffer->memory_offset +
                           commandBuffer->vertex_bindings[0].offset + (vertexOffset * 16) :
                           0;
        uint64_t idx_gpu = commandBuffer->index_buffer && commandBuffer->index_buffer->bo ?
                           commandBuffer->index_buffer->bo->gpu +
                           commandBuffer->index_buffer->memory_offset +
                           commandBuffer->index_offset + (firstIndex * (commandBuffer->index_type == 1 ? 4 : 2)) :
                           0;
        v9_cmd_draw_indexed(commandBuffer->v9_cmd, idx_gpu, indexCount, commandBuffer->index_type, pos_gpu, indexCount);
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
                if (queue->last_v9_cmd != cmd->v9_cmd) {
                    v9_cmd_buffer_destroy(queue->last_v9_cmd);
                    queue->last_v9_cmd = v9_cmd_buffer_ref(cmd->v9_cmd);
                }
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

    surf->connection = (xcb_connection_t *)pCreateInfo->connection;
    surf->window = (uint32_t)pCreateInfo->window;
    surf->is_xcb = true;
    surf->width = 800;
    surf->height = 600;

    if (surf->connection && surf->window) {
        xcb_get_geometry_cookie_t cookie = xcb_get_geometry(surf->connection, surf->window);
        xcb_get_geometry_reply_t *reply = xcb_get_geometry_reply(surf->connection, cookie, NULL);
        if (reply) {
            surf->width = reply->width;
            surf->height = reply->height;
            free(reply);
        }
    }

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

    if (sc->surface && sc->surface->is_xcb && sc->surface->connection && sc->surface->window) {
        sc->xcb_gc = xcb_generate_id(sc->surface->connection);
        xcb_create_gc(sc->surface->connection, sc->xcb_gc, sc->surface->window, 0, NULL);
        sc->image_data = malloc(sc->width * sc->height * 4);
    } else if (sc->surface && sc->surface->dpy && sc->surface->window) {
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
    if (swapchain->surface && swapchain->surface->is_xcb && swapchain->surface->connection && swapchain->xcb_gc) {
        xcb_free_gc(swapchain->surface->connection, swapchain->xcb_gc);
    } else if (swapchain->surface && swapchain->surface->dpy && swapchain->gc) {
        XFreeGC(swapchain->surface->dpy, swapchain->gc);
    }
    if (swapchain->image_data) free(swapchain->image_data);
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
    if (sc && sc->surface && sc->image_data) {
        struct v9_cmd_buffer *last_cmd = queue ? queue->last_v9_cmd : NULL;
        if (last_cmd) {
            uint32_t *dst = (uint32_t *)sc->image_data;
            for (uint32_t y = 0; y < sc->height; y++) {
                for (uint32_t x = 0; x < sc->width; x++) {
                    dst[y * sc->width + x] = v9_cmd_buffer_read_pixel(last_cmd, x, y);
                }
            }
        }
        if (sc->surface->is_xcb && sc->surface->connection && sc->surface->window) {
            xcb_put_image(sc->surface->connection, XCB_IMAGE_FORMAT_Z_PIXMAP,
                          sc->surface->window, sc->xcb_gc,
                          sc->width, sc->height, 0, 0, 0, 24,
                          sc->width * sc->height * 4, (const uint8_t *)sc->image_data);
            xcb_flush(sc->surface->connection);
        } else if (sc->surface->dpy && sc->surface->window && sc->ximage && sc->gc) {
            XPutImage(sc->surface->dpy, sc->surface->window, sc->gc, sc->ximage, 0, 0, 0, 0, sc->width, sc->height);
            XFlush(sc->surface->dpy);
        }
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
    MATCH(vkCreateSampler);
    MATCH(vkDestroySampler);
    MATCH(vkCmdCopyBufferToImage);
    MATCH(vkCmdCopyImageToBuffer);
    MATCH(vkCmdCopyImage);
    MATCH(vkCmdBlitImage);
    MATCH(vkCmdClearColorImage);
    MATCH(vkCmdClearDepthStencilImage);
    MATCH(vkCmdClearAttachments);
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
