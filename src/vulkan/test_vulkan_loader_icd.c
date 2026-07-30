/*
 * Test harness for Step 4: Vulkan Loader ICD Dynamic Shared Library Integration
 * Dynamically loads libvulkan_panvk_v9.so via dlopen(), resolves entry points via vk_icdGetInstanceProcAddr, and executes full Vulkan pipeline rendering
 */

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "panvk_v9_entrypoints.h"

typedef PFN_vkVoidFunction (*PFN_vk_icdGetInstanceProcAddr)(VkInstance instance, const char *pName);

int main(int argc, char **argv) {
    printf("=== Testing Step 4: Vulkan Loader ICD Shared Library Integration ===\n");

    const char *so_path = "./libvulkan_panvk_v9.so";
    void *handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "FAIL: dlopen('%s') failed: %s\n", so_path, dlerror());
        return 1;
    }
    printf("SUCCESS: Dynamically loaded '%s' via dlopen()\n", so_path);

    PFN_vk_icdGetInstanceProcAddr gpa = (PFN_vk_icdGetInstanceProcAddr)dlsym(handle, "vk_icdGetInstanceProcAddr");
    if (!gpa) {
        fprintf(stderr, "FAIL: dlsym('vk_icdGetInstanceProcAddr') failed: %s\n", dlerror());
        dlclose(handle);
        return 1;
    }
    printf("SUCCESS: Resolved 'vk_icdGetInstanceProcAddr' entry point from ICD\n");

#define LOOKUP(type, name) type pfn_##name = (type)gpa(NULL, #name); \
    if (!pfn_##name) { fprintf(stderr, "FAIL: ICD missing proc address for '%s'\n", #name); dlclose(handle); return 1; }

    typedef VkResult (*PFN_vkCreateInstance)(const struct VkInstanceCreateInfo *, void *, VkInstance *);
    typedef void (*PFN_vkDestroyInstance)(VkInstance, void *);
    typedef VkResult (*PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t *, VkPhysicalDevice *);
    typedef void (*PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice, struct VkPhysicalDeviceProperties *);
    typedef VkResult (*PFN_vkCreateDevice)(VkPhysicalDevice, const struct VkDeviceCreateInfo *, void *, VkDevice *);
    typedef void (*PFN_vkDestroyDevice)(VkDevice, void *);
    typedef void (*PFN_vkGetDeviceQueue)(VkDevice, uint32_t, uint32_t, VkQueue *);
    typedef VkResult (*PFN_vkCreateCommandPool)(VkDevice, const struct VkCommandPoolCreateInfo *, void *, VkCommandPool *);
    typedef void (*PFN_vkDestroyCommandPool)(VkDevice, VkCommandPool, void *);
    typedef VkResult (*PFN_vkAllocateCommandBuffers)(VkDevice, const struct VkCommandBufferAllocateInfo *, VkCommandBuffer *);
    typedef void (*PFN_vkFreeCommandBuffers)(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer *);
    typedef VkResult (*PFN_vkBeginCommandBuffer)(VkCommandBuffer, const struct VkCommandBufferBeginInfo *);
    typedef VkResult (*PFN_vkEndCommandBuffer)(VkCommandBuffer);
    typedef void (*PFN_vkCmdBeginRenderPass)(VkCommandBuffer, const struct VkRenderPassBeginInfo *);
    typedef void (*PFN_vkCmdDrawIndexed)(VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
    typedef void (*PFN_vkCmdEndRenderPass)(VkCommandBuffer);
    typedef VkResult (*PFN_vkQueueSubmit)(VkQueue, uint32_t, const struct VkSubmitInfo *, void *);
    typedef uint32_t (*PFN_panvk_v9_read_pixel)(VkCommandBuffer, uint32_t, uint32_t);

    LOOKUP(PFN_vkCreateInstance, vkCreateInstance);
    LOOKUP(PFN_vkDestroyInstance, vkDestroyInstance);
    LOOKUP(PFN_vkEnumeratePhysicalDevices, vkEnumeratePhysicalDevices);
    LOOKUP(PFN_vkGetPhysicalDeviceProperties, vkGetPhysicalDeviceProperties);
    LOOKUP(PFN_vkCreateDevice, vkCreateDevice);
    LOOKUP(PFN_vkDestroyDevice, vkDestroyDevice);
    LOOKUP(PFN_vkGetDeviceQueue, vkGetDeviceQueue);
    LOOKUP(PFN_vkCreateCommandPool, vkCreateCommandPool);
    LOOKUP(PFN_vkDestroyCommandPool, vkDestroyCommandPool);
    LOOKUP(PFN_vkAllocateCommandBuffers, vkAllocateCommandBuffers);
    LOOKUP(PFN_vkFreeCommandBuffers, vkFreeCommandBuffers);
    LOOKUP(PFN_vkBeginCommandBuffer, vkBeginCommandBuffer);
    LOOKUP(PFN_vkEndCommandBuffer, vkEndCommandBuffer);
    LOOKUP(PFN_vkCmdBeginRenderPass, vkCmdBeginRenderPass);
    LOOKUP(PFN_vkCmdDrawIndexed, vkCmdDrawIndexed);
    LOOKUP(PFN_vkCmdEndRenderPass, vkCmdEndRenderPass);
    LOOKUP(PFN_vkQueueSubmit, vkQueueSubmit);
    LOOKUP(PFN_panvk_v9_read_pixel, panvk_v9_read_pixel);
#undef LOOKUP

    printf("SUCCESS: Resolved all 18 Vulkan API functions via ICD proc address lookup\n");

    /* Create Instance */
    VkInstance instance = NULL;
    struct VkInstanceCreateInfo instInfo = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    pfn_vkCreateInstance(&instInfo, NULL, &instance);

    /* Enumerate Physical Devices */
    uint32_t count = 0;
    pfn_vkEnumeratePhysicalDevices(instance, &count, NULL);
    VkPhysicalDevice physDev = NULL;
    pfn_vkEnumeratePhysicalDevices(instance, &count, &physDev);

    struct VkPhysicalDeviceProperties props;
    pfn_vkGetPhysicalDeviceProperties(physDev, &props);
    printf("SUCCESS: Dynamically queried device: '%s'\n", props.deviceName);

    /* Create Device & Queue */
    VkDevice device = NULL;
    struct VkDeviceCreateInfo devInfo = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    pfn_vkCreateDevice(physDev, &devInfo, NULL, &device);
    VkQueue queue = NULL;
    pfn_vkGetDeviceQueue(device, 0, 0, &queue);

    /* Allocate Command Buffer */
    VkCommandPool pool = NULL;
    struct VkCommandPoolCreateInfo poolInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pfn_vkCreateCommandPool(device, &poolInfo, NULL, &pool);

    VkCommandBuffer cmd = NULL;
    struct VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .commandBufferCount = 1,
    };
    pfn_vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    /* Record & Submit Command Buffer */
    struct VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    pfn_vkBeginCommandBuffer(cmd, &beginInfo);
    struct VkRenderPassBeginInfo rpInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderAreaExtent = { .width = 16, .height = 16 },
        .clearColor = 0xFF0000FF,
    };
    pfn_vkCmdBeginRenderPass(cmd, &rpInfo);
    pfn_vkCmdDrawIndexed(cmd, 3, 1, 0, 0, 0);
    pfn_vkCmdEndRenderPass(cmd);
    pfn_vkEndCommandBuffer(cmd);

    struct VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    pfn_vkQueueSubmit(queue, 1, &submitInfo, NULL);
    printf("SUCCESS: Dispatched vkQueueSubmit via dynamic ICD function pointers\n");

    /* Verify Output */
    uint32_t p0 = pfn_panvk_v9_read_pixel(cmd, 0, 0);
    printf("Rendered Output: pixel(0,0)=0x%08x\n", p0);
    if (p0 == 0xFF00FF00) {
        printf("SUCCESS: Dynamically loaded PanVK ICD rendered solid green (0xFF00FF00)!\n");
    } else {
        fprintf(stderr, "FAIL: Expected 0xFF00FF00, got 0x%08x\n", p0);
        dlclose(handle);
        return 1;
    }

    pfn_vkFreeCommandBuffers(device, pool, 1, &cmd);
    pfn_vkDestroyCommandPool(device, pool, NULL);
    pfn_vkDestroyDevice(device, NULL);
    pfn_vkDestroyInstance(instance, NULL);
    dlclose(handle);

    printf("=== Step 4: Vulkan Loader ICD Shared Library Integration PASSED CLEANLY! ===\n");
    return 0;
}
