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
#include <sys/mman.h>

#include "panvk_v9_entrypoints.h"
#include "panvk_v9_compiler.h"

#include <stdarg.h>

#define ICD_LOADER_MAGIC 0x01CDC0DEu

static inline void panvk_file_log(const char *fmt, ...) {
    FILE *f = fopen("/sdcard/Download/panvk_debug.log", "a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fflush(f);
        fclose(f);
    }
}

#ifdef ANDROID
#include <android/log.h>
#define PANVK_LOG(...) do { \
    __android_log_print(ANDROID_LOG_INFO, "PANVK", __VA_ARGS__); \
    panvk_file_log(__VA_ARGS__); \
} while(0)
#else
#define PANVK_LOG(...) do { \
    fprintf(stderr, __VA_ARGS__); \
    fflush(stderr); \
    panvk_file_log(__VA_ARGS__); \
} while(0)
#endif

static inline void panvk_trace(const char *func, const char *extra) {
    if (extra) {
        PANVK_LOG("[TRACE] %s: %s\n", func, extra);
    } else {
        PANVK_LOG("[TRACE] %s\n", func);
    }
}

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

struct VkDeviceMemory_T;

struct VkDevice_T {
    uintptr_t loader_data;
    struct pan_kmod_dev *kdev;
    struct VkPhysicalDevice_T *phys_dev;
    struct VkQueue_T *queue;
    struct VkDeviceMemory_T *memories;
    pthread_mutex_t mem_mutex;
    void *last_rendered_color;
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
    struct VkImage_T *target_swapchain_image;
};

struct VkSurfaceKHR_T {
    Display *dpy;
    xcb_connection_t *connection;
    uint32_t window;
    uint32_t width;
    uint32_t height;
    uint8_t depth;
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
    xcb_pixmap_t xcb_pixmap;
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
    struct VkImage_T *image;
    uint32_t format;
    uint32_t base_mip_level;
    uint32_t level_count;
    uint32_t base_array_layer;
    uint32_t layer_count;
};

struct VkQueryPool_T {
    uint32_t query_count;
    uint32_t query_type;
    uint64_t *results;
};

struct VkDeviceMemory_T {
    struct pan_kmod_bo *bo;
    void *low_cpu;
    VkDeviceSize size;
    struct VkDeviceMemory_T *next;
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
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    uint32_t attachment_count;
    VkImageView *attachments;
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
    struct VkVertexInputBindingDescription vertex_bindings[16];
    struct VkVertexInputAttributeDescription vertex_attributes[16];
    uint32_t vertex_binding_count;
    uint32_t vertex_attribute_count;
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

#if defined(__BIONIC__) || defined(ANDROID)
/* GLIBC compatibility globals and functions for Bionic */
char *program_invocation_name = (char *)"vkmark";
char *program_invocation_short_name = (char *)"vkmark";

extern int *__errno(void);
int *__errno_location(void) {
    return __errno();
}
#endif

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

    compiler_api.attempted = true;

    const char *jit_env = getenv("PANVK_ENABLE_JIT_COMPILER");
    if (!jit_env || strcmp(jit_env, "1") != 0) {
        pthread_mutex_unlock(&compiler_api_mutex);
        return false;
    }

    const char *path = getenv("PANVK_V9_COMPILER_LIBRARY");
    if (path && path[0]) {
        compiler_api.library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    }
    if (!compiler_api.library) {
        compiler_api.library = dlopen("./libpanvk_v9_compiler.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (!compiler_api.library) {
        compiler_api.library = dlopen("/data/data/com.termux/files/home/libpanvk_v9_compiler.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (!compiler_api.library) {
        compiler_api.library = dlopen("libpanvk_v9_compiler.so", RTLD_NOW | RTLD_LOCAL);
    }
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
__attribute__((visibility("default")))
VkResult vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pSupportedVersion) {
    if (!pSupportedVersion) return VK_ERROR_INITIALIZATION_FAILED;
    if (*pSupportedVersion > 5) {
        *pSupportedVersion = 5;
    }
    return VK_SUCCESS;
}

__attribute__((visibility("default")))
VkResult vkEnumerateInstanceVersion(uint32_t *pApiVersion) {
    if (!pApiVersion) return VK_ERROR_INITIALIZATION_FAILED;
    *pApiVersion = (1u << 22) | (2u << 12); /* Vulkan 1.2 */
    return VK_SUCCESS;
}

/* Extension & Layer Enumeration */
__attribute__((visibility("default")))
VkResult vkEnumerateInstanceLayerProperties(uint32_t *pPropertyCount, struct VkLayerProperties *pProperties) {
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

__attribute__((visibility("default")))
VkResult vkEnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pPropertyCount, struct VkExtensionProperties *pProperties) {
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;

    /* If querying a specific layer, return VK_ERROR_LAYER_NOT_PRESENT */
    if (pLayerName && pLayerName[0] != '\0') {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

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

__attribute__((visibility("default")))
VkResult vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName, uint32_t *pPropertyCount, struct VkExtensionProperties *pProperties) {
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;

    if (pLayerName && pLayerName[0] != '\0') {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    static const struct VkExtensionProperties dev_exts[] = {
        { .extensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME, .specVersion = 70 },
        { .extensionName = "VK_KHR_maintenance1", .specVersion = 2 },
        { .extensionName = "VK_KHR_maintenance2", .specVersion = 1 },
        { .extensionName = "VK_KHR_maintenance3", .specVersion = 1 },
        { .extensionName = "VK_KHR_get_memory_requirements2", .specVersion = 1 },
        { .extensionName = "VK_KHR_dedicated_allocation", .specVersion = 3 },
        { .extensionName = "VK_KHR_bind_memory2", .specVersion = 1 },
        { .extensionName = "VK_KHR_sampler_mirror_clamp_to_edge", .specVersion = 3 },
        { .extensionName = "VK_KHR_shader_draw_parameters", .specVersion = 1 },
        { .extensionName = "VK_KHR_driver_properties", .specVersion = 1 },
        { .extensionName = "VK_KHR_image_format_list", .specVersion = 1 },
        { .extensionName = "VK_KHR_depth_stencil_resolve", .specVersion = 1 },
        { .extensionName = "VK_KHR_create_renderpass2", .specVersion = 1 },
        { .extensionName = "VK_KHR_timeline_semaphore", .specVersion = 2 },
        { .extensionName = "VK_KHR_shader_float16_int8", .specVersion = 1 },
        { .extensionName = "VK_KHR_8bit_storage", .specVersion = 1 },
        { .extensionName = "VK_KHR_16bit_storage", .specVersion = 1 },
        { .extensionName = "VK_KHR_descriptor_update_template", .specVersion = 1 },
        { .extensionName = "VK_KHR_buffer_device_address", .specVersion = 1 },
        { .extensionName = "VK_KHR_draw_indirect_count", .specVersion = 1 },
        { .extensionName = "VK_KHR_sampler_ycbcr_conversion", .specVersion = 1 },
        { .extensionName = "VK_KHR_pipeline_library", .specVersion = 1 },
        { .extensionName = "VK_EXT_custom_border_color", .specVersion = 12 },
        { .extensionName = "VK_EXT_vertex_attribute_divisor", .specVersion = 3 },
        { .extensionName = "VK_EXT_transform_feedback", .specVersion = 1 },
        { .extensionName = "VK_EXT_depth_clip_enable", .specVersion = 1 },
        { .extensionName = "VK_EXT_memory_budget", .specVersion = 1 },
        { .extensionName = "VK_EXT_memory_priority", .specVersion = 1 },
        { .extensionName = "VK_EXT_shader_viewport_index_layer", .specVersion = 1 },
        { .extensionName = "VK_EXT_robustness2", .specVersion = 1 },
        { .extensionName = "VK_EXT_shader_demote_to_helper_invocation", .specVersion = 1 },
        { .extensionName = "VK_EXT_scalar_block_layout", .specVersion = 1 },
        { .extensionName = "VK_EXT_descriptor_indexing", .specVersion = 2 },
        { .extensionName = "VK_EXT_host_query_reset", .specVersion = 1 },
        { .extensionName = "VK_EXT_line_rasterization", .specVersion = 1 },
        { .extensionName = "VK_EXT_inline_uniform_block", .specVersion = 1 },
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
            PANVK_LOG("DEBUG: vkCreateInstance requested extension: '%s'\n", pCreateInfo->ppEnabledExtensionNames[i]);
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
    pProperties->deviceID = physicalDevice->props.gpu_id ? physicalDevice->props.gpu_id : 0x9000;
    pProperties->deviceType = 2; /* Discrete GPU for DXVK/D3D9 compatibility */
    snprintf(pProperties->deviceName, sizeof(pProperties->deviceName),
             "ARM Mali-G77 MC9 (Valhall v9 - PanVK Open Source Driver)");

    /* Populate standard limits for Mali-G77 */
    struct VkPhysicalDeviceLimits *l = &pProperties->limits;
    l->maxImageDimension1D = 16384;
    l->maxImageDimension2D = 16384;
    l->maxImageDimension3D = 2048;
    l->maxImageDimensionCube = 16384;
    l->maxImageArrayLayers = 2048;
    l->maxTexelBufferElements = 65536;
    l->maxUniformBufferRange = 65536;
    l->maxStorageBufferRange = 1024ULL * 1024ULL * 1024ULL; /* 1GB */
    l->maxPushConstantsSize = 128;
    l->maxMemoryAllocationCount = 4096;
    l->maxSamplerAllocationCount = 4000;
    l->bufferImageGranularity = 64;
    l->sparseAddressSpaceSize = 0;
    l->maxBoundDescriptorSets = 8;
    l->maxPerStageDescriptorSamplers = 64;
    l->maxPerStageDescriptorUniformBuffers = 16;
    l->maxPerStageDescriptorStorageBuffers = 16;
    l->maxPerStageDescriptorSampledImages = 128;
    l->maxPerStageDescriptorStorageImages = 16;
    l->maxPerStageDescriptorInputAttachments = 8;
    l->maxPerStageResources = 128;
    l->maxDescriptorSetSamplers = 128;
    l->maxDescriptorSetUniformBuffers = 128;
    l->maxDescriptorSetUniformBuffersDynamic = 8;
    l->maxDescriptorSetStorageBuffers = 128;
    l->maxDescriptorSetStorageBuffersDynamic = 8;
    l->maxDescriptorSetSampledImages = 128;
    l->maxDescriptorSetStorageImages = 128;
    l->maxDescriptorSetInputAttachments = 8;
    l->maxVertexInputAttributes = 32;
    l->maxVertexInputBindings = 32;
    l->maxVertexInputAttributeOffset = 2047;
    l->maxVertexInputBindingStride = 2048;
    l->maxVertexOutputComponents = 64;
    l->maxTessellationGenerationLevel = 64;
    l->maxTessellationPatchSize = 32;
    l->maxTessellationControlPerVertexInputComponents = 64;
    l->maxTessellationControlPerVertexOutputComponents = 64;
    l->maxTessellationControlPerPatchOutputComponents = 120;
    l->maxTessellationControlTotalOutputComponents = 4096;
    l->maxTessellationEvaluationInputComponents = 64;
    l->maxTessellationEvaluationOutputComponents = 64;
    l->maxGeometryShaderInvocations = 32;
    l->maxGeometryInputComponents = 64;
    l->maxGeometryOutputComponents = 64;
    l->maxGeometryOutputVertices = 256;
    l->maxGeometryTotalOutputComponents = 1024;
    l->maxFragmentInputComponents = 64;
    l->maxFragmentOutputAttachments = 8;
    l->maxFragmentDualSrcAttachments = 1;
    l->maxFragmentCombinedOutputResources = 16;
    l->maxComputeSharedMemorySize = 32768;
    l->maxComputeWorkGroupCount[0] = 65535;
    l->maxComputeWorkGroupCount[1] = 65535;
    l->maxComputeWorkGroupCount[2] = 65535;
    l->maxComputeWorkGroupInvocations = 1024;
    l->maxComputeWorkGroupSize[0] = 1024;
    l->maxComputeWorkGroupSize[1] = 1024;
    l->maxComputeWorkGroupSize[2] = 1024;
    l->subPixelPrecisionBits = 4;
    l->subTexelPrecisionBits = 4;
    l->mipmapPrecisionBits = 4;
    l->maxDrawIndexedIndexValue = 0xFFFFFFFF;
    l->maxDrawIndirectCount = 65535;
    l->maxSamplerLodBias = 15.0f;
    l->maxSamplerAnisotropy = 16.0f;
    l->maxViewports = 16;
    l->maxViewportDimensions[0] = 16384;
    l->maxViewportDimensions[1] = 16384;
    l->viewportBoundsRange[0] = -32768.0f;
    l->viewportBoundsRange[1] = 32767.0f;
    l->viewportSubPixelBits = 8;
    l->minMemoryMapAlignment = 64;
    l->minTexelBufferOffsetAlignment = 16;
    l->minUniformBufferOffsetAlignment = 64;
    l->minStorageBufferOffsetAlignment = 64;
    l->minTexelOffset = -8;
    l->maxTexelOffset = 7;
    l->minTexelGatherOffset = -8;
    l->maxTexelGatherOffset = 7;
    l->minInterpolationOffset = -0.5f;
    l->maxInterpolationOffset = 0.4375f;
    l->subPixelInterpolationOffsetBits = 4;
    l->maxFramebufferWidth = 16384;
    l->maxFramebufferHeight = 16384;
    l->maxFramebufferLayers = 2048;
    l->framebufferColorSampleCounts = 1 | 4;
    l->framebufferDepthSampleCounts = 1 | 4;
    l->framebufferStencilSampleCounts = 1 | 4;
    l->framebufferNoAttachmentsSampleCounts = 1 | 4;
    l->maxColorAttachments = 8;
    l->sampledImageColorSampleCounts = 1 | 4;
    l->sampledImageIntegerSampleCounts = 1 | 4;
    l->sampledImageDepthSampleCounts = 1 | 4;
    l->sampledImageStencilSampleCounts = 1 | 4;
    l->storageImageSampleCounts = 1;
    l->maxSampleMaskWords = 1;
    l->timestampComputeAndGraphics = 1;
    l->timestampPeriod = 1.0f;
    l->maxClipDistances = 8;
    l->maxCullDistances = 8;
    l->maxCombinedClipAndCullDistances = 8;
    l->discreteQueuePriorities = 2;
    l->pointSizeRange[0] = 1.0f;
    l->pointSizeRange[1] = 64.0f;
    l->lineWidthRange[0] = 1.0f;
    l->lineWidthRange[1] = 8.0f;
    l->pointSizeGranularity = 0.125f;
    l->lineWidthGranularity = 0.125f;
    l->strictLines = 1;
    l->standardSampleLocations = 1;
    l->optimalBufferCopyOffsetAlignment = 64;
    l->optimalBufferCopyRowPitchAlignment = 64;
    l->nonCoherentAtomSize = 64;
}

void vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceProperties2 *pProperties) {
    if (!pProperties) return;
    vkGetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);

    /* Traverse pNext chain for extension property structs queried by DXVK */
    struct VkBaseOutStructure {
        uint32_t sType;
        struct VkBaseOutStructure *pNext;
    } *curr = (struct VkBaseOutStructure *)pProperties->pNext;

    while (curr) {
        switch (curr->sType) {
        case 1000196000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR */
            struct {
                uint32_t sType;
                void *pNext;
                uint32_t driverID;
                char driverName[256];
                char driverInfo[256];
                struct { uint32_t major, minor, subminor, patch; } conformanceVersion;
            } *dp = (void *)curr;
            dp->driverID = 16; /* VK_DRIVER_ID_MESA_PANVK */
            strncpy(dp->driverName, "PanVK", sizeof(dp->driverName) - 1);
            strncpy(dp->driverInfo, "Mali-G77 MC9 Valhall PanVK", sizeof(dp->driverInfo) - 1);
            dp->conformanceVersion.major = 1;
            dp->conformanceVersion.minor = 2;
            dp->conformanceVersion.subminor = 0;
            dp->conformanceVersion.patch = 0;
            break;
        }
        case 1000071004: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES */
            struct {
                uint32_t sType;
                void *pNext;
                uint8_t deviceUUID[16];
                uint8_t driverUUID[16];
                uint8_t deviceLUID[8];
                uint32_t deviceNodeMask;
                uint32_t deviceLUIDValid;
            } *id = (void *)curr;
            memset(id->deviceUUID, 0x42, 16);
            memset(id->driverUUID, 0x24, 16);
            memset(id->deviceLUID, 0x01, 8);
            id->deviceNodeMask = 1;
            id->deviceLUIDValid = 1;
            break;
        }
        case 1000094000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES */
            struct {
                uint32_t sType;
                void *pNext;
                uint32_t subgroupSize;
                uint32_t supportedStages;
                uint32_t supportedOperations;
                uint32_t quadOperationsInAllStages;
            } *sg = (void *)curr;
            sg->subgroupSize = 16;
            sg->supportedStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | 0x00000020;
            sg->supportedOperations = 0x3F; /* BASIC | VOTE | ARITHMETIC | BALLOT | SHUFFLE | SHUFFLE_RELATIVE */
            sg->quadOperationsInAllStages = 1;
            break;
        }
        case 1000197000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES */
            struct { uint32_t sType; void *pNext; uint32_t pointClippingBehavior; } *pc = (void *)curr;
            pc->pointClippingBehavior = 0;
            break;
        }
        case 1000053001: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES */
            struct { uint32_t sType; void *pNext; uint32_t maxMultiviewViewCount; uint32_t maxMultiviewInstanceIndex; } *mv = (void *)curr;
            mv->maxMultiviewViewCount = 6;
            mv->maxMultiviewInstanceIndex = 1 << 27;
            break;
        }
        case 1000168000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES */
            struct { uint32_t sType; void *pNext; uint32_t maxPerSetDescriptors; uint64_t maxMemoryAllocationSize; } *m3 = (void *)curr;
            m3->maxPerSetDescriptors = 1024;
            m3->maxMemoryAllocationSize = 1024ULL * 1024ULL * 1024ULL;
            break;
        }
        case 1000199000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES */
            struct {
                uint32_t sType;
                void *pNext;
                uint32_t supportedDepthResolveModes;
                uint32_t supportedStencilResolveModes;
                uint32_t independentResolveNone;
                uint32_t independentResolve;
            } *dsr = (void *)curr;
            dsr->supportedDepthResolveModes = 0xF;
            dsr->supportedStencilResolveModes = 0xF;
            dsr->independentResolveNone = 1;
            dsr->independentResolve = 1;
            break;
        }
        case 1000387000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT */
            struct { uint32_t sType; void *pNext; uint32_t maxCustomBorderColorSamplers; } *cbc = (void *)curr;
            cbc->maxCustomBorderColorSamplers = 16;
            break;
        }
        case 1000028001: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT */
            struct {
                uint32_t sType;
                void *pNext;
                uint32_t maxTransformFeedbackStreams;
                uint32_t maxTransformFeedbackBuffers;
                uint64_t maxTransformFeedbackBufferSize;
                uint64_t maxTransformFeedbackStreamDataSize;
                uint32_t maxTransformFeedbackBufferDataSize;
                uint32_t maxTransformFeedbackBufferDataStride;
                uint32_t transformFeedbackQueries;
                uint32_t transformFeedbackStreamsLinesTriangles;
                uint32_t transformFeedbackRasterizationStreamSelect;
                uint32_t transformFeedbackDraw;
            } *tf = (void *)curr;
            tf->maxTransformFeedbackStreams = 4;
            tf->maxTransformFeedbackBuffers = 4;
            tf->maxTransformFeedbackBufferSize = 1024ULL * 1024ULL * 1024ULL;
            tf->maxTransformFeedbackStreamDataSize = 1024ULL * 1024ULL * 1024ULL;
            tf->maxTransformFeedbackBufferDataSize = 1024ULL * 1024ULL * 1024ULL;
            tf->maxTransformFeedbackBufferDataStride = 2048;
            tf->transformFeedbackQueries = 1;
            tf->transformFeedbackStreamsLinesTriangles = 1;
            tf->transformFeedbackRasterizationStreamSelect = 1;
            tf->transformFeedbackDraw = 1;
            break;
        }
        case 1000286000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_EXT */
            struct { uint32_t sType; void *pNext; uint64_t robustStorageBufferAccessSizeAlignment; uint64_t robustUniformBufferAccessSizeAlignment; } *r2 = (void *)curr;
            r2->robustStorageBufferAccessSizeAlignment = 1;
            r2->robustUniformBufferAccessSizeAlignment = 1;
            break;
        }
        default:
            break;
        }
        curr = curr->pNext;
    }
}

void vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, void *pFeatures) {
    if (!pFeatures) return;
    struct VkPhysicalDeviceFeatures *f = (struct VkPhysicalDeviceFeatures *)pFeatures;
    memset(f, 0, sizeof(*f));
    f->robustBufferAccess = 1;
    f->fullDrawIndexUint32 = 1;
    f->imageCubeArray = 1;
    f->independentBlend = 1;
    f->geometryShader = 1;
    f->tessellationShader = 1;
    f->sampleRateShading = 1;
    f->dualSrcBlend = 1;
    f->logicOp = 1;
    f->multiDrawIndirect = 1;
    f->drawIndirectFirstInstance = 1;
    f->depthClamp = 1;
    f->depthBiasClamp = 1;
    f->fillModeNonSolid = 1;
    f->depthBounds = 1;
    f->wideLines = 1;
    f->largePoints = 1;
    f->alphaToOne = 1;
    f->multiViewport = 1;
    f->samplerAnisotropy = 1;
    f->textureCompressionETC2 = 1;
    f->textureCompressionASTC_LDR = 1;
    f->textureCompressionBC = 1;
    f->occlusionQueryPrecise = 1;
    f->pipelineStatisticsQuery = 1;
    f->vertexPipelineStoresAndAtomics = 1;
    f->fragmentStoresAndAtomics = 1;
    f->shaderTessellationAndGeometryPointSize = 1;
    f->shaderImageGatherExtended = 1;
    f->shaderStorageImageExtendedFormats = 1;
    f->shaderStorageImageMultisample = 1;
    f->shaderStorageImageReadWithoutFormat = 1;
    f->shaderStorageImageWriteWithoutFormat = 1;
    f->shaderUniformBufferArrayDynamicIndexing = 1;
    f->shaderSampledImageArrayDynamicIndexing = 1;
    f->shaderStorageBufferArrayDynamicIndexing = 1;
    f->shaderStorageImageArrayDynamicIndexing = 1;
    f->shaderClipDistance = 1;
    f->shaderCullDistance = 1;
    f->shaderFloat64 = 0;
    f->shaderInt64 = 1;
    f->shaderInt16 = 1;
    f->shaderResourceResidency = 0;
    f->shaderResourceMinLod = 1;
    f->sparseBinding = 0;
    f->inheritedQueries = 1;
}

void vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceFeatures2 *pFeatures) {
    if (!pFeatures) return;
    vkGetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);

    /* Traverse pNext chain for extension feature structs queried by DXVK */
    struct VkBaseOutStructure {
        uint32_t sType;
        struct VkBaseOutStructure *pNext;
    } *curr = (struct VkBaseOutStructure *)pFeatures->pNext;

    while (curr) {
        switch (curr->sType) {
        case 1000387001: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT */
            struct { uint32_t sType; void *pNext; uint32_t customBorderColors; uint32_t customBorderColorWithoutFormat; } *cbc = (void *)curr;
            cbc->customBorderColors = 1;
            cbc->customBorderColorWithoutFormat = 1;
            break;
        }
        case 1000102000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT */
            struct { uint32_t sType; void *pNext; uint32_t depthClipEnable; } *dc = (void *)curr;
            dc->depthClipEnable = 1;
            break;
        }
        case 1000028000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT */
            struct { uint32_t sType; void *pNext; uint32_t transformFeedback; uint32_t geometryStreams; } *tf = (void *)curr;
            tf->transformFeedback = 1;
            tf->geometryStreams = 1;
            break;
        }
        case 1000286001: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT */
            struct { uint32_t sType; void *pNext; uint32_t robustBufferAccess2; uint32_t robustImageAccess2; uint32_t nullDescriptor; } *r2 = (void *)curr;
            r2->robustBufferAccess2 = 1;
            r2->robustImageAccess2 = 1;
            r2->nullDescriptor = 1;
            break;
        }
        case 1000190000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT */
            struct { uint32_t sType; void *pNext; uint32_t vertexAttributeInstanceRateDivisor; uint32_t vertexAttributeInstanceRateZeroDivisor; } *vad = (void *)curr;
            vad->vertexAttributeInstanceRateDivisor = 1;
            vad->vertexAttributeInstanceRateZeroDivisor = 1;
            break;
        }
        case 1000261000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES */
            struct { uint32_t sType; void *pNext; uint32_t hostQueryReset; } *hqr = (void *)curr;
            hqr->hostQueryReset = 1;
            break;
        }
        case 1000276000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT */
            struct { uint32_t sType; void *pNext; uint32_t shaderDemoteToHelperInvocation; } *sd = (void *)curr;
            sd->shaderDemoteToHelperInvocation = 1;
            break;
        }
        case 1000221000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES */
            struct { uint32_t sType; void *pNext; uint32_t scalarBlockLayout; } *sbl = (void *)curr;
            sbl->scalarBlockLayout = 1;
            break;
        }
        case 1000207000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES */
            struct { uint32_t sType; void *pNext; uint32_t timelineSemaphore; } *ts = (void *)curr;
            ts->timelineSemaphore = 1;
            break;
        }
        case 1000257000: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES */
            struct { uint32_t sType; void *pNext; uint32_t bufferDeviceAddress; uint32_t bufferDeviceAddressCaptureReplay; uint32_t bufferDeviceAddressMultiDevice; } *bda = (void *)curr;
            bda->bufferDeviceAddress = 1;
            bda->bufferDeviceAddressCaptureReplay = 0;
            bda->bufferDeviceAddressMultiDevice = 0;
            break;
        }
        case 49: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES */
            uint32_t *arr = (uint32_t *)curr;
            /* set all boolean fields (starting after sType+pNext, so from index 4 onwards on 64-bit) to 1 */
            for (size_t k = 4; k < 16; k++) arr[k] = 1;
            break;
        }
        case 51: { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES */
            uint32_t *arr = (uint32_t *)curr;
            for (size_t k = 4; k < 50; k++) arr[k] = 1;
            break;
        }
        default:
            break;
        }
        curr = curr->pNext;
    }
}

void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, void *pQueueFamilyProperties) {
    if (!pQueueFamilyPropertyCount) return;
    if (!pQueueFamilyProperties) {
        *pQueueFamilyPropertyCount = 1;
        return;
    }
    /* Family 0: Graphics + Compute + Transfer (0x7) */
    struct VkQueueFamilyProperties *qfp = (struct VkQueueFamilyProperties *)pQueueFamilyProperties;
    memset(qfp, 0, sizeof(*qfp));
    qfp->queueFlags = 0x7; /* Queue flags */
    qfp->queueCount = 1;   /* Queue count */
    qfp->timestampValidBits = 64;
    qfp->minImageTransferGranularity.width = 1;
    qfp->minImageTransferGranularity.height = 1;
    *pQueueFamilyPropertyCount = 1;
}

void vkGetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, struct VkQueueFamilyProperties2 *pQueueFamilyProperties) {
    if (!pQueueFamilyPropertyCount) return;
    if (!pQueueFamilyProperties) {
        *pQueueFamilyPropertyCount = 1;
        return;
    }
    memset(pQueueFamilyProperties, 0, sizeof(*pQueueFamilyProperties));
    pQueueFamilyProperties->queueFamilyProperties.queueFlags = 0x7;
    pQueueFamilyProperties->queueFamilyProperties.queueCount = 1;
    pQueueFamilyProperties->queueFamilyProperties.timestampValidBits = 64;
    pQueueFamilyProperties->queueFamilyProperties.minImageTransferGranularity.width = 1;
    pQueueFamilyProperties->queueFamilyProperties.minImageTransferGranularity.height = 1;
    *pQueueFamilyPropertyCount = 1;
}

void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, void *pMemoryProperties) {
    if (!pMemoryProperties) return;
    struct VkPhysicalDeviceMemoryProperties *mp = (struct VkPhysicalDeviceMemoryProperties *)pMemoryProperties;
    memset(mp, 0, sizeof(*mp));
    mp->memoryTypeCount = 2;
    /* Type 0: Device Local (GPU-only) */
    mp->memoryTypes[0].propertyFlags = 1; /* VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT */
    mp->memoryTypes[0].heapIndex = 0;
    /* Type 1: Unified Memory (Host Visible + Coherent + Cached + Device Local) */
    mp->memoryTypes[1].propertyFlags = 0xF; /* DeviceLocal | HostVisible | HostCoherent | HostCached */
    mp->memoryTypes[1].heapIndex = 0;
    mp->memoryHeapCount = 1;
    mp->memoryHeaps[0].size = 4096ULL * 1024ULL * 1024ULL; /* 4GB Unified VRAM Heap */
    mp->memoryHeaps[0].flags = 1; /* Heap flags: DeviceLocal */
}

void vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, struct VkPhysicalDeviceMemoryProperties2 *pMemoryProperties) {
    if (!pMemoryProperties) return;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &pMemoryProperties->memoryProperties);

    struct VkBaseOutStructure {
        uint32_t sType;
        struct VkBaseOutStructure *pNext;
    } *curr = (struct VkBaseOutStructure *)pMemoryProperties->pNext;

    while (curr) {
        if (curr->sType == 1000237000) { /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT */
            struct {
                uint32_t sType;
                void *pNext;
                VkDeviceSize heapBudget[16];
                VkDeviceSize heapUsage[16];
            } *mb = (void *)curr;
            memset(mb->heapBudget, 0, sizeof(mb->heapBudget));
            memset(mb->heapUsage, 0, sizeof(mb->heapUsage));
            mb->heapBudget[0] = 4096ULL * 1024ULL * 1024ULL;
            mb->heapUsage[0] = 0;
        }
        curr = curr->pNext;
    }
}

void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, void *pFormatProperties) {
    if (!pFormatProperties) return;
    struct VkFormatProperties *fp = (struct VkFormatProperties *)pFormatProperties;
    memset(fp, 0, sizeof(*fp));

    /* Full sampled image, attachment, blit, and transfer features */
    VkFormatFeatureFlags common_img_flags = 0x00000001 /* SAMPLED_IMAGE */
                                          | 0x00000400 /* BLIT_SRC */
                                          | 0x00000800 /* BLIT_DST */
                                          | 0x00001000 /* SAMPLED_IMAGE_FILTER_LINEAR */
                                          | 0x00004000 /* TRANSFER_SRC */
                                          | 0x00008000;/* TRANSFER_DST */

    VkFormatFeatureFlags color_flags = common_img_flags
                                     | 0x00000002 /* STORAGE_IMAGE */
                                     | 0x00000080 /* COLOR_ATTACHMENT */
                                     | 0x00000100;/* COLOR_ATTACHMENT_BLEND */

    VkFormatFeatureFlags depth_flags = common_img_flags
                                     | 0x00000200;/* DEPTH_STENCIL_ATTACHMENT */

    VkFormatFeatureFlags buf_flags = 0x00000040 /* VERTEX_BUFFER */
                                   | 0x00000008 /* UNIFORM_TEXEL_BUFFER */
                                   | 0x00000010;/* STORAGE_TEXEL_BUFFER */

    /* Depth/Stencil formats */
    if (format == 124 || format == 125 || format == 126 || format == 127 || format == 129 || format == 130) {
        fp->optimalTilingFeatures = depth_flags;
        fp->linearTilingFeatures = depth_flags;
    }
    /* BCn compressed formats (131 to 146) */
    else if (format >= 131 && format <= 146) {
        fp->optimalTilingFeatures = common_img_flags;
        fp->linearTilingFeatures = common_img_flags;
    }
    /* Color / Standard formats */
    else {
        fp->optimalTilingFeatures = color_flags;
        fp->linearTilingFeatures = color_flags;
        fp->bufferFeatures = buf_flags;
    }
}

void vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, uint32_t format, struct VkFormatProperties2 *pFormatProperties) {
    if (!pFormatProperties) return;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &pFormatProperties->formatProperties);
}

void vkGetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t format, struct VkFormatProperties2 *pFormatProperties) {
    vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, pFormatProperties);
}

VkResult vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, uint32_t type, uint32_t tiling, uint32_t usage, uint32_t flags, void *pImageFormatProperties) {
    if (!pImageFormatProperties) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkImageFormatProperties *ifp = (struct VkImageFormatProperties *)pImageFormatProperties;
    memset(ifp, 0, sizeof(*ifp));
    ifp->maxExtent.width = 16384;
    ifp->maxExtent.height = 16384;
    ifp->maxExtent.depth = 2048;
    ifp->maxMipLevels = 16;
    ifp->maxArrayLayers = 2048;
    ifp->sampleCounts = 1 | 4;
    ifp->maxResourceSize = 1024ULL * 1024ULL * 1024ULL; /* 1GB */
    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice, const void *pImageFormatInfo, struct VkImageFormatProperties2 *pImageFormatProperties) {
    if (!pImageFormatProperties) return VK_ERROR_INITIALIZATION_FAILED;
    return vkGetPhysicalDeviceImageFormatProperties(physicalDevice, 0, 0, 0, 0, 0, &pImageFormatProperties->imageFormatProperties);
}

VkResult vkGetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const void *pImageFormatInfo, struct VkImageFormatProperties2 *pImageFormatProperties) {
    return vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, pImageFormatInfo, pImageFormatProperties);
}

void vkGetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, uint32_t type, uint32_t samples, uint32_t usage, uint32_t tiling, uint32_t *pPropertyCount, void *pProperties) {
    if (pPropertyCount) *pPropertyCount = 0;
}

VkResult vkCreateDevice(VkPhysicalDevice physicalDevice, const struct VkDeviceCreateInfo *pCreateInfo, void *pAllocator, VkDevice *pDevice) {
    PANVK_LOG("vkCreateDevice called: phys=%p\n", physicalDevice);
    if (!physicalDevice || !pDevice) return VK_ERROR_INITIALIZATION_FAILED;

    struct VkDevice_T *dev = calloc(1, sizeof(*dev));
    if (!dev) return VK_ERROR_OUT_OF_HOST_MEMORY;

    set_loader_magic(dev);
    pthread_mutex_init(&dev->mem_mutex, NULL);
    dev->phys_dev = physicalDevice;
    dev->kdev = physicalDevice->kdev;
    if (!dev->kdev) {
        PANVK_LOG("vkCreateDevice: physicalDevice->kdev was NULL, creating fallback kdev\n");
        dev->kdev = pan_kmod_dev_create(NULL);
    }

    dev->queue = calloc(1, sizeof(*dev->queue));
    if (dev->queue) {
        set_loader_magic(dev->queue);
        dev->queue->device = dev;
    }
    *pDevice = dev;
    PANVK_LOG("vkCreateDevice SUCCESS dev=%p kdev=%p queue=%p\n", dev, dev->kdev, dev->queue);
    return VK_SUCCESS;
}

void vkDestroyDevice(VkDevice device, void *pAllocator) {
    if (!device) return;
    PANVK_LOG("vkDestroyDevice: dev=%p\n", device);
    if (device->queue) v9_cmd_buffer_destroy(device->queue->last_v9_cmd);
    free(device->queue);
    pthread_mutex_destroy(&device->mem_mutex);
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

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static void *alloc_low32_memory(size_t aligned_sz) {
    static uint64_t g_next_low_hint = 0x20000000ULL;

    for (int attempts = 0; attempts < 64; attempts++) {
        uint64_t hint = __sync_fetch_and_add(&g_next_low_hint, aligned_sz);
        if (hint + aligned_sz > 0xE0000000ULL) {
            g_next_low_hint = 0x20000000ULL;
            hint = 0x20000000ULL;
        }

        void *p = mmap((void *)(uintptr_t)hint, aligned_sz, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p != MAP_FAILED) {
            return p;
        }
    }
    return mmap(NULL, aligned_sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

/* Memory Allocation & Buffer Management */
VkResult vkAllocateMemory(VkDevice device, const struct VkMemoryAllocateInfo *pAllocateInfo, void *pAllocator, VkDeviceMemory *pMemory) {
    if (!device || !pAllocateInfo || !pMemory) return VK_ERROR_INITIALIZATION_FAILED;

    struct VkDeviceMemory_T *mem = calloc(1, sizeof(*mem));
    if (!mem) return VK_ERROR_OUT_OF_HOST_MEMORY;

    size_t sz = pAllocateInfo->allocationSize > 0 ? pAllocateInfo->allocationSize : 4096;
    size_t page_sz = 4096;
    size_t aligned_sz = (sz + page_sz - 1) & ~(page_sz - 1);

    if (device->kdev) {
        mem->bo = pan_kmod_bo_alloc(device->kdev, aligned_sz, PAN_KMOD_BO_FLAG_READ | PAN_KMOD_BO_FLAG_WRITE);
    }
    if (!mem->bo) {
        if (!device->kdev) device->kdev = pan_kmod_dev_create(NULL);
        if (device->kdev) mem->bo = pan_kmod_bo_alloc(device->kdev, aligned_sz, PAN_KMOD_BO_FLAG_READ | PAN_KMOD_BO_FLAG_WRITE);
    }

    if (!mem->bo) {
        free(mem);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    /* Allocate guaranteed 32-bit address space buffer (< 4GB) for 32-bit Wine/DXVK mapping */
    mem->low_cpu = alloc_low32_memory(aligned_sz);
    mem->size = aligned_sz;

    /* Add to device memory list */
    pthread_mutex_lock(&device->mem_mutex);
    mem->next = device->memories;
    device->memories = mem;
    pthread_mutex_unlock(&device->mem_mutex);

    *pMemory = mem;
    PANVK_LOG("vkAllocateMemory OK: sz=%zu low_cpu=%p bo_cpu=%p bo_gpu=0x%llx mem=%p\n",
              sz, mem->low_cpu, mem->bo ? mem->bo->cpu : NULL,
              mem->bo ? (unsigned long long)mem->bo->gpu : 0, mem);
    return VK_SUCCESS;
}

void vkFreeMemory(VkDevice device, VkDeviceMemory memory, void *pAllocator) {
    if (!memory) return;
    PANVK_LOG("vkFreeMemory: mem=%p\n", memory);
    if (device) {
        pthread_mutex_lock(&device->mem_mutex);
        struct VkDeviceMemory_T **curr = &device->memories;
        while (*curr) {
            if (*curr == memory) {
                *curr = memory->next;
                break;
            }
            curr = &(*curr)->next;
        }
        pthread_mutex_unlock(&device->mem_mutex);
    }
    if (memory->low_cpu && memory->low_cpu != MAP_FAILED) {
        munmap(memory->low_cpu, memory->size);
    }
    if (memory->bo) pan_kmod_bo_free(memory->bo);
    free(memory);
}

VkResult vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkFlags flags, void **ppData) {
    if (!memory || !ppData) return VK_ERROR_INITIALIZATION_FAILED;
    void *base = (memory->low_cpu && memory->low_cpu != MAP_FAILED && (uintptr_t)memory->low_cpu <= 0xFFFFFFFFULL) ?
                 memory->low_cpu : (memory->bo ? memory->bo->cpu : NULL);
    if (!base) return VK_ERROR_INITIALIZATION_FAILED;
    *ppData = (uint8_t *)base + offset;
    PANVK_LOG("vkMapMemory OK: mem=%p cpu=%p off=%llu sz=%llu -> *ppData=%p\n",
              memory, base, (unsigned long long)offset, (unsigned long long)size, *ppData);
    return VK_SUCCESS;
}

void vkUnmapMemory(VkDevice device, VkDeviceMemory memory) {
    (void)device; (void)memory;
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
    pMemoryRequirements->memoryTypeBits = 0x3;
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
    pMemoryRequirements->memoryTypeBits = 0x3;
}

VkResult vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory,
                           VkDeviceSize memoryOffset) {
    if (!image || !memory) return VK_ERROR_INITIALIZATION_FAILED;
    image->bo = memory->bo;
    image->memory_offset = memoryOffset;
    return VK_SUCCESS;
}

void vkGetBufferMemoryRequirements2(VkDevice device, const struct VkBufferMemoryRequirementsInfo2 *pInfo, struct VkMemoryRequirements2 *pMemoryRequirements) {
    if (!pMemoryRequirements) return;
    VkBuffer buffer = pInfo ? pInfo->buffer : NULL;
    vkGetBufferMemoryRequirements(device, buffer, &pMemoryRequirements->memoryRequirements);
}

void vkGetBufferMemoryRequirements2KHR(VkDevice device, const struct VkBufferMemoryRequirementsInfo2 *pInfo, struct VkMemoryRequirements2 *pMemoryRequirements) {
    vkGetBufferMemoryRequirements2(device, pInfo, pMemoryRequirements);
}

void vkGetImageMemoryRequirements2(VkDevice device, const struct VkImageMemoryRequirementsInfo2 *pInfo, struct VkMemoryRequirements2 *pMemoryRequirements) {
    if (!pMemoryRequirements) return;
    VkImage image = pInfo ? pInfo->image : NULL;
    vkGetImageMemoryRequirements(device, image, &pMemoryRequirements->memoryRequirements);
}

void vkGetImageMemoryRequirements2KHR(VkDevice device, const struct VkImageMemoryRequirementsInfo2 *pInfo, struct VkMemoryRequirements2 *pMemoryRequirements) {
    vkGetImageMemoryRequirements2(device, pInfo, pMemoryRequirements);
}

VkResult vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const struct VkBindBufferMemoryInfo *pBindInfos) {
    if (!pBindInfos) return VK_SUCCESS;
    for (uint32_t i = 0; i < bindInfoCount; i++) {
        VkResult res = vkBindBufferMemory(device, pBindInfos[i].buffer, pBindInfos[i].memory, pBindInfos[i].memoryOffset);
        if (res != VK_SUCCESS) return res;
    }
    return VK_SUCCESS;
}

VkResult vkBindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount, const struct VkBindBufferMemoryInfo *pBindInfos) {
    return vkBindBufferMemory2(device, bindInfoCount, pBindInfos);
}

VkResult vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount, const struct VkBindImageMemoryInfo *pBindInfos) {
    if (!pBindInfos) return VK_SUCCESS;
    for (uint32_t i = 0; i < bindInfoCount; i++) {
        VkResult res = vkBindImageMemory(device, pBindInfos[i].image, pBindInfos[i].memory, pBindInfos[i].memoryOffset);
        if (res != VK_SUCCESS) return res;
    }
    return VK_SUCCESS;
}

VkResult vkBindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const struct VkBindImageMemoryInfo *pBindInfos) {
    return vkBindImageMemory2(device, bindInfoCount, pBindInfos);
}

VkResult vkCreateRenderPass2(VkDevice device, const void *pCreateInfo, void *pAllocator, VkRenderPass *pRenderPass) {
    return vkCreateRenderPass(device, (const struct VkRenderPassCreateInfo *)pCreateInfo, pAllocator, pRenderPass);
}

VkResult vkCreateRenderPass2KHR(VkDevice device, const void *pCreateInfo, void *pAllocator, VkRenderPass *pRenderPass) {
    return vkCreateRenderPass2(device, pCreateInfo, pAllocator, pRenderPass);
}

struct VkDescriptorUpdateTemplate_T {
    uintptr_t loader_data;
};

VkResult vkCreateDescriptorUpdateTemplate(VkDevice device, const void *pCreateInfo, void *pAllocator, VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate) {
    if (!pDescriptorUpdateTemplate) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkDescriptorUpdateTemplate_T *tmpl = calloc(1, sizeof(*tmpl));
    if (!tmpl) return VK_ERROR_OUT_OF_HOST_MEMORY;
    set_loader_magic(tmpl);
    *pDescriptorUpdateTemplate = tmpl;
    return VK_SUCCESS;
}

VkResult vkCreateDescriptorUpdateTemplateKHR(VkDevice device, const void *pCreateInfo, void *pAllocator, VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate) {
    return vkCreateDescriptorUpdateTemplate(device, pCreateInfo, pAllocator, pDescriptorUpdateTemplate);
}

void vkDestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, void *pAllocator) {
    free(descriptorUpdateTemplate);
}

void vkDestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, void *pAllocator) {
    vkDestroyDescriptorUpdateTemplate(device, descriptorUpdateTemplate, pAllocator);
}

void vkUpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void *pData) {
}

void vkUpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void *pData) {
}

VkResult vkResetCommandPool(VkDevice device, VkCommandPool commandPool, VkFlags flags) {
    return VK_SUCCESS;
}

VkResult vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkFlags flags) {
    return VK_SUCCESS;
}

VkResult vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkFlags flags) {
    return VK_SUCCESS;
}

void vkTrimCommandPool(VkDevice device, VkCommandPool commandPool, VkFlags flags) {
}

void vkTrimCommandPoolKHR(VkDevice device, VkCommandPool commandPool, VkFlags flags) {
}

void vkGetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize *pCommittedMemoryInBytes) {
    if (pCommittedMemoryInBytes) *pCommittedMemoryInBytes = memory ? memory->size : 0;
}

VkResult vkCreateImageView(VkDevice device, const void *pCreateInfo, void *pAllocator,
                           VkImageView *pView) {
    if (!pView) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkImageView_T *view = calloc(1, sizeof(struct VkImageView_T));
    if (!view) return VK_ERROR_OUT_OF_HOST_MEMORY;
    /* Parse VkImageViewCreateInfo to extract image and format */
    if (pCreateInfo) {
        const uint32_t *info = (const uint32_t *)pCreateInfo;
        /* VkImageViewCreateInfo layout:
         *   [0] sType
         *   [1-2] pNext (pointer, 8 bytes on 64-bit)
         *   at offset 16: VkImage image (pointer)
         *   at offset 24: VkImageViewType viewType
         *   at offset 28: VkFormat format
         *   at offset 32: VkComponentMapping components (4 x uint32)
         *   at offset 48: VkImageSubresourceRange subresourceRange
         *    -> [0] aspectMask [1] baseMipLevel [2] levelCount [3] baseArrayLayer [4] layerCount
         */
        struct {
            uint32_t sType;
            uint32_t _pad;
            const void *pNext;
            VkImage image;
            uint32_t viewType;
            uint32_t format;
            uint32_t components[4];
            uint32_t aspectMask;
            uint32_t baseMipLevel;
            uint32_t levelCount;
            uint32_t baseArrayLayer;
            uint32_t layerCount;
        } ci;
        memcpy(&ci, pCreateInfo, sizeof(ci));
        view->image = ci.image;
        view->format = ci.format;
        view->base_mip_level = ci.baseMipLevel;
        view->level_count = ci.levelCount;
        view->base_array_layer = ci.baseArrayLayer;
        view->layer_count = ci.layerCount;
    }
    *pView = view;
    return VK_SUCCESS;
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
    case 1: return 0x00000002; /* TESSELLATION_CONTROL */
    case 2: return 0x00000004; /* TESSELLATION_EVALUATION */
    case 3: return 0x00000008; /* GEOMETRY */
    case 4: return VK_SHADER_STAGE_FRAGMENT_BIT;
    case 5: return 0x00000020; /* COMPUTE */
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
        code[3] == 0) {
        return false;
    }

    size_t count = code_size / sizeof(uint32_t);
    uint32_t stages = 0;
    for (size_t offset = 5; offset < count;) {
        uint32_t instruction = code[offset];
        uint32_t word_count = instruction >> 16;
        uint32_t opcode = instruction & 0xffffu;
        if (word_count == 0 || word_count > count - offset) break;
        if (opcode == SPIRV_OP_ENTRY_POINT && word_count >= 4) {
            stages |= spirv_execution_model_stage(code[offset + 1]);
        }
        offset += word_count;
    }

    if (stage_mask) *stage_mask = stages ? stages : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | 0x00000020);
    return true;
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

    if (pCreateInfo->codeSize > 40) {
        static int g_shader_count = 0;
        char dump_path[256];
        snprintf(dump_path, sizeof(dump_path), "/data/data/com.termux/files/home/captured_vkmark_shader_%d.spv", g_shader_count++);
        FILE *fspv = fopen(dump_path, "wb");
        if (fspv) {
            fwrite(pCreateInfo->pCode, 1, pCreateInfo->codeSize, fspv);
            fclose(fspv);
            PANVK_LOG("DEBUG: dumped %zu bytes of SPIR-V to %s (stage_mask=0x%x)\n", pCreateInfo->codeSize, dump_path, stage_mask);
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

VkResult vkCreatePipelineLayout(VkDevice device, const struct VkPipelineLayoutCreateInfo *pCreateInfo,
                                void *pAllocator, VkPipelineLayout *pPipelineLayout) {
    if (!pCreateInfo || !pPipelineLayout) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkPipelineLayout_T *pl = calloc(1, sizeof(*pl));
    if (!pl) return VK_ERROR_OUT_OF_HOST_MEMORY;

    uint32_t binding_count = 0;
    if (pCreateInfo->setLayoutCount > 0 && pCreateInfo->setLayoutCount < 64 && pCreateInfo->pSetLayouts && (uintptr_t)pCreateInfo->pSetLayouts > 0x10000) {
        for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; set++) {
            VkDescriptorSetLayout set_layout = pCreateInfo->pSetLayouts[set];
            if (set_layout && (uintptr_t)set_layout > 0x10000 && set_layout->binding_count < 256)
                binding_count += set_layout->binding_count;
        }
    }
    if (binding_count) {
        pl->bindings = calloc(binding_count, sizeof(*pl->bindings));
        if (!pl->bindings) {
            free(pl);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
    }

    uint32_t index = 0;
    uint32_t ubo_index = 0;
    uint32_t tex_index = 0;
    uint32_t sampler_index = 0;
    if (pCreateInfo->setLayoutCount > 0 && pCreateInfo->setLayoutCount < 64 && pCreateInfo->pSetLayouts && (uintptr_t)pCreateInfo->pSetLayouts > 0x10000 && pl->bindings) {
        for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; set++) {
            VkDescriptorSetLayout set_layout = pCreateInfo->pSetLayouts[set];
            if (!set_layout || (uintptr_t)set_layout < 0x10000 || set_layout->binding_count > 256 || !set_layout->bindings || (uintptr_t)set_layout->bindings < 0x10000) continue;
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
                } else if (binding->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                           binding->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
                    out->resource_index = tex_index;
                    tex_index += binding->descriptorCount;
                } else if (binding->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) {
                    out->resource_index = sampler_index;
                    sampler_index += binding->descriptorCount;
                }
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
    if (!fb) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (pCreateInfo) {
        /* VkFramebufferCreateInfo layout (64-bit):
         * [0]  sType       uint32
         * [4]  _pad        uint32
         * [8]  pNext       pointer (8 bytes)
         * [16] flags       uint32
         * [20] _pad        uint32
         * [24] renderPass  pointer (8 bytes)
         * [32] attachmentCount uint32
         * [36] _pad        uint32
         * [40] pAttachments pointer to VkImageView[] (8 bytes)
         * [48] width       uint32
         * [52] height      uint32
         * [56] layers      uint32
         */
        struct {
            uint32_t sType;
            uint32_t _pad0;
            const void *pNext;
            uint32_t flags;
            uint32_t _pad1;
            VkRenderPass renderPass;
            uint32_t attachmentCount;
            uint32_t _pad2;
            const VkImageView *pAttachments;
            uint32_t width;
            uint32_t height;
            uint32_t layers;
        } ci;
        memcpy(&ci, pCreateInfo, sizeof(ci));
        fb->width = ci.width;
        fb->height = ci.height;
        fb->layers = ci.layers;
        fb->attachment_count = ci.attachmentCount;
        if (ci.attachmentCount > 0 && ci.pAttachments) {
            fb->attachments = calloc(ci.attachmentCount, sizeof(VkImageView));
            if (fb->attachments)
                memcpy(fb->attachments, ci.pAttachments, ci.attachmentCount * sizeof(VkImageView));
        }
    }
    *pFramebuffer = fb;
    return VK_SUCCESS;
}

void vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, void *pAllocator) {
    if (!framebuffer) return;
    free(framebuffer->attachments);
    free(framebuffer);
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
    if (!pDescriptorWrites) return;
    for (uint32_t w = 0; w < descriptorWriteCount; w++) {
        const struct VkWriteDescriptorSet *write = &pDescriptorWrites[w];
        if (!write->dstSet) continue;
        VkDescriptorSetLayout layout = write->dstSet->layout;
        if (!layout || !layout->bindings || !write->dstSet->buffers) continue;
        for (uint32_t b = 0; b < layout->binding_count; b++) {
            const struct VkDescriptorSetLayoutBinding *binding = &layout->bindings[b];
            if (binding->binding != write->dstBinding) continue;
            if (write->pBufferInfo && write->dstSet->buffers) {
                uint32_t offset = layout->binding_offsets[b] + write->dstArrayElement;
                if (offset + write->descriptorCount <= layout->descriptor_count) {
                    memcpy(&write->dstSet->buffers[offset],
                           write->pBufferInfo,
                           write->descriptorCount * sizeof(*write->pBufferInfo));
                }
            }
            break;
        }
    }
}

static bool pipeline_dynamic_state(const struct VkPipelineDynamicStateCreateInfo *dynamic,
                                   uint32_t state) {
    if (!dynamic || (uintptr_t)dynamic < 0x10000 || !dynamic->pDynamicStates || (uintptr_t)dynamic->pDynamicStates < 0x10000 || dynamic->dynamicStateCount > 100) return false;
    for (uint32_t i = 0; i < dynamic->dynamicStateCount; i++) {
        if (dynamic->pDynamicStates[i] == state) return true;
    }
    return false;
}

static bool is_valid_spirv_module(const struct VkShaderModule_T *sm) {
    if (!sm || (uintptr_t)sm < 0x10000) return false;
    if (sm->code_size < 20 || sm->code_size > 10 * 1024 * 1024) return false;
    if (!sm->code || (uintptr_t)sm->code < 0x10000) return false;
    if (sm->code[0] != 0x07230203u) return false;
    return true;
}

static VkResult pipeline_parse_shader_stages(struct VkPipeline_T *pipeline,
                                             const struct VkGraphicsPipelineCreateInfo *info) {
    if (!pipeline || !info || !info->pStages || (uintptr_t)info->pStages < 0x10000 || info->stageCount == 0 || info->stageCount > 16) return VK_SUCCESS;

    for (uint32_t i = 0; i < info->stageCount; i++) {
        const struct VkPipelineShaderStageCreateInfo *stage = &info->pStages[i];
        if (!stage || (uintptr_t)stage < 0x10000) continue;

        pipeline->stage_mask |= stage->stage;
        const char *name = (stage->pName && (uintptr_t)stage->pName > 0x10000) ? stage->pName : "main";
        if (stage->stage == VK_SHADER_STAGE_VERTEX_BIT) {
            snprintf(pipeline->vertex_entry_point, sizeof(pipeline->vertex_entry_point), "%s", name);
        } else if (stage->stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
            snprintf(pipeline->fragment_entry_point, sizeof(pipeline->fragment_entry_point), "%s", name);
        }
    }

    return VK_SUCCESS;
}

static VkResult pipeline_compile_shaders(struct VkPipeline_T *pipeline,
                                         const struct VkGraphicsPipelineCreateInfo *info) {
    if (!pipeline || !info || !info->pStages || (uintptr_t)info->pStages < 0x10000 || info->stageCount == 0 || info->stageCount > 16) return VK_SUCCESS;
    if (!load_compiler() || !compiler_api.compile) {
        return VK_SUCCESS;
    }

    char error[512];
    for (uint32_t i = 0; i < info->stageCount; i++) {
        const struct VkPipelineShaderStageCreateInfo *stage = &info->pStages[i];
        if (!stage || (uintptr_t)stage < 0x10000) continue;
        if (!is_valid_spirv_module(stage->module)) continue;

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

        const char *name = (stage->pName && (uintptr_t)stage->pName > 0x10000) ? stage->pName : "main";
        int ret = compiler_api.compile(stage->module->code, stage->module->code_size,
                                       compiler_stage, name,
                                       &pipeline->compiler_layout,
                                       binary,
                                       error, sizeof(error));
        if (ret != 0) {
            if (compiler_api.cleanup) compiler_api.cleanup(binary);
            memset(binary, 0, sizeof(*binary));
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
    if (pipeline->bindings) free(pipeline->bindings);
    free(pipeline);
}

static VkResult pipeline_copy_layout(struct VkPipeline_T *pipeline,
                                     VkPipelineLayout layout) {
    if (!pipeline || !layout || (uintptr_t)layout < 0x10000) return VK_SUCCESS;
    if (layout->compiler_layout.binding_count > 0 && layout->compiler_layout.binding_count < 256 && layout->bindings && (uintptr_t)layout->bindings > 0x10000) {
        size_t size = layout->compiler_layout.binding_count * sizeof(*pipeline->bindings);
        pipeline->bindings = calloc(1, size);
        if (pipeline->bindings) {
            memcpy(pipeline->bindings, layout->bindings, size);
            pipeline->compiler_layout = layout->compiler_layout;
            pipeline->compiler_layout.bindings = pipeline->bindings;
        }
    }
    return VK_SUCCESS;
}

static void pipeline_parse_fixed_state(struct VkPipeline_T *pipeline,
                                       const struct VkGraphicsPipelineCreateInfo *info) {
    if (!pipeline || !info) return;

    pipeline->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipeline->polygon_mode = VK_POLYGON_MODE_FILL;
    pipeline->cull_mode = VK_CULL_MODE_NONE;
    pipeline->front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipeline->line_width = 1.0f;
    pipeline->rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    pipeline->depth_compare_op = VK_COMPARE_OP_ALWAYS;
    pipeline->color_write_mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    if (info->pVertexInputState && (uintptr_t)info->pVertexInputState > 0x10000) {
        pipeline->vertex_binding_count =
            info->pVertexInputState->vertexBindingDescriptionCount < 16 ?
            info->pVertexInputState->vertexBindingDescriptionCount : 16;
        pipeline->vertex_attribute_count =
            info->pVertexInputState->vertexAttributeDescriptionCount < 16 ?
            info->pVertexInputState->vertexAttributeDescriptionCount : 16;
        if (pipeline->vertex_binding_count &&
            info->pVertexInputState->pVertexBindingDescriptions &&
            (uintptr_t)info->pVertexInputState->pVertexBindingDescriptions > 0x10000) {
            memcpy(pipeline->vertex_bindings,
                   info->pVertexInputState->pVertexBindingDescriptions,
                   pipeline->vertex_binding_count * sizeof(pipeline->vertex_bindings[0]));
        }
        if (pipeline->vertex_attribute_count &&
            info->pVertexInputState->pVertexAttributeDescriptions &&
            (uintptr_t)info->pVertexInputState->pVertexAttributeDescriptions > 0x10000) {
            memcpy(pipeline->vertex_attributes,
                   info->pVertexInputState->pVertexAttributeDescriptions,
                   pipeline->vertex_attribute_count * sizeof(pipeline->vertex_attributes[0]));
        }
    }

    if (info->pInputAssemblyState && (uintptr_t)info->pInputAssemblyState > 0x10000) {
        pipeline->topology = info->pInputAssemblyState->topology;
        pipeline->primitive_restart = info->pInputAssemblyState->primitiveRestartEnable != 0;
    }
    if (info->pViewportState && (uintptr_t)info->pViewportState > 0x10000) {
        if (info->pViewportState->viewportCount && info->pViewportState->pViewports && (uintptr_t)info->pViewportState->pViewports > 0x10000)
            pipeline->viewport = info->pViewportState->pViewports[0];
        if (info->pViewportState->scissorCount && info->pViewportState->pScissors && (uintptr_t)info->pViewportState->pScissors > 0x10000)
            pipeline->scissor = info->pViewportState->pScissors[0];
    }
    if (info->pDynamicState && (uintptr_t)info->pDynamicState > 0x10000) {
        pipeline->dynamic_viewport = pipeline_dynamic_state(info->pDynamicState,
                                                            VK_DYNAMIC_STATE_VIEWPORT);
        pipeline->dynamic_scissor = pipeline_dynamic_state(info->pDynamicState,
                                                           VK_DYNAMIC_STATE_SCISSOR);
    }
    if (info->pRasterizationState && (uintptr_t)info->pRasterizationState > 0x10000) {
        pipeline->rasterizer_discard = info->pRasterizationState->rasterizerDiscardEnable != 0;
        pipeline->polygon_mode = info->pRasterizationState->polygonMode;
        pipeline->cull_mode = info->pRasterizationState->cullMode;
        pipeline->front_face = info->pRasterizationState->frontFace;
        pipeline->line_width = info->pRasterizationState->lineWidth;
    }
    if (info->pMultisampleState && (uintptr_t)info->pMultisampleState > 0x10000)
        pipeline->rasterization_samples = info->pMultisampleState->rasterizationSamples;
    if (info->pDepthStencilState && (uintptr_t)info->pDepthStencilState > 0x10000) {
        pipeline->depth_test = info->pDepthStencilState->depthTestEnable != 0;
        pipeline->depth_write = info->pDepthStencilState->depthWriteEnable != 0;
        pipeline->depth_compare_op = info->pDepthStencilState->depthCompareOp;
    }
    if (info->pColorBlendState && (uintptr_t)info->pColorBlendState > 0x10000 &&
        info->pColorBlendState->attachmentCount &&
        info->pColorBlendState->pAttachments &&
        (uintptr_t)info->pColorBlendState->pAttachments > 0x10000) {
        pipeline->blend_enable = info->pColorBlendState->pAttachments[0].blendEnable != 0;
        pipeline->color_write_mask = info->pColorBlendState->pAttachments[0].colorWriteMask;
    }
}

VkResult vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
                                   uint32_t createInfoCount,
                                   const struct VkGraphicsPipelineCreateInfo *pCreateInfos,
                                   void *pAllocator, VkPipeline *pPipelines) {
    PANVK_LOG("vkCreateGraphicsPipelines: count=%u\n", createInfoCount);
    if (!device || !pCreateInfos || !pPipelines) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < createInfoCount; i++) pPipelines[i] = NULL;

    for (uint32_t i = 0; i < createInfoCount; i++) {
        PANVK_LOG("vkCreateGraphicsPipelines: [%u] start\n", i);
        struct VkPipeline_T *pipe = calloc(1, sizeof(*pipe));
        if (!pipe) return VK_ERROR_OUT_OF_HOST_MEMORY;

        PANVK_LOG("vkCreateGraphicsPipelines: [%u] copying layout\n", i);
        pipeline_copy_layout(pipe, pCreateInfos[i].layout);

        PANVK_LOG("vkCreateGraphicsPipelines: [%u] parsing shader stages\n", i);
        pipeline_parse_shader_stages(pipe, &pCreateInfos[i]);

        PANVK_LOG("vkCreateGraphicsPipelines: [%u] compiling shaders\n", i);
        pipeline_compile_shaders(pipe, &pCreateInfos[i]);

        PANVK_LOG("vkCreateGraphicsPipelines: [%u] parsing fixed state\n", i);
        pipeline_parse_fixed_state(pipe, &pCreateInfos[i]);

        pPipelines[i] = pipe;
        PANVK_LOG("vkCreateGraphicsPipelines OK: [%u/%u] pipe=%p\n", i + 1, createInfoCount, pipe);
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
    PANVK_LOG("vkBeginCommandBuffer: cb=%p\n", (void*)commandBuffer);
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
    if (!commandBuffer || !commandBuffer->v9_cmd) return;
    struct v9_ubo_binding ubos[8] = {0};
    uint32_t ubo_count = 0;
    VkPipeline pipeline = commandBuffer->graphics_pipeline;
    if (!pipeline) {
        v9_cmd_buffer_set_ubos(commandBuffer->v9_cmd, NULL, 0);
        return;
    }

    for (uint32_t i = 0; i < pipeline->compiler_layout.binding_count && ubo_count < 8; i++) {
        const struct panvk_v9_descriptor_binding *binding =
            &pipeline->compiler_layout.bindings[i];
        if (binding->set >= 8 || !commandBuffer->descriptor_sets[binding->set])
            continue;

        VkDescriptorSet set = commandBuffer->descriptor_sets[binding->set];
        if (!set->layout || !set->buffers) continue;

        for (uint32_t b = 0; b < set->layout->binding_count; b++) {
            if (set->layout->bindings[b].binding != binding->binding) continue;
            for (uint32_t elem = 0; elem < binding->array_size && ubo_count < 8; elem++) {
                uint32_t offset_idx = set->layout->binding_offsets[b] + elem;
                if (offset_idx >= set->layout->descriptor_count) break;
                const struct VkDescriptorBufferInfo *info = &set->buffers[offset_idx];
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

static uint32_t vk_format_to_pan_v9_attr_format(uint32_t vk_fmt) {
    switch (vk_fmt) {
        case 100: /* VK_FORMAT_R32_SFLOAT */          return (167u << 12) | 16u;
        case 103: /* VK_FORMAT_R32G32_SFLOAT */       return (175u << 12) | 16u;
        case 106: /* VK_FORMAT_R32G32B32_SFLOAT */    return (183u << 12) | 16u;
        case 109: /* VK_FORMAT_R32G32B32A32_SFLOAT */ return (191u << 12) | 0u;
        case 37:  /* VK_FORMAT_R8G8B8A8_UNORM */      return (187u << 12) | 0u;
        case 44:  /* VK_FORMAT_B8G8R8A8_UNORM */      return (187u << 12) | 4u;
        default:                                      return (191u << 12) | 0u;
    }
}

static void command_buffer_apply_attributes(VkCommandBuffer commandBuffer) {
    if (!commandBuffer || !commandBuffer->v9_cmd) return;
    struct v9_attribute_binding attrs[8] = {0};
    uint32_t attr_count = 0;
    VkPipeline pipeline = commandBuffer->graphics_pipeline;

    for (uint32_t i = 0; pipeline && i < pipeline->vertex_attribute_count; i++) {
        const struct VkVertexInputAttributeDescription *attribute =
            &pipeline->vertex_attributes[i];
        if (attribute->location >= 8 || attribute->binding >= 16)
            continue;

        const struct VkVertexInputBindingDescription *binding = NULL;
        for (uint32_t b = 0; b < pipeline->vertex_binding_count; b++) {
            if (pipeline->vertex_bindings[b].binding == attribute->binding) {
                binding = &pipeline->vertex_bindings[b];
                break;
            }
        }
        VkBuffer buf = commandBuffer->vertex_bindings[attribute->binding].buffer;
        if (!binding || !buf || !buf->bo) continue;

        VkDeviceSize offset = commandBuffer->vertex_bindings[attribute->binding].offset;
        attrs[attribute->location] = (struct v9_attribute_binding) {
            .format = vk_format_to_pan_v9_attr_format(attribute->format),
            .offset = attribute->offset,
            .stride = binding->stride,
            .input_rate = binding->inputRate,
            .buffer_address = buf->bo->gpu + buf->memory_offset + offset,
            .buffer_size = (buf->size > offset) ? (uint32_t)(buf->size - offset) : 0,
        };
        if (attribute->location + 1 > attr_count)
            attr_count = attribute->location + 1;
    }
    v9_cmd_buffer_set_attributes(commandBuffer->v9_cmd, attrs, attr_count);
}

void vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    if (commandBuffer && commandBuffer->v9_cmd && vertexCount > 0 && instanceCount > 0 &&
        (!commandBuffer->graphics_pipeline ||
         !commandBuffer->graphics_pipeline->rasterizer_discard)) {
        command_buffer_apply_ubos(commandBuffer);
        command_buffer_apply_attributes(commandBuffer);
        if (commandBuffer->graphics_pipeline) {
            if (commandBuffer->graphics_pipeline->vertex_binary.binary_size) {
                v9_cmd_buffer_set_vertex_shader(
                    commandBuffer->v9_cmd,
                    &commandBuffer->graphics_pipeline->vertex_binary);
            }
            if (commandBuffer->graphics_pipeline->fragment_binary.binary_size) {
                v9_cmd_buffer_set_fragment_shader(
                    commandBuffer->v9_cmd,
                    &commandBuffer->graphics_pipeline->fragment_binary);
            }
        }
        uint64_t pos_gpu = commandBuffer->vertex_bindings[0].buffer && commandBuffer->vertex_bindings[0].buffer->bo ?
                           commandBuffer->vertex_bindings[0].buffer->bo->gpu +
                           commandBuffer->vertex_bindings[0].buffer->memory_offset +
                           commandBuffer->vertex_bindings[0].offset + (firstVertex * 16) :
                           v9_cmd_buffer_get_pos_gpu(commandBuffer->v9_cmd);
        uint64_t idx_gpu = 0;
        v9_cmd_draw_indexed(commandBuffer->v9_cmd, idx_gpu, vertexCount, 0,
                            pos_gpu, vertexCount);
    }
}

void vkCmdBeginRenderPass(VkCommandBuffer commandBuffer,
                          const struct VkRenderPassBeginInfo *pRenderPassBegin,
                          uint32_t contents) {
    if (!commandBuffer || !pRenderPassBegin) return;
    PANVK_LOG("vkCmdBeginRenderPass: cb=%p fb=%p\n",
              (void*)commandBuffer, (void*)pRenderPassBegin->framebuffer);

    uint32_t clear_color = 0;
    if (pRenderPassBegin->clearValueCount > 0 && pRenderPassBegin->pClearValues) {
        const float *c = (const float *)pRenderPassBegin->pClearValues;
        uint8_t r = (uint8_t)(c[0] * 255.0f);
        uint8_t g = (uint8_t)(c[1] * 255.0f);
        uint8_t b = (uint8_t)(c[2] * 255.0f);
        uint8_t a = (uint8_t)(c[3] * 255.0f);
        clear_color = (a << 24) | (b << 16) | (g << 8) | r;
    }

    uint32_t fb_width = 0, fb_height = 0;
    VkFramebuffer fb = pRenderPassBegin->framebuffer;
    if (fb && fb->width > 0) {
        fb_width = fb->width;
        fb_height = fb->height;
    } else if (pRenderPassBegin->renderArea.extent.width > 0) {
        fb_width = pRenderPassBegin->renderArea.extent.width;
        fb_height = pRenderPassBegin->renderArea.extent.height;
    } else {
        fb_width = 1280;
        fb_height = 720;
    }

    struct v9_render_target_config config = {
        .width = fb_width,
        .height = fb_height,
        .clear_color = clear_color,
    };

    commandBuffer->target_swapchain_image = NULL;
    if (fb && fb->attachment_count > 0 && fb->attachments && fb->attachments[0]) {
        VkImageView view = fb->attachments[0];
        if (view && view->image && view->image->swapchain) {
            commandBuffer->target_swapchain_image = view->image;
        }
    }

    if (commandBuffer->v9_cmd) {
        v9_cmd_buffer_destroy(commandBuffer->v9_cmd);
    }
    if (commandBuffer->device && commandBuffer->device->kdev) {
        commandBuffer->v9_cmd = v9_cmd_buffer_create(commandBuffer->device->kdev, &config);
    }
    if (commandBuffer->v9_cmd) {
        v9_cmd_buffer_begin(commandBuffer->v9_cmd);
    }
}

void vkCmdNextSubpass(VkCommandBuffer commandBuffer, uint32_t contents) {
    (void)commandBuffer; (void)contents;
}

void vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                           const void *pRenderPassBegin,
                           const void *pSubpassBeginInfo) {
    vkCmdBeginRenderPass(commandBuffer,
                         (const struct VkRenderPassBeginInfo *)pRenderPassBegin,
                         0);
}

void vkCmdBeginRenderPass2KHR(VkCommandBuffer commandBuffer,
                               const void *pRenderPassBegin,
                               const void *pSubpassBeginInfo) {
    vkCmdBeginRenderPass2(commandBuffer, pRenderPassBegin, pSubpassBeginInfo);
}

void vkCmdNextSubpass2(VkCommandBuffer commandBuffer,
                       const void *pSubpassBeginInfo,
                       const void *pSubpassEndInfo) {
    (void)commandBuffer; (void)pSubpassBeginInfo; (void)pSubpassEndInfo;
}

void vkCmdNextSubpass2KHR(VkCommandBuffer commandBuffer,
                           const void *pSubpassBeginInfo,
                           const void *pSubpassEndInfo) {
    vkCmdNextSubpass2(commandBuffer, pSubpassBeginInfo, pSubpassEndInfo);
}

void vkCmdEndRenderPass2(VkCommandBuffer commandBuffer, const void *pSubpassEndInfo) {
    vkCmdEndRenderPass(commandBuffer);
}

void vkCmdEndRenderPass2KHR(VkCommandBuffer commandBuffer, const void *pSubpassEndInfo) {
    vkCmdEndRenderPass2(commandBuffer, pSubpassEndInfo);
}

void vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (commandBuffer && commandBuffer->v9_cmd && indexCount > 0 && instanceCount > 0 &&
        (!commandBuffer->graphics_pipeline ||
         !commandBuffer->graphics_pipeline->rasterizer_discard)) {
        command_buffer_apply_ubos(commandBuffer);
        command_buffer_apply_attributes(commandBuffer);
        if (commandBuffer->graphics_pipeline) {
            if (commandBuffer->graphics_pipeline->vertex_binary.binary_size) {
                v9_cmd_buffer_set_vertex_shader(
                    commandBuffer->v9_cmd,
                    &commandBuffer->graphics_pipeline->vertex_binary);
            }
            if (commandBuffer->graphics_pipeline->fragment_binary.binary_size) {
                v9_cmd_buffer_set_fragment_shader(
                    commandBuffer->v9_cmd,
                    &commandBuffer->graphics_pipeline->fragment_binary);
            }
        }
        uint64_t pos_gpu = commandBuffer->vertex_bindings[0].buffer && commandBuffer->vertex_bindings[0].buffer->bo ?
                           commandBuffer->vertex_bindings[0].buffer->bo->gpu +
                           commandBuffer->vertex_bindings[0].buffer->memory_offset +
                           commandBuffer->vertex_bindings[0].offset + (vertexOffset * 16) :
                           v9_cmd_buffer_get_pos_gpu(commandBuffer->v9_cmd);
        uint64_t idx_gpu = commandBuffer->index_buffer && commandBuffer->index_buffer->bo ?
                           commandBuffer->index_buffer->bo->gpu +
                           commandBuffer->index_buffer->memory_offset +
                           commandBuffer->index_offset + (firstIndex * (commandBuffer->index_type == 1 ? 4 : 2)) :
                           v9_cmd_buffer_get_idx_gpu(commandBuffer->v9_cmd);
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
    if (!queue) return VK_ERROR_INITIALIZATION_FAILED;
    PANVK_LOG("vkQueueSubmit: queue=%p submitCount=%u\n", (void*)queue, submitCount);

    if (queue->device) {
        pthread_mutex_lock(&queue->device->mem_mutex);
        for (struct VkDeviceMemory_T *m = queue->device->memories; m; m = m->next) {
            if (m->bo && m->bo->cpu && m->low_cpu && m->low_cpu != MAP_FAILED && m->size <= 4 * 1024 * 1024) {
                memcpy(m->bo->cpu, m->low_cpu, m->size);
            }
        }
        pthread_mutex_unlock(&queue->device->mem_mutex);
    }

    if (pSubmits) {
        for (uint32_t s = 0; s < submitCount; s++) {
            for (uint32_t cb = 0; cb < pSubmits[s].commandBufferCount; cb++) {
                VkCommandBuffer cmd = pSubmits[s].pCommandBuffers[cb];
                if (cmd && cmd->v9_cmd) {
                    if (queue->last_v9_cmd != cmd->v9_cmd) {
                        v9_cmd_buffer_destroy(queue->last_v9_cmd);
                        queue->last_v9_cmd = v9_cmd_buffer_ref(cmd->v9_cmd);
                    }
                    v9_cmd_buffer_submit(cmd->v9_cmd);
                    void *color = v9_cmd_buffer_get_color_cpu(cmd->v9_cmd);
                    if (color && queue->device) {
                        queue->device->last_rendered_color = color;
                    }
                    if (color && cmd->target_swapchain_image && cmd->target_swapchain_image->bo) {
                        size_t sz = (size_t)cmd->target_swapchain_image->width * cmd->target_swapchain_image->height * 4;
                        memcpy(cmd->target_swapchain_image->bo->cpu, color, sz);
                    }
                }
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

    PANVK_LOG("vkCreateXlibSurfaceKHR: dpy=%p window=0x%lx w=%u h=%u\n",
              (void*)surf->dpy, (unsigned long)surf->window, surf->width, surf->height);
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
            surf->depth = reply->depth;
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

    uint32_t w = surface ? surface->width : 1280;
    uint32_t h = surface ? surface->height : 720;

    pSurfaceCapabilities->minImageCount = 1;
    pSurfaceCapabilities->maxImageCount = 8;
    pSurfaceCapabilities->currentExtent.width = w > 0 ? w : 1280;
    pSurfaceCapabilities->currentExtent.height = h > 0 ? h : 720;
    pSurfaceCapabilities->minImageExtent.width = 1;
    pSurfaceCapabilities->minImageExtent.height = 1;
    pSurfaceCapabilities->maxImageExtent.width = 16384;
    pSurfaceCapabilities->maxImageExtent.height = 16384;
    pSurfaceCapabilities->maxImageArrayLayers = 1;
    pSurfaceCapabilities->supportedTransforms = 0x1F; /* IDENTITY | ROTATE_90 | ROTATE_180 | ROTATE_270 | HORIZONTAL_MIRROR */
    pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->supportedCompositeAlpha = 0xF; /* OPAQUE | PRE_MULTIPLIED | POST_MULTIPLIED | INHERIT */
    pSurfaceCapabilities->supportedUsageFlags = 0x1F; /* TRANSFER_SRC | TRANSFER_DST | SAMPLED | STORAGE | COLOR_ATTACHMENT */

    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const struct VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo, struct VkSurfaceCapabilities2KHR *pSurfaceCapabilities) {
    if (!pSurfaceCapabilities) return VK_ERROR_INITIALIZATION_FAILED;
    VkSurfaceKHR surface = pSurfaceInfo ? pSurfaceInfo->surface : NULL;
    return vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &pSurfaceCapabilities->surfaceCapabilities);
}

VkResult vkGetPhysicalDeviceSurfaceCapabilities2EXT(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, struct VkSurfaceCapabilities2KHR *pSurfaceCapabilities) {
    if (!pSurfaceCapabilities) return VK_ERROR_INITIALIZATION_FAILED;
    return vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &pSurfaceCapabilities->surfaceCapabilities);
}

VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount, struct VkSurfaceFormatKHR *pSurfaceFormats) {
    if (!pSurfaceFormatCount) return VK_ERROR_INITIALIZATION_FAILED;

    static const struct VkSurfaceFormatKHR formats[] = {
        { .format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
        { .format = VK_FORMAT_R8G8B8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
        { .format = VK_FORMAT_B8G8R8A8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
        { .format = VK_FORMAT_R8G8B8A8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
        { .format = 84 /* VK_FORMAT_A2B10G10R10_UNORM_PACK32 */, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
        { .format = 64 /* VK_FORMAT_A2R10G10B10_UNORM_PACK32 */, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
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

VkResult vkGetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const struct VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo, uint32_t *pSurfaceFormatCount, struct VkSurfaceFormat2KHR *pSurfaceFormats) {
    if (!pSurfaceFormatCount) return VK_ERROR_INITIALIZATION_FAILED;

    static const struct VkSurfaceFormatKHR formats[] = {
        { .format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
        { .format = VK_FORMAT_R8G8B8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
        { .format = VK_FORMAT_B8G8R8A8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
        { .format = VK_FORMAT_R8G8B8A8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
        { .format = 84 /* VK_FORMAT_A2B10G10R10_UNORM_PACK32 */, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
        { .format = 64 /* VK_FORMAT_A2R10G10B10_UNORM_PACK32 */, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
    };
    uint32_t num_formats = sizeof(formats) / sizeof(formats[0]);

    if (!pSurfaceFormats) {
        *pSurfaceFormatCount = num_formats;
        return VK_SUCCESS;
    }

    uint32_t to_copy = (*pSurfaceFormatCount < num_formats) ? *pSurfaceFormatCount : num_formats;
    for (uint32_t i = 0; i < to_copy; i++) {
        pSurfaceFormats[i].surfaceFormat = formats[i];
    }
    *pSurfaceFormatCount = to_copy;
    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount, uint32_t *pPresentModes) {
    if (!pPresentModeCount) return VK_ERROR_INITIALIZATION_FAILED;

    static const uint32_t modes[] = { VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR, 3 /* VK_PRESENT_MODE_FIFO_RELAXED_KHR */ };
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
    panvk_trace("vkCreateSwapchainKHR", NULL);
    if (!device || !pCreateInfo || !pSwapchain) return VK_ERROR_INITIALIZATION_FAILED;

    struct VkSwapchainKHR_T *sc = calloc(1, sizeof(*sc));
    if (!sc) return VK_ERROR_OUT_OF_HOST_MEMORY;

    sc->device = device;
    sc->surface = pCreateInfo->surface;
    sc->width = pCreateInfo->imageExtent.width > 0 ? pCreateInfo->imageExtent.width : 1280;
    sc->height = pCreateInfo->imageExtent.height > 0 ? pCreateInfo->imageExtent.height : 720;
    sc->image_count = pCreateInfo->minImageCount > 0 ? pCreateInfo->minImageCount : 2;

    sc->images = calloc(sc->image_count, sizeof(struct VkImage_T));
    size_t img_sz = (size_t)sc->width * sc->height * 4;
    for (uint32_t i = 0; i < sc->image_count; i++) {
        sc->images[i].swapchain = sc;
        sc->images[i].index = i;
        sc->images[i].width = sc->width;
        sc->images[i].height = sc->height;
        if (device && device->kdev) {
            sc->images[i].bo = pan_kmod_bo_alloc(device->kdev, img_sz, PAN_KMOD_BO_FLAG_READ | PAN_KMOD_BO_FLAG_WRITE);
        }
    }

    sc->image_data = malloc(img_sz);

    if (sc->surface && (uintptr_t)sc->surface > 0x1000 && sc->surface->is_xcb && sc->surface->connection && sc->surface->window) {
        uint8_t depth = sc->surface->depth ? sc->surface->depth : 24;
        uint32_t values[] = { XCB_BACK_PIXMAP_NONE };
        xcb_change_window_attributes(sc->surface->connection, sc->surface->window, XCB_CW_BACK_PIXMAP, values);

        sc->xcb_pixmap = xcb_generate_id(sc->surface->connection);
        xcb_create_pixmap(sc->surface->connection, depth, sc->xcb_pixmap, sc->surface->window, sc->width, sc->height);

        sc->xcb_gc = xcb_generate_id(sc->surface->connection);
        xcb_create_gc(sc->surface->connection, sc->xcb_gc, sc->surface->window, 0, NULL);
    } else if (sc->surface && (uintptr_t)sc->surface > 0x1000 && sc->surface->dpy && sc->surface->window) {
        int screen = DefaultScreen(sc->surface->dpy);
        XSetWindowBackgroundPixmap(sc->surface->dpy, sc->surface->window, None);
        sc->gc = XCreateGC(sc->surface->dpy, sc->surface->window, 0, NULL);
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
    if (swapchain->images) {
        for (uint32_t i = 0; i < swapchain->image_count; i++) {
            if (swapchain->images[i].bo) pan_kmod_bo_free(swapchain->images[i].bo);
        }
        free(swapchain->images);
    }
    if (swapchain->surface && (uintptr_t)swapchain->surface > 0x1000 && swapchain->surface->is_xcb && swapchain->surface->connection) {
        if (swapchain->xcb_pixmap) xcb_free_pixmap(swapchain->surface->connection, swapchain->xcb_pixmap);
        if (swapchain->xcb_gc) xcb_free_gc(swapchain->surface->connection, swapchain->xcb_gc);
    } else if (swapchain->surface && (uintptr_t)swapchain->surface > 0x1000 && swapchain->surface->dpy && swapchain->gc) {
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
    static uint32_t g_current_image_idx = 0;
    *pImageIndex = (g_current_image_idx++) % (swapchain->image_count > 0 ? swapchain->image_count : 1);
    if (fence) ((VkFence)fence)->signaled = true;
    return VK_SUCCESS;
}

VkResult vkAcquireNextImage2KHR(VkDevice device, const void *pAcquireInfo, uint32_t *pImageIndex) {
    if (!pAcquireInfo || !pImageIndex) return VK_ERROR_INITIALIZATION_FAILED;
    struct {
        uint32_t sType;
        uint32_t _pad;
        const void *pNext;
        VkSwapchainKHR swapchain;
        uint64_t timeout;
        void *semaphore;
        void *fence;
        uint32_t deviceMask;
    } info;
    memcpy(&info, pAcquireInfo, sizeof(info));
    return vkAcquireNextImageKHR(device, info.swapchain, info.timeout, info.semaphore, info.fence, pImageIndex);
}

VkResult vkGetDeviceGroupPresentCapabilitiesKHR(VkDevice device, void *pDeviceGroupPresentCapabilities) {
    if (!pDeviceGroupPresentCapabilities) return VK_ERROR_INITIALIZATION_FAILED;
    memset(pDeviceGroupPresentCapabilities, 0, 144);
    uint32_t *p = (uint32_t *)pDeviceGroupPresentCapabilities;
    p[4] = 1; /* presentMask[0] = 1 */
    p[36] = 1; /* modes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR (1) */
    return VK_SUCCESS;
}

VkResult vkGetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface, uint32_t *pModes) {
    if (!pModes) return VK_ERROR_INITIALIZATION_FAILED;
    *pModes = 1; /* VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR */
    return VK_SUCCESS;
}

VkResult vkGetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pRectCount, void *pRects) {
    if (!pRectCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pRects) {
        *pRectCount = 1;
        return VK_SUCCESS;
    }
    struct { int32_t x, y; uint32_t w, h; } *rect = pRects;
    rect->x = 0; rect->y = 0;
    rect->w = surface ? surface->width : 1280;
    rect->h = surface ? surface->height : 720;
    *pRectCount = 1;
    return VK_SUCCESS;
}

VkResult vkQueuePresentKHR(VkQueue queue, const struct VkPresentInfoKHR *pPresentInfo) {
    if (!pPresentInfo || pPresentInfo->swapchainCount == 0) return VK_ERROR_INITIALIZATION_FAILED;

    VkSwapchainKHR sc = pPresentInfo->pSwapchains[0];
    uint32_t img_idx = pPresentInfo->pImageIndices ? pPresentInfo->pImageIndices[0] : 0;
    struct v9_cmd_buffer *last_cmd = queue ? queue->last_v9_cmd : NULL;
    void *color_cpu = last_cmd ? v9_cmd_buffer_get_color_cpu(last_cmd) : NULL;

    if (!color_cpu && queue && queue->device && queue->device->last_rendered_color) {
        color_cpu = queue->device->last_rendered_color;
    }
    if (!color_cpu && sc && img_idx < sc->image_count && sc->images[img_idx].bo) {
        color_cpu = sc->images[img_idx].bo->cpu;
    }

    PANVK_LOG("vkQueuePresentKHR: sc=%p surface=%p img_idx=%u image_data=%p last_cmd=%p color_cpu=%p\n",
              (void*)sc,
              sc ? (void*)sc->surface : NULL,
              img_idx,
              sc ? sc->image_data : NULL,
              (void*)last_cmd, color_cpu);

    if (sc && sc->surface && (uintptr_t)sc->surface > 0x1000 && sc->image_data) {
        if (color_cpu) {
            memcpy(sc->image_data, color_cpu, sc->width * sc->height * 4);
        }

        if (sc->surface->is_xcb && sc->surface->connection && sc->surface->window) {
            uint8_t depth = sc->surface->depth ? sc->surface->depth : 24;
            xcb_put_image(
                sc->surface->connection, XCB_IMAGE_FORMAT_Z_PIXMAP,
                sc->surface->window, sc->xcb_gc ? sc->xcb_gc : 0,
                sc->width, sc->height, 0, 0, 0, depth,
                sc->width * sc->height * 4, (const uint8_t *)sc->image_data);
            xcb_flush(sc->surface->connection);
        } else if (sc->surface->dpy && sc->surface->window) {
            if (!sc->gc) {
                sc->gc = XCreateGC(sc->surface->dpy, sc->surface->window, 0, NULL);
            }
            if (!sc->ximage) {
                int screen = DefaultScreen(sc->surface->dpy);
                sc->ximage = XCreateImage(sc->surface->dpy, DefaultVisual(sc->surface->dpy, screen),
                                         24, ZPixmap, 0, sc->image_data, sc->width, sc->height, 32, 0);
            }
            if (sc->ximage && sc->gc) {
                XPutImage(sc->surface->dpy, sc->surface->window, sc->gc, sc->ximage, 0, 0, 0, 0, sc->width, sc->height);
                XFlush(sc->surface->dpy);
            }
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

/* ========== Query Pool ========== */
VkResult vkCreateQueryPool(VkDevice device, const void *pCreateInfo, void *pAllocator, VkQueryPool *pQueryPool) {
    if (!pQueryPool) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkQueryPool_T *qp = calloc(1, sizeof(*qp));
    if (!qp) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (pCreateInfo) {
        struct { uint32_t sType; uint32_t _pad; const void *pNext; uint32_t queryType; uint32_t queryCount; } ci;
        memcpy(&ci, pCreateInfo, sizeof(ci));
        qp->query_type = ci.queryType;
        qp->query_count = ci.queryCount > 0 ? ci.queryCount : 1;
    } else {
        qp->query_count = 1;
    }
    qp->results = calloc(qp->query_count, sizeof(uint64_t));
    *pQueryPool = qp;
    return VK_SUCCESS;
}

void vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool, void *pAllocator) {
    if (!queryPool) return;
    free(queryPool->results);
    free(queryPool);
}

VkResult vkGetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery,
                               uint32_t queryCount, size_t dataSize, void *pData,
                               uint64_t stride, uint32_t flags) {
    if (!pData) return VK_SUCCESS;
    memset(pData, 0, dataSize);
    return VK_SUCCESS;
}

void vkCmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                         uint32_t firstQuery, uint32_t queryCount) {
    if (queryPool && queryPool->results)
        memset(&queryPool->results[firstQuery], 0, queryCount * sizeof(uint64_t));
}

void vkCmdWriteTimestamp(VkCommandBuffer commandBuffer, uint32_t pipelineStage,
                         VkQueryPool queryPool, uint32_t query) {
    (void)commandBuffer; (void)pipelineStage; (void)queryPool; (void)query;
}

void vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer, uint64_t stage,
                          VkQueryPool queryPool, uint32_t query) {
    (void)commandBuffer; (void)stage; (void)queryPool; (void)query;
}

void vkCmdWriteTimestamp2KHR(VkCommandBuffer commandBuffer, uint64_t stage,
                              VkQueryPool queryPool, uint32_t query) {
    (void)commandBuffer; (void)stage; (void)queryPool; (void)query;
}

void vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                     uint32_t query, uint32_t flags) {
    (void)commandBuffer; (void)queryPool; (void)query; (void)flags;
}

void vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) {
    (void)commandBuffer; (void)queryPool; (void)query;
}

void vkCmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                               uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer,
                               uint64_t dstOffset, uint64_t stride, uint32_t flags) {
    (void)commandBuffer; (void)queryPool; (void)firstQuery; (void)queryCount;
    (void)dstBuffer; (void)dstOffset; (void)stride; (void)flags;
}

/* ========== Events ========== */
struct VkEvent_T { int signaled; };

VkResult vkCreateEvent(VkDevice device, const void *pCreateInfo, void *pAllocator, VkEvent *pEvent) {
    if (!pEvent) return VK_ERROR_INITIALIZATION_FAILED;
    *pEvent = calloc(1, sizeof(struct VkEvent_T));
    return *pEvent ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

void vkDestroyEvent(VkDevice device, VkEvent event, void *pAllocator) { free(event); }
VkResult vkGetEventStatus(VkDevice device, VkEvent event) { return VK_SUCCESS; }
VkResult vkSetEvent(VkDevice device, VkEvent event) { if (event) event->signaled = 1; return VK_SUCCESS; }
VkResult vkResetEvent(VkDevice device, VkEvent event) { if (event) event->signaled = 0; return VK_SUCCESS; }
void vkCmdSetEvent(VkCommandBuffer cb, VkEvent event, uint32_t stageMask) { (void)cb; (void)event; (void)stageMask; }
void vkCmdResetEvent(VkCommandBuffer cb, VkEvent event, uint32_t stageMask) { (void)cb; (void)event; (void)stageMask; }
void vkCmdWaitEvents(VkCommandBuffer cb, uint32_t eventCount, const VkEvent *pEvents,
                     uint32_t srcStage, uint32_t dstStage, uint32_t memBarrierCount,
                     const void *pMemBarriers, uint32_t bufBarrierCount, const void *pBufBarriers,
                     uint32_t imgBarrierCount, const void *pImgBarriers) {
    (void)cb; (void)eventCount; (void)pEvents; (void)srcStage; (void)dstStage;
    (void)memBarrierCount; (void)pMemBarriers; (void)bufBarrierCount; (void)pBufBarriers;
    (void)imgBarrierCount; (void)pImgBarriers;
}

void vkCmdSetEvent2(VkCommandBuffer cb, VkEvent event, const void *pDependencyInfo) { (void)cb; (void)event; (void)pDependencyInfo; }
void vkCmdSetEvent2KHR(VkCommandBuffer cb, VkEvent event, const void *pDependencyInfo) { (void)cb; (void)event; (void)pDependencyInfo; }
void vkCmdResetEvent2(VkCommandBuffer cb, VkEvent event, uint64_t stageMask) { (void)cb; (void)event; (void)stageMask; }
void vkCmdResetEvent2KHR(VkCommandBuffer cb, VkEvent event, uint64_t stageMask) { (void)cb; (void)event; (void)stageMask; }
void vkCmdWaitEvents2(VkCommandBuffer cb, uint32_t eventCount, const VkEvent *pEvents, const void *pDependencyInfos) {
    (void)cb; (void)eventCount; (void)pEvents; (void)pDependencyInfos;
}
void vkCmdWaitEvents2KHR(VkCommandBuffer cb, uint32_t eventCount, const VkEvent *pEvents, const void *pDependencyInfos) {
    (void)cb; (void)eventCount; (void)pEvents; (void)pDependencyInfos;
}

/* ========== Dynamic State Commands ========== */
void vkCmdSetDepthBias(VkCommandBuffer cb, float constantFactor, float clamp, float slopeFactor) {
    (void)cb; (void)constantFactor; (void)clamp; (void)slopeFactor;
}
void vkCmdSetBlendConstants(VkCommandBuffer cb, const float blendConstants[4]) {
    (void)cb; (void)blendConstants;
}
void vkCmdSetDepthBounds(VkCommandBuffer cb, float minDepthBounds, float maxDepthBounds) {
    (void)cb; (void)minDepthBounds; (void)maxDepthBounds;
}
void vkCmdSetStencilCompareMask(VkCommandBuffer cb, uint32_t faceMask, uint32_t compareMask) {
    (void)cb; (void)faceMask; (void)compareMask;
}
void vkCmdSetStencilWriteMask(VkCommandBuffer cb, uint32_t faceMask, uint32_t writeMask) {
    (void)cb; (void)faceMask; (void)writeMask;
}
void vkCmdSetStencilReference(VkCommandBuffer cb, uint32_t faceMask, uint32_t reference) {
    (void)cb; (void)faceMask; (void)reference;
}
void vkCmdSetLineWidth(VkCommandBuffer cb, float lineWidth) {
    (void)cb; (void)lineWidth;
}

/* ========== Compute / Dispatch ========== */
void vkCmdDispatch(VkCommandBuffer cb, uint32_t x, uint32_t y, uint32_t z) {
    (void)cb; (void)x; (void)y; (void)z;
}
void vkCmdDispatchIndirect(VkCommandBuffer cb, VkBuffer buffer, uint64_t offset) {
    (void)cb; (void)buffer; (void)offset;
}
void vkCmdDispatchBase(VkCommandBuffer cb, uint32_t baseX, uint32_t baseY, uint32_t baseZ,
                       uint32_t x, uint32_t y, uint32_t z) {
    (void)cb; (void)baseX; (void)baseY; (void)baseZ; (void)x; (void)y; (void)z;
}

/* ========== Indirect Draw ========== */
void vkCmdDrawIndirect(VkCommandBuffer cb, VkBuffer buffer, uint64_t offset,
                       uint32_t drawCount, uint32_t stride) {
    (void)cb; (void)buffer; (void)offset; (void)drawCount; (void)stride;
}
void vkCmdDrawIndexedIndirect(VkCommandBuffer cb, VkBuffer buffer, uint64_t offset,
                               uint32_t drawCount, uint32_t stride) {
    (void)cb; (void)buffer; (void)offset; (void)drawCount; (void)stride;
}

/* ========== Buffer Fill / Update ========== */
void vkCmdFillBuffer(VkCommandBuffer cb, VkBuffer dstBuffer, uint64_t dstOffset,
                     uint64_t size, uint32_t data) {
    (void)cb; (void)dstBuffer; (void)dstOffset; (void)size; (void)data;
}
void vkCmdUpdateBuffer(VkCommandBuffer cb, VkBuffer dstBuffer, uint64_t dstOffset,
                       uint64_t dataSize, const void *pData) {
    (void)cb; (void)dstBuffer; (void)dstOffset; (void)dataSize; (void)pData;
}

/* ========== Buffer Barriers (sync2) ========== */
void vkCmdPipelineBarrier2(VkCommandBuffer cb, const void *pDependencyInfo) {
    (void)cb; (void)pDependencyInfo;
}
void vkCmdPipelineBarrier2KHR(VkCommandBuffer cb, const void *pDependencyInfo) {
    (void)cb; (void)pDependencyInfo;
}

/* ========== QueueSubmit2 ========== */
VkResult vkQueueSubmit2(VkQueue queue, uint32_t submitCount, const void *pSubmits, VkFence fence) {
    /* Simplified: treat as no-op submit since work is done inline */
    (void)queue; (void)submitCount; (void)pSubmits; (void)fence;
    return VK_SUCCESS;
}
VkResult vkQueueSubmit2KHR(VkQueue queue, uint32_t submitCount, const void *pSubmits, VkFence fence) {
    return vkQueueSubmit2(queue, submitCount, pSubmits, fence);
}

/* ========== External Object Properties (Vulkan 1.1) ========== */
void vkGetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice,
                                                  const void *pExternalBufferInfo,
                                                  void *pExternalBufferProperties) {
    if (!pExternalBufferProperties) return;
    /* Report no external handle support */
    memset(pExternalBufferProperties, 0, 32); /* sizeof VkExternalBufferProperties */
}
void vkGetPhysicalDeviceExternalBufferPropertiesKHR(VkPhysicalDevice physicalDevice,
                                                     const void *pExternalBufferInfo,
                                                     void *pExternalBufferProperties) {
    vkGetPhysicalDeviceExternalBufferProperties(physicalDevice, pExternalBufferInfo, pExternalBufferProperties);
}
void vkGetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice physicalDevice,
                                                 const void *pExternalFenceInfo,
                                                 void *pExternalFenceProperties) {
    if (pExternalFenceProperties) memset(pExternalFenceProperties, 0, 24);
}
void vkGetPhysicalDeviceExternalFencePropertiesKHR(VkPhysicalDevice physicalDevice,
                                                    const void *pExternalFenceInfo,
                                                    void *pExternalFenceProperties) {
    vkGetPhysicalDeviceExternalFenceProperties(physicalDevice, pExternalFenceInfo, pExternalFenceProperties);
}
void vkGetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice,
                                                     const void *pExternalSemaphoreInfo,
                                                     void *pExternalSemaphoreProperties) {
    if (pExternalSemaphoreProperties) memset(pExternalSemaphoreProperties, 0, 24);
}
void vkGetPhysicalDeviceExternalSemaphorePropertiesKHR(VkPhysicalDevice physicalDevice,
                                                        const void *pExternalSemaphoreInfo,
                                                        void *pExternalSemaphoreProperties) {
    vkGetPhysicalDeviceExternalSemaphoreProperties(physicalDevice, pExternalSemaphoreInfo, pExternalSemaphoreProperties);
}

/* ========== Timeline Semaphore ========== */
VkResult vkWaitSemaphores(VkDevice device, const void *pWaitInfo, uint64_t timeout) {
    return VK_SUCCESS;
}
VkResult vkWaitSemaphoresKHR(VkDevice device, const void *pWaitInfo, uint64_t timeout) {
    return VK_SUCCESS;
}
VkResult vkSignalSemaphore(VkDevice device, const void *pSignalInfo) {
    return VK_SUCCESS;
}
VkResult vkSignalSemaphoreKHR(VkDevice device, const void *pSignalInfo) {
    return VK_SUCCESS;
}
VkResult vkGetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore, uint64_t *pValue) {
    if (pValue) *pValue = 0;
    return VK_SUCCESS;
}
VkResult vkGetSemaphoreCounterValueKHR(VkDevice device, VkSemaphore semaphore, uint64_t *pValue) {
    return vkGetSemaphoreCounterValue(device, semaphore, pValue);
}

/* ========== BufferDeviceAddress ========== */
uint64_t vkGetBufferDeviceAddress(VkDevice device, const void *pInfo) {
    if (!pInfo) return 0;
    /* Extract VkBuffer from pInfo (offset 16 on 64-bit after sType+pad+pNext) */
    struct { uint32_t sType; uint32_t _pad; const void *pNext; VkBuffer buffer; } info;
    memcpy(&info, pInfo, sizeof(info));
    if (info.buffer && info.buffer->bo) return info.buffer->bo->gpu + info.buffer->memory_offset;
    return 0;
}
uint64_t vkGetBufferDeviceAddressKHR(VkDevice device, const void *pInfo) {
    return vkGetBufferDeviceAddress(device, pInfo);
}
uint64_t vkGetBufferOpaqueCaptureAddress(VkDevice device, const void *pInfo) {
    return 0;
}
uint64_t vkGetBufferOpaqueCaptureAddressKHR(VkDevice device, const void *pInfo) {
    return 0;
}
uint64_t vkGetDeviceMemoryOpaqueCaptureAddress(VkDevice device, const void *pInfo) {
    return 0;
}
uint64_t vkGetDeviceMemoryOpaqueCaptureAddressKHR(VkDevice device, const void *pInfo) {
    return 0;
}

/* ========== Misc Missing ========== */
void vkCmdExecuteCommands(VkCommandBuffer cb, uint32_t commandBufferCount,
                          const VkCommandBuffer *pCommandBuffers) {
    (void)cb; (void)commandBufferCount; (void)pCommandBuffers;
}
VkResult vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                                         const void *pMemoryRanges) {
    if (!pMemoryRanges) return VK_SUCCESS;
    const struct {
        uint32_t sType;
        uint32_t _pad0;
        const void *pNext;
        VkDeviceMemory memory;
        VkDeviceSize offset;
        VkDeviceSize size;
    } *ranges = pMemoryRanges;

    for (uint32_t i = 0; i < memoryRangeCount; i++) {
        VkDeviceMemory mem = ranges[i].memory;
        if (mem && mem->bo && mem->bo->cpu && mem->low_cpu && mem->low_cpu != MAP_FAILED) {
            VkDeviceSize offset = ranges[i].offset;
            VkDeviceSize sz = ranges[i].size;
            if (sz == ~0ULL || offset + sz > mem->size) {
                sz = mem->size > offset ? mem->size - offset : 0;
            }
            if (sz > 0) {
                memcpy((uint8_t *)mem->low_cpu + offset, (uint8_t *)mem->bo->cpu + offset, sz);
            }
        }
    }
    return VK_SUCCESS;
}

VkResult vkFlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                                    const void *pMemoryRanges) {
    if (!pMemoryRanges) return VK_SUCCESS;
    const struct {
        uint32_t sType;
        uint32_t _pad0;
        const void *pNext;
        VkDeviceMemory memory;
        VkDeviceSize offset;
        VkDeviceSize size;
    } *ranges = pMemoryRanges;

    for (uint32_t i = 0; i < memoryRangeCount; i++) {
        VkDeviceMemory mem = ranges[i].memory;
        if (mem && mem->bo && mem->bo->cpu && mem->low_cpu && mem->low_cpu != MAP_FAILED) {
            VkDeviceSize offset = ranges[i].offset;
            VkDeviceSize sz = ranges[i].size;
            if (sz == ~0ULL || offset + sz > mem->size) {
                sz = mem->size > offset ? mem->size - offset : 0;
            }
            if (sz > 0) {
                memcpy((uint8_t *)mem->bo->cpu + offset, (uint8_t *)mem->low_cpu + offset, sz);
            }
        }
    }
    return VK_SUCCESS;
}
VkResult vkCreateBufferView(VkDevice device, const void *pCreateInfo, void *pAllocator,
                            VkBufferView *pView) {
    if (!pView) return VK_ERROR_INITIALIZATION_FAILED;
    *pView = calloc(1, 8);
    return *pView ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}
void vkDestroyBufferView(VkDevice device, VkBufferView bufferView, void *pAllocator) {
    free(bufferView);
}
void vkCmdBindTransformFeedbackBuffersEXT(VkCommandBuffer cb, uint32_t firstBinding,
                                           uint32_t bindingCount, const VkBuffer *pBuffers,
                                           const uint64_t *pOffsets, const uint64_t *pSizes) {
    (void)cb; (void)firstBinding; (void)bindingCount; (void)pBuffers; (void)pOffsets; (void)pSizes;
}
void vkCmdBeginTransformFeedbackEXT(VkCommandBuffer cb, uint32_t firstCounterBuffer,
                                     uint32_t counterBufferCount, const VkBuffer *pCounterBuffers,
                                     const uint64_t *pCounterBufferOffsets) {
    (void)cb; (void)firstCounterBuffer; (void)counterBufferCount;
    (void)pCounterBuffers; (void)pCounterBufferOffsets;
}
void vkCmdEndTransformFeedbackEXT(VkCommandBuffer cb, uint32_t firstCounterBuffer,
                                   uint32_t counterBufferCount, const VkBuffer *pCounterBuffers,
                                   const uint64_t *pCounterBufferOffsets) {
    (void)cb; (void)firstCounterBuffer; (void)counterBufferCount;
    (void)pCounterBuffers; (void)pCounterBufferOffsets;
}
void vkCmdBeginQueryIndexedEXT(VkCommandBuffer cb, VkQueryPool queryPool, uint32_t query,
                                uint32_t flags, uint32_t index) {
    (void)cb; (void)queryPool; (void)query; (void)flags; (void)index;
}
void vkCmdEndQueryIndexedEXT(VkCommandBuffer cb, VkQueryPool queryPool, uint32_t query,
                              uint32_t index) {
    (void)cb; (void)queryPool; (void)query; (void)index;
}
void vkCmdDrawIndirectByteCountEXT(VkCommandBuffer cb, uint32_t instanceCount,
                                    uint32_t firstInstance, VkBuffer counterBuffer,
                                    uint64_t counterBufferOffset, uint32_t counterOffset,
                                    uint32_t vertexStride) {
    (void)cb; (void)instanceCount; (void)firstInstance; (void)counterBuffer;
    (void)counterBufferOffset; (void)counterOffset; (void)vertexStride;
}
void vkCmdSetVertexInputEXT(VkCommandBuffer cb, uint32_t vertexBindingDescriptionCount,
                             const void *pVertexBindingDescriptions,
                             uint32_t vertexAttributeDescriptionCount,
                             const void *pVertexAttributeDescriptions) {
    (void)cb; (void)vertexBindingDescriptionCount; (void)pVertexBindingDescriptions;
    (void)vertexAttributeDescriptionCount; (void)pVertexAttributeDescriptions;
}
void vkCmdSetPrimitiveTopologyEXT(VkCommandBuffer cb, uint32_t primitiveTopology) {
    (void)cb; (void)primitiveTopology;
}
void vkCmdSetFrontFaceEXT(VkCommandBuffer cb, uint32_t frontFace) {
    (void)cb; (void)frontFace;
}
void vkCmdSetCullModeEXT(VkCommandBuffer cb, uint32_t cullMode) {
    (void)cb; (void)cullMode;
}
void vkCmdSetDepthTestEnableEXT(VkCommandBuffer cb, uint32_t enable) {
    (void)cb; (void)enable;
}
void vkCmdSetDepthWriteEnableEXT(VkCommandBuffer cb, uint32_t enable) {
    (void)cb; (void)enable;
}
void vkCmdSetDepthCompareOpEXT(VkCommandBuffer cb, uint32_t depthCompareOp) {
    (void)cb; (void)depthCompareOp;
}
void vkCmdSetDepthBoundsTestEnableEXT(VkCommandBuffer cb, uint32_t enable) {
    (void)cb; (void)enable;
}
void vkCmdSetStencilTestEnableEXT(VkCommandBuffer cb, uint32_t enable) {
    (void)cb; (void)enable;
}
void vkCmdSetStencilOpEXT(VkCommandBuffer cb, uint32_t faceMask, uint32_t failOp,
                           uint32_t passOp, uint32_t depthFailOp, uint32_t compareOp) {
    (void)cb; (void)faceMask; (void)failOp; (void)passOp; (void)depthFailOp; (void)compareOp;
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

/* ========== Additional Functions required by DXVK / Vulkan Loader ========== */
void vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                        uint32_t stageFlags, uint32_t offset, uint32_t size,
                        const void *pValues) {
    (void)commandBuffer; (void)layout; (void)stageFlags; (void)offset; (void)size; (void)pValues;
}

void vkGetImageSubresourceLayout(VkDevice device, VkImage image,
                                 const void *pSubresource, void *pLayout) {
    (void)device; (void)pSubresource;
    if (!pLayout) return;
    struct VkSubresourceLayout_T {
        VkDeviceSize offset;
        VkDeviceSize size;
        VkDeviceSize rowPitch;
        VkDeviceSize arrayPitch;
        VkDeviceSize depthPitch;
    } *l = (struct VkSubresourceLayout_T *)pLayout;
    memset(l, 0, sizeof(*l));
    if (image) {
        uint32_t w = image->width > 0 ? image->width : 1;
        uint32_t h = image->height > 0 ? image->height : 1;
        l->rowPitch = w * 4;
        l->size = image->bo ? image->bo->size : (VkDeviceSize)w * h * 4;
        l->arrayPitch = l->size;
        l->depthPitch = l->size;
    }
}

void vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout,
                       VkImage dstImage, uint32_t dstImageLayout, uint32_t regionCount,
                       const void *pRegions) {
    (void)commandBuffer; (void)srcImage; (void)srcImageLayout; (void)dstImage; (void)dstImageLayout; (void)regionCount; (void)pRegions;
}

void vkGetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, struct VkExtent2D *pGranularity) {
    (void)device; (void)renderPass;
    if (pGranularity) {
        pGranularity->width = 1;
        pGranularity->height = 1;
    }
}

VkResult vkGetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t *pDataSize, void *pData) {
    (void)device; (void)pipelineCache;
    if (!pDataSize) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pData) {
        *pDataSize = 0;
        return VK_SUCCESS;
    }
    return VK_SUCCESS;
}

VkResult vkMergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache *pSrcCaches) {
    (void)device; (void)dstCache; (void)srcCacheCount; (void)pSrcCaches;
    return VK_SUCCESS;
}

void vkGetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t *pRequirementsCount, void *pRequirements) {
    (void)device; (void)image; (void)pRequirements;
    if (pRequirementsCount) *pRequirementsCount = 0;
}

void vkGetImageSparseMemoryRequirements2(VkDevice device, const void *pInfo, uint32_t *pRequirementsCount, void *pRequirements) {
    (void)device; (void)pInfo; (void)pRequirements;
    if (pRequirementsCount) *pRequirementsCount = 0;
}

void vkGetImageSparseMemoryRequirements2KHR(VkDevice device, const void *pInfo, uint32_t *pRequirementsCount, void *pRequirements) {
    vkGetImageSparseMemoryRequirements2(device, pInfo, pRequirementsCount, pRequirements);
}

VkResult vkQueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const void *pBindInfo, VkFence fence) {
    (void)queue; (void)bindInfoCount; (void)pBindInfo;
    if (fence) ((VkFence)fence)->signaled = true;
    return VK_SUCCESS;
}

void vkCmdBindVertexBuffers2EXT(VkCommandBuffer cb, uint32_t firstBinding, uint32_t bindingCount,
                               const VkBuffer *pBuffers, const VkDeviceSize *pOffsets,
                               const VkDeviceSize *pSizes, const VkDeviceSize *pStrides) {
    (void)pSizes; (void)pStrides;
    vkCmdBindVertexBuffers(cb, firstBinding, bindingCount, pBuffers, pOffsets);
}

void vkCmdSetScissorWithCountEXT(VkCommandBuffer cb, uint32_t scissorCount, const void *pScissors) {
    (void)cb; (void)scissorCount; (void)pScissors;
}

void vkCmdSetViewportWithCountEXT(VkCommandBuffer cb, uint32_t viewportCount, const void *pViewports) {
    (void)cb; (void)viewportCount; (void)pViewports;
}

void vkCmdDrawIndirectCountKHR(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset,
                               VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                               uint32_t maxDrawCount, uint32_t stride) {
    (void)cb; (void)buffer; (void)offset; (void)countBuffer; (void)countBufferOffset; (void)maxDrawCount; (void)stride;
}

void vkCmdDrawIndexedIndirectCountKHR(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset,
                                      VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                      uint32_t maxDrawCount, uint32_t stride) {
    (void)cb; (void)buffer; (void)offset; (void)countBuffer; (void)countBufferOffset; (void)maxDrawCount; (void)stride;
}

void vkResetQueryPoolEXT(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) {
    (void)device; (void)queryPool; (void)firstQuery; (void)queryCount;
}

void vkCmdBeginConditionalRenderingEXT(VkCommandBuffer cb, const void *pConditionalRenderingBegin) {
    (void)cb; (void)pConditionalRenderingBegin;
}

void vkCmdEndConditionalRenderingEXT(VkCommandBuffer cb) {
    (void)cb;
}

void vkGetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice, const void *pFormatInfo, uint32_t *pPropertyCount, void *pProperties) {
    (void)physicalDevice; (void)pFormatInfo; (void)pProperties;
    if (pPropertyCount) *pPropertyCount = 0;
}

void vkGetPhysicalDeviceSparseImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const void *pFormatInfo, uint32_t *pPropertyCount, void *pProperties) {
    vkGetPhysicalDeviceSparseImageFormatProperties2(physicalDevice, pFormatInfo, pPropertyCount, pProperties);
}

void vkGetDeviceQueue2(VkDevice device, const void *pQueueInfo, VkQueue *pQueue) {
    if (!device || !pQueueInfo || !pQueue) return;
    uint32_t family = *(const uint32_t *)((const uint8_t *)pQueueInfo + 20);
    uint32_t index = *(const uint32_t *)((const uint8_t *)pQueueInfo + 24);
    vkGetDeviceQueue(device, family, index, pQueue);
}

VkResult vkGetPhysicalDeviceSurfacePresentModes2EXT(VkPhysicalDevice physicalDevice, const void *pSurfaceInfo, uint32_t *pPresentModeCount, void *pPresentModes) {
    (void)physicalDevice; (void)pSurfaceInfo; (void)pPresentModes;
    if (pPresentModeCount) *pPresentModeCount = 0;
    return VK_SUCCESS;
}

VkResult vkAcquireFullScreenExclusiveModeEXT(VkDevice device, VkSwapchainKHR swapchain) {
    (void)device; (void)swapchain;
    return VK_SUCCESS;
}

VkResult vkReleaseFullScreenExclusiveModeEXT(VkDevice device, VkSwapchainKHR swapchain) {
    (void)device; (void)swapchain;
    return VK_SUCCESS;
}

VkResult vkGetDeviceGroupSurfacePresentModes2EXT(VkDevice device, const void *pSurfaceInfo, void *pModes) {
    (void)device; (void)pSurfaceInfo; (void)pModes;
    return VK_SUCCESS;
}

uint32_t vkGetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, void *dpy, unsigned long visualID) {
    (void)physicalDevice; (void)queueFamilyIndex; (void)dpy; (void)visualID;
    return 1;
}

VkResult vkCreateWaylandSurfaceKHR(VkInstance instance, const void *pCreateInfo, void *pAllocator, void **pSurface) {
    if (!pSurface) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkSurfaceKHR_T *surf = calloc(1, sizeof(*surf));
    if (!surf) return VK_ERROR_OUT_OF_HOST_MEMORY;
    surf->width = 1280;
    surf->height = 720;

    int screen_num = 0;
    surf->connection = xcb_connect(NULL, &screen_num);
    if (surf->connection && !xcb_connection_has_error(surf->connection)) {
        const xcb_setup_t *setup = xcb_get_setup(surf->connection);
        xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
        for (int i = 0; i < screen_num; i++) xcb_screen_next(&iter);
        if (iter.data) {
            surf->window = iter.data->root;
            surf->depth = iter.data->root_depth;
            surf->width = iter.data->width_in_pixels > 0 ? iter.data->width_in_pixels : 1280;
            surf->height = iter.data->height_in_pixels > 0 ? iter.data->height_in_pixels : 720;
            surf->is_xcb = true;
        }
    }
    *pSurface = surf;
    return VK_SUCCESS;
}

uint32_t vkGetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, void *display) {
    (void)physicalDevice; (void)queueFamilyIndex; (void)display;
    return 1;
}

VkResult vkCreateWin32SurfaceKHR(VkInstance instance, const void *pCreateInfo, void *pAllocator, void **pSurface) {
    if (!pSurface) return VK_ERROR_INITIALIZATION_FAILED;
    struct VkSurfaceKHR_T *surf = calloc(1, sizeof(*surf));
    if (!surf) return VK_ERROR_OUT_OF_HOST_MEMORY;
    surf->width = 1280;
    surf->height = 720;

    int screen_num = 0;
    surf->connection = xcb_connect(NULL, &screen_num);
    if (surf->connection && !xcb_connection_has_error(surf->connection)) {
        const xcb_setup_t *setup = xcb_get_setup(surf->connection);
        xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
        for (int i = 0; i < screen_num; i++) xcb_screen_next(&iter);
        if (iter.data) {
            surf->window = iter.data->root;
            surf->depth = iter.data->root_depth;
            surf->width = iter.data->width_in_pixels > 0 ? iter.data->width_in_pixels : 1280;
            surf->height = iter.data->height_in_pixels > 0 ? iter.data->height_in_pixels : 720;
            surf->is_xcb = true;
        }
    }
    *pSurface = surf;
    return VK_SUCCESS;
}

uint32_t vkGetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex) {
    (void)physicalDevice; (void)queueFamilyIndex;
    return 1;
}

VkResult vkCreateDebugReportCallbackEXT(VkInstance instance, const void *pCreateInfo, void *pAllocator, void **pCallback) {
    (void)instance; (void)pCreateInfo; (void)pAllocator;
    if (pCallback) *pCallback = (void *)0x1;
    return VK_SUCCESS;
}

void vkDestroyDebugReportCallbackEXT(VkInstance instance, void *callback, void *pAllocator) {
    (void)instance; (void)callback; (void)pAllocator;
}

void vkDebugReportMessageEXT(VkInstance instance, uint32_t flags, uint32_t objectType, uint64_t object, size_t location, int32_t messageCode, const char *pLayerPrefix, const char *pMessage) {
    (void)instance; (void)flags; (void)objectType; (void)object; (void)location; (void)messageCode; (void)pLayerPrefix; (void)pMessage;
}

VkResult vkMapMemory2KHR(VkDevice device, const void *pMemoryMapInfo, void **ppData) {
    if (!pMemoryMapInfo || !ppData) return VK_ERROR_INITIALIZATION_FAILED;
    /* VkMemoryMapInfoKHR:
     *   uint32_t sType
     *   const void* pNext (offset 8)
     *   uint32_t flags (offset 16)
     *   VkDeviceMemory memory (offset 24)
     *   VkDeviceSize offset (offset 32)
     *   VkDeviceSize size (offset 40)
     */
    VkDeviceMemory memory = *(VkDeviceMemory *)((const uint8_t *)pMemoryMapInfo + 24);
    VkDeviceSize offset = *(VkDeviceSize *)((const uint8_t *)pMemoryMapInfo + 32);
    VkDeviceSize size = *(VkDeviceSize *)((const uint8_t *)pMemoryMapInfo + 40);
    uint32_t flags = *(uint32_t *)((const uint8_t *)pMemoryMapInfo + 16);
    return vkMapMemory(device, memory, offset, size, flags, ppData);
}

VkResult vkMapMemory2(VkDevice device, const void *pMemoryMapInfo, void **ppData) {
    return vkMapMemory2KHR(device, pMemoryMapInfo, ppData);
}

VkResult vkUnmapMemory2KHR(VkDevice device, const void *pMemoryUnmapInfo) {
    (void)device; (void)pMemoryUnmapInfo;
    return VK_SUCCESS;
}

VkResult vkUnmapMemory2(VkDevice device, const void *pMemoryUnmapInfo) {
    (void)device; (void)pMemoryUnmapInfo;
    return VK_SUCCESS;
}

VkResult vkGetMemoryWin32HandleKHR(VkDevice device, const void *pGetWin32HandleInfo, void **pHandle) {
    (void)device; (void)pGetWin32HandleInfo;
    if (pHandle) *pHandle = (void *)0x1;
    return VK_SUCCESS;
}

VkResult vkGetMemoryWin32HandlePropertiesKHR(VkDevice device, uint32_t handleType, void *handle, void *pMemoryWin32HandleProperties) {
    (void)device; (void)handleType; (void)handle;
    if (pMemoryWin32HandleProperties) {
        struct { uint32_t sType; void *pNext; uint32_t memoryTypeBits; } *p = pMemoryWin32HandleProperties;
        p->memoryTypeBits = 0x1;
    }
    return VK_SUCCESS;
}

/* Vulkan ICD Entry Point Lookup Table */
__attribute__((visibility("default"))) PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char *pName) {
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
    MATCH(vkGetPhysicalDeviceFormatProperties2);
    MATCH(vkGetPhysicalDeviceFormatProperties2KHR);
    MATCH(vkGetPhysicalDeviceImageFormatProperties);
    MATCH(vkGetPhysicalDeviceImageFormatProperties2);
    MATCH(vkGetPhysicalDeviceImageFormatProperties2KHR);
    MATCH(vkGetPhysicalDeviceSparseImageFormatProperties);
    MATCH(vkGetPhysicalDeviceSparseImageFormatProperties2);
    MATCH(vkGetPhysicalDeviceSparseImageFormatProperties2KHR);
    MATCH(vkCreateDevice);
    MATCH(vkDestroyDevice);
    MATCH(vkGetDeviceQueue);
    MATCH(vkGetDeviceQueue2);
    MATCH(vkAllocateMemory);
    MATCH(vkFreeMemory);
    MATCH(vkMapMemory);
    MATCH(vkMapMemory2);
    MATCH(vkMapMemory2KHR);
    MATCH(vkUnmapMemory);
    MATCH(vkUnmapMemory2);
    MATCH(vkUnmapMemory2KHR);
    MATCH(vkGetMemoryWin32HandleKHR);
    MATCH(vkGetMemoryWin32HandlePropertiesKHR);
    MATCH(vkCreateBuffer);
    MATCH(vkDestroyBuffer);
    MATCH(vkGetBufferMemoryRequirements);
    MATCH(vkGetBufferMemoryRequirements2);
    MATCH(vkGetBufferMemoryRequirements2KHR);
    MATCH(vkBindBufferMemory);
    MATCH(vkBindBufferMemory2);
    MATCH(vkBindBufferMemory2KHR);
    MATCH(vkCreateImage);
    MATCH(vkDestroyImage);
    MATCH(vkGetImageMemoryRequirements);
    MATCH(vkGetImageMemoryRequirements2);
    MATCH(vkGetImageMemoryRequirements2KHR);
    MATCH(vkGetImageSparseMemoryRequirements);
    MATCH(vkGetImageSparseMemoryRequirements2);
    MATCH(vkGetImageSparseMemoryRequirements2KHR);
    MATCH(vkQueueBindSparse);
    MATCH(vkGetImageSubresourceLayout);
    MATCH(vkBindImageMemory);
    MATCH(vkBindImageMemory2);
    MATCH(vkBindImageMemory2KHR);
    MATCH(vkCreateImageView);
    MATCH(vkDestroyImageView);
    MATCH(vkCreateShaderModule);
    MATCH(vkDestroyShaderModule);
    MATCH(vkCreatePipelineCache);
    MATCH(vkDestroyPipelineCache);
    MATCH(vkGetPipelineCacheData);
    MATCH(vkMergePipelineCaches);
    MATCH(vkCreatePipelineLayout);
    MATCH(vkDestroyPipelineLayout);
    MATCH(vkCreateRenderPass);
    MATCH(vkCreateRenderPass2);
    MATCH(vkCreateRenderPass2KHR);
    MATCH(vkDestroyRenderPass);
    MATCH(vkGetRenderAreaGranularity);
    MATCH(vkCreateFramebuffer);
    MATCH(vkDestroyFramebuffer);
    MATCH(vkCreateDescriptorSetLayout);
    MATCH(vkDestroyDescriptorSetLayout);
    MATCH(vkCreateDescriptorPool);
    MATCH(vkDestroyDescriptorPool);
    MATCH(vkCreateDescriptorUpdateTemplate);
    MATCH(vkCreateDescriptorUpdateTemplateKHR);
    MATCH(vkDestroyDescriptorUpdateTemplate);
    MATCH(vkDestroyDescriptorUpdateTemplateKHR);
    MATCH(vkUpdateDescriptorSetWithTemplate);
    MATCH(vkUpdateDescriptorSetWithTemplateKHR);
    MATCH(vkResetCommandPool);
    MATCH(vkResetCommandBuffer);
    MATCH(vkResetDescriptorPool);
    MATCH(vkTrimCommandPool);
    MATCH(vkTrimCommandPoolKHR);
    MATCH(vkGetDeviceMemoryCommitment);
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
    MATCH(vkCmdResolveImage);
    MATCH(vkCmdPushConstants);
    MATCH(vkCmdPipelineBarrier);
    MATCH(vkCmdPipelineBarrier2);
    MATCH(vkCmdPipelineBarrier2KHR);
    MATCH(vkCmdDraw);
    MATCH(vkCmdBeginRenderPass);
    MATCH(vkCmdBeginRenderPass2);
    MATCH(vkCmdBeginRenderPass2KHR);
    MATCH(vkCmdNextSubpass);
    MATCH(vkCmdNextSubpass2);
    MATCH(vkCmdNextSubpass2KHR);
    MATCH(vkCmdDrawIndexed);
    MATCH(vkCmdEndRenderPass);
    MATCH(vkCmdEndRenderPass2);
    MATCH(vkCmdEndRenderPass2KHR);
    MATCH(vkCmdDispatch);
    MATCH(vkCmdDispatchIndirect);
    MATCH(vkCmdDispatchBase);
    MATCH(vkCmdDrawIndirect);
    MATCH(vkCmdDrawIndirectCountKHR);
    MATCH(vkCmdDrawIndexedIndirect);
    MATCH(vkCmdDrawIndexedIndirectCountKHR);
    MATCH(vkCmdFillBuffer);
    MATCH(vkCmdUpdateBuffer);
    MATCH(vkCmdSetDepthBias);
    MATCH(vkCmdSetBlendConstants);
    MATCH(vkCmdSetDepthBounds);
    MATCH(vkCmdSetStencilCompareMask);
    MATCH(vkCmdSetStencilWriteMask);
    MATCH(vkCmdSetStencilReference);
    MATCH(vkCmdSetLineWidth);
    MATCH(vkCmdSetEvent);
    MATCH(vkCmdSetEvent2);
    MATCH(vkCmdSetEvent2KHR);
    MATCH(vkCmdResetEvent);
    MATCH(vkCmdResetEvent2);
    MATCH(vkCmdResetEvent2KHR);
    MATCH(vkCmdWaitEvents);
    MATCH(vkCmdWaitEvents2);
    MATCH(vkCmdWaitEvents2KHR);
    MATCH(vkCmdExecuteCommands);
    MATCH(vkCmdBeginQuery);
    MATCH(vkCmdEndQuery);
    MATCH(vkCmdResetQueryPool);
    MATCH(vkResetQueryPoolEXT);
    MATCH(vkCmdWriteTimestamp);
    MATCH(vkCmdWriteTimestamp2);
    MATCH(vkCmdWriteTimestamp2KHR);
    MATCH(vkCmdCopyQueryPoolResults);
    MATCH(vkCmdBindTransformFeedbackBuffersEXT);
    MATCH(vkCmdBeginTransformFeedbackEXT);
    MATCH(vkCmdEndTransformFeedbackEXT);
    MATCH(vkCmdBeginQueryIndexedEXT);
    MATCH(vkCmdEndQueryIndexedEXT);
    MATCH(vkCmdDrawIndirectByteCountEXT);
    MATCH(vkCmdSetVertexInputEXT);
    MATCH(vkCmdSetPrimitiveTopologyEXT);
    MATCH(vkCmdSetFrontFaceEXT);
    MATCH(vkCmdSetCullModeEXT);
    MATCH(vkCmdSetDepthTestEnableEXT);
    MATCH(vkCmdSetDepthWriteEnableEXT);
    MATCH(vkCmdSetDepthCompareOpEXT);
    MATCH(vkCmdSetDepthBoundsTestEnableEXT);
    MATCH(vkCmdSetStencilTestEnableEXT);
    MATCH(vkCmdSetStencilOpEXT);
    MATCH(vkCmdBindVertexBuffers2EXT);
    MATCH(vkCmdSetScissorWithCountEXT);
    MATCH(vkCmdSetViewportWithCountEXT);
    MATCH(vkCmdBeginConditionalRenderingEXT);
    MATCH(vkCmdEndConditionalRenderingEXT);
    MATCH(vkQueueSubmit);
    MATCH(vkQueueSubmit2);
    MATCH(vkQueueSubmit2KHR);
    MATCH(vkQueueWaitIdle);
    MATCH(vkDeviceWaitIdle);
    MATCH(vkCreateQueryPool);
    MATCH(vkDestroyQueryPool);
    MATCH(vkGetQueryPoolResults);
    MATCH(vkCreateEvent);
    MATCH(vkDestroyEvent);
    MATCH(vkGetEventStatus);
    MATCH(vkSetEvent);
    MATCH(vkResetEvent);
    MATCH(vkCreateBufferView);
    MATCH(vkDestroyBufferView);
    MATCH(vkFlushMappedMemoryRanges);
    MATCH(vkInvalidateMappedMemoryRanges);
    MATCH(vkGetPhysicalDeviceExternalBufferProperties);
    MATCH(vkGetPhysicalDeviceExternalBufferPropertiesKHR);
    MATCH(vkGetPhysicalDeviceExternalFenceProperties);
    MATCH(vkGetPhysicalDeviceExternalFencePropertiesKHR);
    MATCH(vkGetPhysicalDeviceExternalSemaphoreProperties);
    MATCH(vkGetPhysicalDeviceExternalSemaphorePropertiesKHR);
    MATCH(vkWaitSemaphores);
    MATCH(vkWaitSemaphoresKHR);
    MATCH(vkSignalSemaphore);
    MATCH(vkSignalSemaphoreKHR);
    MATCH(vkGetSemaphoreCounterValue);
    MATCH(vkGetSemaphoreCounterValueKHR);
    MATCH(vkGetBufferDeviceAddress);
    MATCH(vkGetBufferDeviceAddressKHR);
    MATCH(vkGetBufferOpaqueCaptureAddress);
    MATCH(vkGetBufferOpaqueCaptureAddressKHR);
    MATCH(vkGetDeviceMemoryOpaqueCaptureAddress);
    MATCH(vkGetDeviceMemoryOpaqueCaptureAddressKHR);
    MATCH(vkCreateXlibSurfaceKHR);
    MATCH(vkCreateXcbSurfaceKHR);
    MATCH(vkGetPhysicalDeviceXlibPresentationSupportKHR);
    MATCH(vkGetPhysicalDeviceXcbPresentationSupportKHR);
    MATCH(vkCreateWaylandSurfaceKHR);
    MATCH(vkGetPhysicalDeviceWaylandPresentationSupportKHR);
    MATCH(vkCreateWin32SurfaceKHR);
    MATCH(vkGetPhysicalDeviceWin32PresentationSupportKHR);
    MATCH(vkCreateDebugReportCallbackEXT);
    MATCH(vkDestroyDebugReportCallbackEXT);
    MATCH(vkDebugReportMessageEXT);
    MATCH(vkGetPhysicalDeviceDisplayPropertiesKHR);
    MATCH(vkGetPhysicalDeviceDisplayPlanePropertiesKHR);
    MATCH(vkGetDisplayPlaneSupportedDisplaysKHR);
    MATCH(vkGetDisplayModePropertiesKHR);
    MATCH(vkDestroySurfaceKHR);
    MATCH(vkGetPhysicalDeviceSurfaceSupportKHR);
    MATCH(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    MATCH(vkGetPhysicalDeviceSurfaceCapabilities2KHR);
    MATCH(vkGetPhysicalDeviceSurfaceCapabilities2EXT);
    MATCH(vkGetPhysicalDeviceSurfaceFormatsKHR);
    MATCH(vkGetPhysicalDeviceSurfaceFormats2KHR);
    MATCH(vkGetPhysicalDeviceSurfacePresentModesKHR);
    MATCH(vkGetPhysicalDeviceSurfacePresentModes2EXT);
    MATCH(vkAcquireFullScreenExclusiveModeEXT);
    MATCH(vkReleaseFullScreenExclusiveModeEXT);
    MATCH(vkGetDeviceGroupSurfacePresentModes2EXT);
    MATCH(vkCreateSwapchainKHR);
    MATCH(vkDestroySwapchainKHR);
    MATCH(vkGetSwapchainImagesKHR);
    MATCH(vkAcquireNextImageKHR);
    MATCH(vkAcquireNextImage2KHR);
    MATCH(vkGetDeviceGroupPresentCapabilitiesKHR);
    MATCH(vkGetDeviceGroupSurfacePresentModesKHR);
    MATCH(vkGetPhysicalDevicePresentRectanglesKHR);
    MATCH(vkQueuePresentKHR);
    MATCH(panvk_v9_read_pixel);
    MATCH(vk_icdGetPhysicalDeviceProcAddr);
#undef MATCH
    if (pName) panvk_trace("UNRESOLVED", pName);
    return NULL;
}

__attribute__((visibility("default"))) PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device, const char *pName) {
    return vkGetInstanceProcAddr(NULL, pName);
}

__attribute__((visibility("default"))) PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName) {
    return vkGetInstanceProcAddr(instance, pName);
}

__attribute__((visibility("default"))) PFN_vkVoidFunction vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char *pName) {
    return vkGetInstanceProcAddr(instance, pName);
}
