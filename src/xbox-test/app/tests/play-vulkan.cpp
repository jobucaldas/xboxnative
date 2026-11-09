// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that probes Vulkan the way a Vulkan-optional engine
// does (research/VULKAN-PLAN.md, milestone 1). It loads vulkan-1.dll by name,
// which Play serves from the launcher's runtime directory with the Khronos
// loader built from pinned source, creates an instance, and enumerates physical
// devices. The console has no Vulkan driver, so the answer must be the one a
// desktop loader gives on a machine with no GPU: the instance is created
// (VK_SUCCESS) and vkEnumeratePhysicalDevices reports VK_ERROR_INITIALIZATION_FAILED
// with no devices. Exit code 42 = exactly that.
//
// The count is deliberately checked as "unchanged", not as zero: the loader's
// terminator (terminator_EnumeratePhysicalDevices) returns before it writes
// *pPhysicalDeviceCount when setup_loader_term_phys_devs finds no devices, so
// the caller's sentinel survives. That is the real behaviour of the pinned
// loader, verified off-device; asserting a zero there would be a fiction.
//
// The Vulkan ABI is hand-declared: mingw-w64 ships no vulkan.h, and the fixture
// must not depend on the loader's own headers (it is a guest, not a driver).
#include <windows.h>
#include <stdint.h>

extern "C" {
typedef int32_t VkResult;
typedef uint32_t VkFlags;
typedef uint64_t VkDeviceSize;
typedef struct VkInstance_T* VkInstance;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkAllocationCallbacks VkAllocationCallbacks;

enum { VK_SUCCESS=0, VK_ERROR_INITIALIZATION_FAILED=-3, VK_ERROR_INCOMPATIBLE_DRIVER=-9, VK_INCOMPLETE=5 };
enum { VK_STRUCTURE_TYPE_APPLICATION_INFO=0, VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO=1 };
enum { VK_API_VERSION_1_0=(1u<<22)|(0u<<12)|0u };

typedef struct VkApplicationInfo_ {
    int32_t sType; const void* pNext; const char* pApplicationName; uint32_t applicationVersion;
    const char* pEngineName; uint32_t engineVersion; uint32_t apiVersion;
} VkApplicationInfo;
typedef struct VkInstanceCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags;
    const VkApplicationInfo* pApplicationInfo;
    uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames;
} VkInstanceCreateInfo;
typedef VkResult (__stdcall *PFN_vkCreateInstance)(const VkInstanceCreateInfo*,const VkAllocationCallbacks*,VkInstance*);
typedef VkResult (__stdcall *PFN_vkEnumeratePhysicalDevices)(VkInstance,uint32_t*,VkPhysicalDevice*);
typedef void (__stdcall *PFN_vkDestroyInstance)(VkInstance,const VkAllocationCallbacks*);

int vk_anchor;
void* vk_relocation=&vk_anchor;

DWORD vk_entry() {
    HMODULE loader=LoadLibraryW(L"vulkan-1.dll");
    if(!loader) return 10;   // the milestone: the load itself must succeed
    auto create=(PFN_vkCreateInstance)GetProcAddress(loader,"vkCreateInstance");
    auto enumerate=(PFN_vkEnumeratePhysicalDevices)GetProcAddress(loader,"vkEnumeratePhysicalDevices");
    auto destroy=(PFN_vkDestroyInstance)GetProcAddress(loader,"vkDestroyInstance");
    if(!create||!enumerate||!destroy) return 11;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO,nullptr,"play-vulkan",1,"play",1,VK_API_VERSION_1_0};
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,nullptr,0,&app,0,nullptr,0,nullptr};
    VkInstance instance=nullptr;
    VkResult result=create(&info,nullptr,&instance);
    if(result!=VK_SUCCESS||!instance) return 12;   // the null ICD lets the instance be created
    // Count-only query: no devices, and the loader's answer when there are none.
    uint32_t count=0xFFFFFFFF;
    result=enumerate(instance,&count,nullptr);
    if(result!=VK_ERROR_INITIALIZATION_FAILED) { destroy(instance,nullptr); return 13; }
    if(count!=0xFFFFFFFF) { destroy(instance,nullptr); return 14; }   // untouched, as the loader returns early
    // Array query: still no devices, and nothing is written to the array.
    VkPhysicalDevice device=(VkPhysicalDevice)(uintptr_t)0x1;
    count=1;
    result=enumerate(instance,&count,&device);
    if(result!=VK_ERROR_INITIALIZATION_FAILED||device!=(VkPhysicalDevice)(uintptr_t)0x1) { destroy(instance,nullptr); return 15; }
    destroy(instance,nullptr);
    return 42;
}
}
