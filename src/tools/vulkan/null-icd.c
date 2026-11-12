// SPDX-License-Identifier: GPL-3.0-or-later
// Null Vulkan ICD: a Vulkan-Loader driver that reports zero physical devices.
//
// Milestone 1 of issue #177. A Vulkan-optional engine probes Vulkan
// with LoadLibrary("vulkan-1.dll") and, on Windows without a driver, gets
// ERROR_MOD_NOT_FOUND and falls back to D3D11. Play ships a real loader built
// from pinned Khronos source, so the probe now succeeds; this ICD is what makes
// the loader answer "no GPU" cleanly instead of "Found no drivers!".
//
// The loader requires at least one ICD whose vkCreateInstance succeeds, or
// vkCreateInstance returns VK_ERROR_INCOMPATIBLE_DRIVER. With this ICD the
// instance is created (VK_SUCCESS) and vkEnumeratePhysicalDevices returns
// VK_ERROR_INITIALIZATION_FAILED with a count of zero, which is the same answer
// a desktop Vulkan loader gives on a machine with no GPU. There is no
// configuration in which the loader returns VK_SUCCESS with zero devices.
//
// The interface is the loader/ICD contract from vulkan/vk_icd.h (version 7):
// the four vk_icd* entry points, and the twelve core entry points
// loader_icd_init_entries requires to be non-NULL. Physical-device and device
// entry points can never be reached with zero devices; they exist so the
// loader's required-lookup check passes.
#include <stdint.h>
#include <string.h>

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

// Every dispatchable object reserves the loader's dispatch slot as its first
// word; the loader writes its table there.
typedef struct NullInstance {
    VK_LOADER_DATA loader;
} NullInstance;

static NullInstance null_instance;

VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pSupportedVersion) {
    if (pSupportedVersion == NULL) return VK_ERROR_INITIALIZATION_FAILED;
    // The loader offers CURRENT_LOADER_ICD_INTERFACE_VERSION (7); accept it and
    // anything older it may offer, but never a version this ICD does not know.
    if (*pSupportedVersion > CURRENT_LOADER_ICD_INTERFACE_VERSION)
        *pSupportedVersion = CURRENT_LOADER_ICD_INTERFACE_VERSION;
    if (*pSupportedVersion < MIN_PHYS_DEV_EXTENSION_ICD_INTERFACE_VERSION) {
        // Below 4 the loader cannot use vk_icdGetPhysicalDeviceProcAddr, and a
        // driver that reports VK_SUCCESS here would be misread as v1.
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }
    return VK_SUCCESS;
}

// ---- instance entry points -------------------------------------------------

static VKAPI_ATTR VkResult VKAPI_CALL
null_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator, VkInstance *pInstance) {
    (void)pCreateInfo;
    (void)pAllocator;
    if (pInstance == NULL) return VK_ERROR_INITIALIZATION_FAILED;
    set_loader_magic_value(&null_instance);
    *pInstance = (VkInstance)&null_instance;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL
null_DestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator) {
    (void)instance;
    (void)pAllocator;
}

static VKAPI_ATTR VkResult VKAPI_CALL
null_EnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pPropertyCount,
                                          VkExtensionProperties *pProperties) {
    (void)pLayerName;
    (void)pProperties;
    if (pPropertyCount == NULL) return VK_ERROR_INITIALIZATION_FAILED;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL
null_EnumerateInstanceLayerProperties(uint32_t *pPropertyCount, VkLayerProperties *pProperties) {
    (void)pProperties;
    if (pPropertyCount == NULL) return VK_ERROR_INITIALIZATION_FAILED;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL null_EnumerateInstanceVersion(uint32_t *pApiVersion) {
    if (pApiVersion == NULL) return VK_ERROR_INITIALIZATION_FAILED;
    *pApiVersion = VK_API_VERSION_1_0;
    return VK_SUCCESS;
}

// No physical devices: the count is always zero. The loader turns this into
// VK_ERROR_INITIALIZATION_FAILED for the application (setup_loader_term_phys_devs
// bails when the total capacity is zero); VK_SUCCESS here is what keeps the
// instance alive.
static VKAPI_ATTR VkResult VKAPI_CALL
null_EnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount,
                              VkPhysicalDevice *pPhysicalDevices) {
    (void)instance;
    (void)pPhysicalDevices;
    if (pPhysicalDeviceCount == NULL) return VK_ERROR_INITIALIZATION_FAILED;
    *pPhysicalDeviceCount = 0;
    return VK_SUCCESS;
}

// ---- device entry points ---------------------------------------------------
// Unreachable with zero physical devices. They exist because the loader refuses
// an ICD that does not answer all twelve required names.

static VKAPI_ATTR void VKAPI_CALL
null_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures) {
    (void)physicalDevice;
    if (pFeatures) memset(pFeatures, 0, sizeof *pFeatures);
}

static VKAPI_ATTR void VKAPI_CALL
null_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                       VkFormatProperties *pFormatProperties) {
    (void)physicalDevice;
    (void)format;
    if (pFormatProperties) memset(pFormatProperties, 0, sizeof *pFormatProperties);
}

static VKAPI_ATTR VkResult VKAPI_CALL
null_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                            VkImageType type, VkImageTiling tiling,
                                            VkImageUsageFlags usage, VkImageCreateFlags flags,
                                            VkImageFormatProperties *pImageFormatProperties) {
    (void)physicalDevice; (void)format; (void)type; (void)tiling; (void)usage; (void)flags;
    (void)pImageFormatProperties;
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

static VKAPI_ATTR void VKAPI_CALL
null_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties) {
    (void)physicalDevice;
    if (pProperties) memset(pProperties, 0, sizeof *pProperties);
}

static VKAPI_ATTR void VKAPI_CALL
null_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
                                            VkQueueFamilyProperties *pQueueFamilyProperties) {
    (void)physicalDevice;
    (void)pQueueFamilyProperties;
    if (pQueueFamilyPropertyCount) *pQueueFamilyPropertyCount = 0;
}

static VKAPI_ATTR void VKAPI_CALL
null_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                       VkPhysicalDeviceMemoryProperties *pMemoryProperties) {
    (void)physicalDevice;
    if (pMemoryProperties) memset(pMemoryProperties, 0, sizeof *pMemoryProperties);
}

static VKAPI_ATTR void VKAPI_CALL
null_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                                  VkImageType type, VkSampleCountFlagBits samples,
                                                  VkImageUsageFlags usage, VkImageTiling tiling,
                                                  uint32_t *pPropertyCount,
                                                  VkSparseImageFormatProperties *pProperties) {
    (void)physicalDevice; (void)format; (void)type; (void)samples; (void)usage; (void)tiling;
    (void)pProperties;
    if (pPropertyCount) *pPropertyCount = 0;
}

static VKAPI_ATTR VkResult VKAPI_CALL
null_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
    (void)physicalDevice; (void)pCreateInfo; (void)pAllocator;
    if (pDevice) *pDevice = VK_NULL_HANDLE;
    return VK_ERROR_INITIALIZATION_FAILED;
}

static VKAPI_ATTR VkResult VKAPI_CALL
null_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName,
                                        uint32_t *pPropertyCount, VkExtensionProperties *pProperties) {
    (void)physicalDevice; (void)pLayerName; (void)pProperties;
    if (pPropertyCount == NULL) return VK_ERROR_INITIALIZATION_FAILED;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
null_GetDeviceProcAddr(VkDevice device, const char *pName) {
    (void)device;
    (void)pName;
    return NULL;
}

// ---- proc address tables ---------------------------------------------------

static PFN_vkVoidFunction instance_proc(const char *pName) {
    if (pName == NULL) return NULL;
    if (!strcmp(pName, "vkDestroyInstance")) return (PFN_vkVoidFunction)null_DestroyInstance;
    if (!strcmp(pName, "vkEnumeratePhysicalDevices")) return (PFN_vkVoidFunction)null_EnumeratePhysicalDevices;
    if (!strcmp(pName, "vkGetPhysicalDeviceFeatures")) return (PFN_vkVoidFunction)null_GetPhysicalDeviceFeatures;
    if (!strcmp(pName, "vkGetPhysicalDeviceFormatProperties"))
        return (PFN_vkVoidFunction)null_GetPhysicalDeviceFormatProperties;
    if (!strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties"))
        return (PFN_vkVoidFunction)null_GetPhysicalDeviceImageFormatProperties;
    if (!strcmp(pName, "vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)null_GetPhysicalDeviceProperties;
    if (!strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties"))
        return (PFN_vkVoidFunction)null_GetPhysicalDeviceQueueFamilyProperties;
    if (!strcmp(pName, "vkGetPhysicalDeviceMemoryProperties"))
        return (PFN_vkVoidFunction)null_GetPhysicalDeviceMemoryProperties;
    if (!strcmp(pName, "vkGetPhysicalDeviceSparseImageFormatProperties"))
        return (PFN_vkVoidFunction)null_GetPhysicalDeviceSparseImageFormatProperties;
    if (!strcmp(pName, "vkCreateDevice")) return (PFN_vkVoidFunction)null_CreateDevice;
    if (!strcmp(pName, "vkEnumerateDeviceExtensionProperties"))
        return (PFN_vkVoidFunction)null_EnumerateDeviceExtensionProperties;
    if (!strcmp(pName, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)null_GetDeviceProcAddr;
    return NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName) {
    (void)instance;
    if (pName == NULL) return NULL;
    // Interface version 7 requires these to be queryable through this entry
    // point, not only as direct exports.
    if (!strcmp(pName, "vk_icdNegotiateLoaderICDInterfaceVersion"))
        return (PFN_vkVoidFunction)vk_icdNegotiateLoaderICDInterfaceVersion;
    if (!strcmp(pName, "vk_icdGetInstanceProcAddr")) return (PFN_vkVoidFunction)vk_icdGetInstanceProcAddr;
    if (!strcmp(pName, "vk_icdGetPhysicalDeviceProcAddr"))
        return (PFN_vkVoidFunction)vk_icdGetPhysicalDeviceProcAddr;
    if (!strcmp(pName, "vk_icdEnumerateAdapterPhysicalDevices"))
        return (PFN_vkVoidFunction)vk_icdEnumerateAdapterPhysicalDevices;
    if (!strcmp(pName, "vkCreateInstance")) return (PFN_vkVoidFunction)null_CreateInstance;
    if (!strcmp(pName, "vkEnumerateInstanceExtensionProperties"))
        return (PFN_vkVoidFunction)null_EnumerateInstanceExtensionProperties;
    if (!strcmp(pName, "vkEnumerateInstanceLayerProperties"))
        return (PFN_vkVoidFunction)null_EnumerateInstanceLayerProperties;
    if (!strcmp(pName, "vkEnumerateInstanceVersion")) return (PFN_vkVoidFunction)null_EnumerateInstanceVersion;
    if (!strcmp(pName, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)vk_icdGetInstanceProcAddr;
    return instance_proc(pName);
}

// Nothing beyond the core names above exists, so a physical-device lookup is
// always NULL. The loader calls this only when it has a device to look up.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char *pName) {
    (void)instance;
    (void)pName;
    return NULL;
}

// Windows adapter enumeration (interface version 6+). With no devices, every
// adapter reports none; the loader then finds no capacity and fails the same way
// it does for vkEnumeratePhysicalDevices.
VKAPI_ATTR VkResult VKAPI_CALL
vk_icdEnumerateAdapterPhysicalDevices(VkInstance instance, LUID adapterLUID,
                                      uint32_t *pPhysicalDeviceCount,
                                      VkPhysicalDevice *pPhysicalDevices) {
    (void)instance;
    (void)adapterLUID;
    (void)pPhysicalDevices;
    if (pPhysicalDeviceCount == NULL) return VK_ERROR_INITIALIZATION_FAILED;
    *pPhysicalDeviceCount = 0;
    return VK_SUCCESS;
}
