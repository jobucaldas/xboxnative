// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that runs the Vulkan startup path a Vulkan game
// takes (research/VULKAN-PLAN.md, milestone 2). It loads vulkan-1.dll by name,
// which Play serves from the launcher's runtime directory with the Khronos
// loader built from pinned source, creates an instance with VK_KHR_surface and
// VK_KHR_win32_surface, enumerates physical devices (Mesa's dzn, Vulkan on
// D3D12, must answer at least one), creates a win32 surface on a guest HWND the
// launcher maps to its CoreWindow, creates a device, a swap chain, a render
// pass and framebuffers, then clears each acquired image and presents it.
//
// Stages are reported so a failure names the first missing piece:
//   exit 42  a frame was presented ("PLAY-VULKAN-DZN PASS frame-presented")
//   exit 43  instance, physical device and surface worked, but vkCreateDevice
//            failed: BLOCKED with the reason, never a pass. Off-device this is
//            Wine's vkd3d, whose DXIL parser rejects the shader model dzn emits
//            (it reports SM 6.0 as its highest and dzn's meta shaders do not
//            parse); the console's D3D12 is a different implementation.
//   other    a stage before that failed.
//
// The Vulkan ABI is hand-declared: mingw-w64 ships no vulkan.h, and the fixture
// must not depend on the loader's own headers (it is a guest, not a driver).
#include <windows.h>
#include <stdint.h>

// The fixture links /nodefaultlib (no CRT), so it writes its stage lines with
// WriteFile and formats the few numbers it reports by hand. Zero-initialising
// the Vulkan structs makes the compiler emit memset, so it is provided here
// (built with -fno-builtin so this loop is not turned back into a call).
extern "C" void* memset(void* dest, int value, size_t count) {
    volatile unsigned char* p = static_cast<volatile unsigned char*>(dest);
    while (count--) *p++ = static_cast<unsigned char>(value);
    return dest;
}
static void emit(const char* text) {
    DWORD written = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, text, (DWORD)lstrlenA(text), &written, nullptr);
}
static char* append(char* p, const char* text) {
    while (*text) *p++ = *text++;
    return p;
}
static char* appendInt(char* p, int32_t value) {
    char digits[12]; int n = 0;
    uint32_t v = value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    if (value < 0) *p++ = '-';
    do { digits[n++] = char('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = digits[--n];
    return p;
}
static char* appendUInt(char* p, uint32_t value) {
    char digits[12]; int n = 0;
    do { digits[n++] = char('0' + value % 10u); value /= 10u; } while (value);
    while (n) *p++ = digits[--n];
    return p;
}
static char* appendHex(char* p, uint32_t value) {
    static const char hex[] = "0123456789abcdef";
    *p++ = '0'; *p++ = 'x';
    bool started = false;
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t digit = (value >> shift) & 0xfu;
        if (digit || started || shift == 0) { *p++ = hex[digit]; started = true; }
    }
    return p;
}

extern "C" {
typedef int32_t VkResult;
typedef uint32_t VkFlags;
typedef uint64_t VkDeviceSize;
typedef struct VkInstance_T* VkInstance;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkDevice_T* VkDevice;
typedef struct VkQueue_T* VkQueue;
typedef struct VkSurfaceKHR_T* VkSurfaceKHR;
typedef struct VkSwapchainKHR_T* VkSwapchainKHR;
typedef struct VkCommandBuffer_T* VkCommandBuffer;
typedef struct VkSemaphore_T* VkSemaphore;
typedef struct VkFence_T* VkFence;
typedef struct VkImage_T* VkImage;
typedef struct VkImageView_T* VkImageView;
typedef struct VkRenderPass_T* VkRenderPass;
typedef struct VkFramebuffer_T* VkFramebuffer;
typedef struct VkCommandPool_T* VkCommandPool;
typedef struct VkAllocationCallbacks VkAllocationCallbacks;

enum { VK_SUCCESS=0, VK_ERROR_INITIALIZATION_FAILED=-3, VK_ERROR_INCOMPATIBLE_DRIVER=-9, VK_TIMEOUT=2, VK_SUBOPTIMAL_KHR=1000001003 };
enum {
    VK_STRUCTURE_TYPE_APPLICATION_INFO=0, VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO=1,
    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO=2, VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO=3,
    VK_STRUCTURE_TYPE_SUBMIT_INFO=4, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO=8,
    VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO=9, VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO=15,
    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO=37, VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO=38,
    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO=39, VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO=40,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO=42, VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO=43,
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER=45,
    VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR=1000009000,
    VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR=1000001000,
    VK_STRUCTURE_TYPE_PRESENT_INFO_KHR=1000001003,
};
enum { VK_API_VERSION_1_0=(1u<<22)|(0u<<12)|0u };
enum { VK_QUEUE_GRAPHICS_BIT=0x1 };
enum { VK_PRESENT_MODE_FIFO_KHR=2 };
enum { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR=0x1, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR=0x8 };
enum { VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT=0x10 };
enum { VK_SHARING_MODE_EXCLUSIVE=0 };
enum { VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR=0x1 };
enum { VK_IMAGE_LAYOUT_UNDEFINED=0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL=2,
       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR=1000001002 };
enum { VK_ATTACHMENT_LOAD_OP_CLEAR=1, VK_ATTACHMENT_STORE_OP_STORE=0 };
enum { VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT=0x100 };
enum { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT=0x400 };
enum { VK_IMAGE_ASPECT_COLOR_BIT=0x1 };
enum { VK_IMAGE_VIEW_TYPE_2D=1 };
enum { VK_COMPONENT_SWIZZLE_IDENTITY=0, VK_COMPONENT_SWIZZLE_R=1, VK_COMPONENT_SWIZZLE_G=2,
       VK_COMPONENT_SWIZZLE_B=3, VK_COMPONENT_SWIZZLE_A=4 };
enum { VK_SAMPLE_COUNT_1_BIT=1 };
enum { VK_PIPELINE_BIND_POINT_GRAPHICS=0 };
enum { VK_COMMAND_BUFFER_LEVEL_PRIMARY=0 };
enum { VK_SUBPASS_CONTENTS_INLINE=0 };

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
// Only the prefix is read; the buffer is sized for the whole struct the driver writes.
typedef struct VkPhysicalDeviceProperties_ {
    uint32_t apiVersion, driverVersion, vendorID, deviceID; int32_t deviceType;
    char deviceName[256]; uint8_t pipelineCacheUUID[16];
    uint8_t limitsAndSparse[532];   // VkPhysicalDeviceLimits + VkSparseProperties
} VkPhysicalDeviceProperties;
typedef struct VkExtent3D_ { uint32_t width, height, depth; } VkExtent3D;
typedef struct VkQueueFamilyProperties_ {
    VkFlags queueFlags; uint32_t queueCount, timestampValidBits; VkExtent3D minImageTransferGranularity;
} VkQueueFamilyProperties;
typedef struct VkWin32SurfaceCreateInfoKHR_ {
    int32_t sType; const void* pNext; VkFlags flags; HINSTANCE hinstance; HWND hwnd;
} VkWin32SurfaceCreateInfoKHR;
typedef struct VkExtent2D_ { uint32_t width, height; } VkExtent2D;
typedef struct VkSurfaceCapabilitiesKHR_ {
    uint32_t minImageCount, maxImageCount;
    VkExtent2D currentExtent, minImageExtent, maxImageExtent;
    uint32_t maxImageArrayLayers;
    VkFlags supportedTransforms, currentTransform, supportedCompositeAlpha, supportedUsageFlags;
} VkSurfaceCapabilitiesKHR;
typedef struct VkSurfaceFormatKHR_ { int32_t format, colorSpace; } VkSurfaceFormatKHR;
typedef struct VkDeviceQueueCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags;
    uint32_t queueFamilyIndex, queueCount; const float* pQueuePriorities;
} VkDeviceQueueCreateInfo;
typedef struct VkDeviceCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags;
    uint32_t queueCreateInfoCount; const VkDeviceQueueCreateInfo* pQueueCreateInfos;
    uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames;
    const void* pEnabledFeatures;
} VkDeviceCreateInfo;
typedef struct VkSwapchainCreateInfoKHR_ {
    int32_t sType; const void* pNext; VkFlags flags; VkSurfaceKHR surface;
    uint32_t minImageCount; int32_t imageFormat, imageColorSpace; VkExtent2D imageExtent;
    uint32_t imageArrayLayers; VkFlags imageUsage; int32_t imageSharingMode;
    uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices;
    int32_t preTransform, compositeAlpha, presentMode; VkFlags clipped; VkSwapchainKHR oldSwapchain;
} VkSwapchainCreateInfoKHR;
typedef struct VkComponentMapping_ { int32_t r, g, b, a; } VkComponentMapping;
typedef struct VkImageSubresourceRange_ {
    VkFlags aspectMask; uint32_t baseMipLevel, levelCount, baseArrayLayer, layerCount;
} VkImageSubresourceRange;
typedef struct VkImageViewCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; VkImage image;
    int32_t viewType, format; VkComponentMapping components; VkImageSubresourceRange subresourceRange;
} VkImageViewCreateInfo;
typedef struct VkAttachmentDescription_ {
    VkFlags flags; int32_t format, samples, loadOp, storeOp, stencilLoadOp, stencilStoreOp;
    int32_t initialLayout, finalLayout;
} VkAttachmentDescription;
typedef struct VkAttachmentReference_ { uint32_t attachment; int32_t layout; } VkAttachmentReference;
typedef struct VkSubpassDescription_ {
    VkFlags flags; int32_t pipelineBindPoint;
    uint32_t inputAttachmentCount; const VkAttachmentReference* pInputAttachments;
    uint32_t colorAttachmentCount; const VkAttachmentReference* pColorAttachments;
    const VkAttachmentReference* pResolveAttachments; const VkAttachmentReference* pDepthStencilAttachment;
    uint32_t preserveAttachmentCount; const uint32_t* pPreserveAttachments;
} VkSubpassDescription;
typedef struct VkRenderPassCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags;
    uint32_t attachmentCount; const VkAttachmentDescription* pAttachments;
    uint32_t subpassCount; const VkSubpassDescription* pSubpasses;
    uint32_t dependencyCount; const void* pDependencies;
} VkRenderPassCreateInfo;
typedef struct VkFramebufferCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; VkRenderPass renderPass;
    uint32_t attachmentCount; const VkImageView* pAttachments; uint32_t width, height, layers;
} VkFramebufferCreateInfo;
typedef struct VkCommandPoolCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; uint32_t queueFamilyIndex;
} VkCommandPoolCreateInfo;
typedef struct VkCommandBufferAllocateInfo_ {
    int32_t sType; const void* pNext; VkCommandPool commandPool; int32_t level;
    uint32_t commandBufferCount;
} VkCommandBufferAllocateInfo;
typedef struct VkCommandBufferBeginInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; const void* pInheritanceInfo;
} VkCommandBufferBeginInfo;
typedef struct VkClearValue_ {
    union { struct { float r, g, b, a; } color; struct { float d; uint32_t s; } ds; };
} VkClearValue;
typedef struct VkRect2D_ { int32_t x, y; VkExtent2D extent; } VkRect2D;
typedef struct VkRenderPassBeginInfo_ {
    int32_t sType; const void* pNext; VkRenderPass renderPass; VkFramebuffer framebuffer;
    VkRect2D renderArea; uint32_t clearValueCount; const VkClearValue* pClearValues;
} VkRenderPassBeginInfo;
typedef struct VkImageMemoryBarrier_ {
    int32_t sType; const void* pNext; VkFlags srcAccessMask, dstAccessMask;
    int32_t oldLayout, newLayout; uint32_t srcQueueFamilyIndex, dstQueueFamilyIndex;
    VkImage image; VkImageSubresourceRange subresourceRange;
} VkImageMemoryBarrier;
typedef struct VkSubmitInfo_ {
    int32_t sType; const void* pNext;
    uint32_t waitSemaphoreCount; const VkSemaphore* pWaitSemaphores; const VkFlags* pWaitDstStageMask;
    uint32_t commandBufferCount; const VkCommandBuffer* pCommandBuffers;
    uint32_t signalSemaphoreCount; const VkSemaphore* pSignalSemaphores;
} VkSubmitInfo;
typedef struct VkPresentInfoKHR_ {
    int32_t sType; const void* pNext;
    uint32_t waitSemaphoreCount; const VkSemaphore* pWaitSemaphores;
    uint32_t swapchainCount; const VkSwapchainKHR* pSwapchains; const uint32_t* pImageIndices;
    VkResult* pResults;
} VkPresentInfoKHR;
typedef struct VkFenceCreateInfo_ { int32_t sType; const void* pNext; VkFlags flags; } VkFenceCreateInfo;
typedef struct VkSemaphoreCreateInfo_ { int32_t sType; const void* pNext; VkFlags flags; } VkSemaphoreCreateInfo;

typedef VkResult (__stdcall *PFN_vkCreateInstance)(const VkInstanceCreateInfo*,const VkAllocationCallbacks*,VkInstance*);
typedef VkResult (__stdcall *PFN_vkEnumeratePhysicalDevices)(VkInstance,uint32_t*,VkPhysicalDevice*);
typedef void (__stdcall *PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice,VkPhysicalDeviceProperties*);
typedef void (__stdcall *PFN_vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice,uint32_t*,VkQueueFamilyProperties*);
typedef VkResult (__stdcall *PFN_vkCreateWin32SurfaceKHR)(VkInstance,const VkWin32SurfaceCreateInfoKHR*,const VkAllocationCallbacks*,VkSurfaceKHR*);
typedef VkResult (__stdcall *PFN_vkGetPhysicalDeviceSurfaceSupportKHR)(VkPhysicalDevice,uint32_t,VkSurfaceKHR,uint32_t*);
typedef VkResult (__stdcall *PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(VkPhysicalDevice,VkSurfaceKHR,VkSurfaceCapabilitiesKHR*);
typedef VkResult (__stdcall *PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)(VkPhysicalDevice,VkSurfaceKHR,uint32_t*,VkSurfaceFormatKHR*);
typedef VkResult (__stdcall *PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)(VkPhysicalDevice,VkSurfaceKHR,uint32_t*,int32_t*);
typedef VkResult (__stdcall *PFN_vkCreateDevice)(VkPhysicalDevice,const VkDeviceCreateInfo*,const VkAllocationCallbacks*,VkDevice*);
typedef void (__stdcall *PFN_vkGetDeviceQueue)(VkDevice,uint32_t,uint32_t,VkQueue*);
typedef VkResult (__stdcall *PFN_vkCreateSwapchainKHR)(VkDevice,const VkSwapchainCreateInfoKHR*,const VkAllocationCallbacks*,VkSwapchainKHR*);
typedef VkResult (__stdcall *PFN_vkGetSwapchainImagesKHR)(VkDevice,VkSwapchainKHR,uint32_t*,VkImage*);
typedef VkResult (__stdcall *PFN_vkCreateImageView)(VkDevice,const VkImageViewCreateInfo*,const VkAllocationCallbacks*,VkImageView*);
typedef VkResult (__stdcall *PFN_vkCreateRenderPass)(VkDevice,const VkRenderPassCreateInfo*,const VkAllocationCallbacks*,VkRenderPass*);
typedef VkResult (__stdcall *PFN_vkCreateFramebuffer)(VkDevice,const VkFramebufferCreateInfo*,const VkAllocationCallbacks*,VkFramebuffer*);
typedef VkResult (__stdcall *PFN_vkCreateCommandPool)(VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool*);
typedef VkResult (__stdcall *PFN_vkAllocateCommandBuffers)(VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer*);
typedef VkResult (__stdcall *PFN_vkBeginCommandBuffer)(VkCommandBuffer,const VkCommandBufferBeginInfo*);
typedef void (__stdcall *PFN_vkCmdPipelineBarrier)(VkCommandBuffer,VkFlags,VkFlags,VkFlags,uint32_t,const void*,uint32_t,const void*,uint32_t,const VkImageMemoryBarrier*);
typedef void (__stdcall *PFN_vkCmdBeginRenderPass)(VkCommandBuffer,const VkRenderPassBeginInfo*,int32_t);
typedef void (__stdcall *PFN_vkCmdEndRenderPass)(VkCommandBuffer);
typedef VkResult (__stdcall *PFN_vkEndCommandBuffer)(VkCommandBuffer);
typedef VkResult (__stdcall *PFN_vkResetCommandPool)(VkDevice,VkCommandPool,VkFlags);
typedef VkResult (__stdcall *PFN_vkQueueSubmit)(VkQueue,uint32_t,const VkSubmitInfo*,VkFence);
typedef VkResult (__stdcall *PFN_vkQueueWaitIdle)(VkQueue);
typedef VkResult (__stdcall *PFN_vkAcquireNextImageKHR)(VkDevice,VkSwapchainKHR,uint64_t,VkSemaphore,VkFence,uint32_t*);
typedef VkResult (__stdcall *PFN_vkQueuePresentKHR)(VkQueue,const VkPresentInfoKHR*);
typedef VkResult (__stdcall *PFN_vkCreateSemaphore)(VkDevice,const VkSemaphoreCreateInfo*,const VkAllocationCallbacks*,VkSemaphore*);
typedef VkResult (__stdcall *PFN_vkCreateFence)(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence*);
typedef VkResult (__stdcall *PFN_vkWaitForFences)(VkDevice,uint32_t,const VkFence*,uint32_t,uint64_t);
typedef VkResult (__stdcall *PFN_vkResetFences)(VkDevice,uint32_t,const VkFence*);

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

// A relocation keeps the entry point reachable for the linker's /include, as
// the other Play fixtures do.
int vk_dzn_anchor;
void* vk_relocation = &vk_dzn_anchor;

static int fail(int code, const char* stage, VkResult result) {
    char line[192]; char* p = append(line, "PLAY-VULKAN-DZN FAIL ");
    p = append(p, stage); p = append(p, " result="); p = appendInt(p, result);
    p = append(p, "\n"); *p = 0;
    emit(line);
    return code;
}

int vk_dzn_entry() {
    // A guest window, the way a Vulkan game creates one; the launcher's DXGI
    // adapter maps it to its CoreWindow for the swap chain.
    WNDCLASSEXW cls{sizeof cls};
    cls.lpfnWndProc = wndproc; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"play-vulkan-dzn";
    if (!RegisterClassExW(&cls)) return fail(10, "RegisterClassExW", (VkResult)GetLastError());
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"play-vulkan-dzn", WS_OVERLAPPEDWINDOW,
                                  0, 0, 640, 480, nullptr, nullptr, cls.hInstance, nullptr);
    if (!window) return fail(10, "CreateWindowExW", (VkResult)GetLastError());

    HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
    if (!loader) return fail(11, "LoadLibraryW(vulkan-1.dll)", 0);
    auto gipa = (void* (__stdcall*)(VkInstance, const char*))GetProcAddress(loader, "vkGetInstanceProcAddr");
    if (!gipa) return fail(11, "GetProcAddress(vkGetInstanceProcAddr)", 0);
    // The loader exports the global and instance commands directly; with a null
    // instance vkGetInstanceProcAddr is only required to answer the globals.
    auto createInstance = (PFN_vkCreateInstance)GetProcAddress(loader, "vkCreateInstance");
    auto enumerate = (PFN_vkEnumeratePhysicalDevices)GetProcAddress(loader, "vkEnumeratePhysicalDevices");
    if (!createInstance || !enumerate) return fail(11, "GetProcAddress(global commands)", createInstance ? 1 : 0);

    const char* extensions[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "play-vulkan-dzn", 1, "play", 1, VK_API_VERSION_1_0};
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app, 0, nullptr, 2, extensions};
    VkInstance instance = nullptr;
    VkResult result = createInstance(&instanceInfo, nullptr, &instance);
    if (result != VK_SUCCESS || !instance) return fail(12, "vkCreateInstance", result);
    emit("PLAY-VULKAN-DZN STAGE instance-created\n");

    uint32_t count = 0;
    result = enumerate(instance, &count, nullptr);
    if (result != VK_SUCCESS || !count) return fail(13, "vkEnumeratePhysicalDevices", result);
    VkPhysicalDevice devices[8]{};
    uint32_t listed = count < 8 ? count : 8;
    result = enumerate(instance, &listed, devices);
    if (result != VK_SUCCESS || !listed) return fail(13, "vkEnumeratePhysicalDevices(array)", result);
    VkPhysicalDevice physical = devices[0];
    VkPhysicalDeviceProperties properties{};
    auto getProperties = (PFN_vkGetPhysicalDeviceProperties)gipa(instance, "vkGetPhysicalDeviceProperties");
    if (!getProperties) return 13;
    getProperties(physical, &properties);
    {char line[384]; char* p = append(line, "PLAY-VULKAN-DZN STAGE physical-device count=");
     p = appendUInt(p, listed); p = append(p, " api="); p = appendHex(p, properties.apiVersion);
     p = append(p, " driver="); p = appendHex(p, properties.driverVersion);
     p = append(p, " name=\""); p = append(p, properties.deviceName); p = append(p, "\"\n"); *p = 0; emit(line);}

    auto createSurface = (PFN_vkCreateWin32SurfaceKHR)gipa(instance, "vkCreateWin32SurfaceKHR");
    auto surfaceSupport = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)gipa(instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
    auto queueFamilies = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    auto surfaceCaps = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)gipa(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    auto surfaceFormats = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)gipa(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    auto presentModes = (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)gipa(instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    auto createDevice = (PFN_vkCreateDevice)gipa(instance, "vkCreateDevice");
    if (!createSurface || !surfaceSupport || !queueFamilies || !surfaceCaps || !surfaceFormats || !presentModes || !createDevice)
        return 14;

    VkWin32SurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, nullptr, 0,
                                            GetModuleHandleW(nullptr), window};
    VkSurfaceKHR surface = nullptr;
    result = createSurface(instance, &surfaceInfo, nullptr, &surface);
    if (result != VK_SUCCESS || !surface) return fail(15, "vkCreateWin32SurfaceKHR", result);
    {char line[192]; char* p = append(line, "PLAY-VULKAN-DZN STAGE win32-surface-created hwnd=");
     p = appendUInt(p, (uint32_t)(uintptr_t)window); p = append(p, "\n"); *p = 0; emit(line);}

    uint32_t familyCount = 0;
    queueFamilies(physical, &familyCount, nullptr);
    if (!familyCount) return 16;
    VkQueueFamilyProperties families[16]{};
    uint32_t familiesRead = familyCount < 16 ? familyCount : 16;
    queueFamilies(physical, &familiesRead, families);
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < familiesRead; ++i) {
        uint32_t supported = 0;
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT &&
            surfaceSupport(physical, i, surface, &supported) == VK_SUCCESS && supported) { family = i; break; }
    }
    if (family == UINT32_MAX) return 16;
    {char line[160]; char* p = append(line, "PLAY-VULKAN-DZN STAGE queue-family index=");
     p = appendUInt(p, family); p = append(p, " count="); p = appendUInt(p, families[family].queueCount);
     p = append(p, "\n"); *p = 0; emit(line);}

    VkSurfaceCapabilitiesKHR caps{};
    result = surfaceCaps(physical, surface, &caps);
    if (result != VK_SUCCESS) return fail(17, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
    uint32_t formatCount = 0;
    result = surfaceFormats(physical, surface, &formatCount, nullptr);
    if (result != VK_SUCCESS || !formatCount) return fail(17, "vkGetPhysicalDeviceSurfaceFormatsKHR", result);
    VkSurfaceFormatKHR formats[16]{};
    uint32_t formatsRead = formatCount < 16 ? formatCount : 16;
    surfaceFormats(physical, surface, &formatsRead, formats);
    VkSurfaceFormatKHR chosen = formats[0];
    for (uint32_t i = 0; i < formatsRead; ++i)
        if (formats[i].format == 44 /* VK_FORMAT_B8G8R8A8_UNORM */) { chosen = formats[i]; break; }
    {char line[256]; char* p = append(line, "PLAY-VULKAN-DZN STAGE surface formats=");
     p = appendUInt(p, formatsRead); p = append(p, " chosen="); p = appendInt(p, chosen.format);
     p = append(p, " minImages="); p = appendUInt(p, caps.minImageCount);
     p = append(p, " maxImages="); p = appendUInt(p, caps.maxImageCount);
     p = append(p, " extent="); p = appendUInt(p, caps.currentExtent.width);
     p = append(p, "x"); p = appendUInt(p, caps.currentExtent.height); p = append(p, "\n"); *p = 0; emit(line);}

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, family, 1, &priority};
    const char* deviceExtensions[] = {"VK_KHR_swapchain"};
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr, 0, 1, &queueInfo,
                                  0, nullptr, 1, deviceExtensions, nullptr};
    VkDevice device = nullptr;
    result = createDevice(physical, &deviceInfo, nullptr, &device);
    if (result != VK_SUCCESS || !device) {
        // The host's D3D12 refused dzn's device: everything up to the surface
        // worked, so this is a host limit, reported and never counted as a pass.
        {char line[224]; char* p = append(line, "PLAY-VULKAN-DZN BLOCKED device-creation result=");
         p = appendInt(p, result);
         p = append(p, " (the host D3D12 rejected dzn's device; off-device this is Wine's vkd3d DXIL parser, not the console)\n");
         *p = 0; emit(line);}
        return 43;
    }
    auto getDeviceQueue = (PFN_vkGetDeviceQueue)gipa(instance, "vkGetDeviceQueue");
    VkQueue queue = nullptr;
    getDeviceQueue(device, family, 0, &queue);
    {char line[192]; char* p = append(line, "PLAY-VULKAN-DZN STAGE device-created queue=");
     p = appendUInt(p, (uint32_t)(uintptr_t)queue); p = append(p, "\n"); *p = 0; emit(line);}

    auto createSwapchain = (PFN_vkCreateSwapchainKHR)gipa(instance, "vkCreateSwapchainKHR");
    auto getImages = (PFN_vkGetSwapchainImagesKHR)gipa(instance, "vkGetSwapchainImagesKHR");
    auto createImageView = (PFN_vkCreateImageView)gipa(instance, "vkCreateImageView");
    auto createRenderPass = (PFN_vkCreateRenderPass)gipa(instance, "vkCreateRenderPass");
    auto createFramebuffer = (PFN_vkCreateFramebuffer)gipa(instance, "vkCreateFramebuffer");
    auto createCommandPool = (PFN_vkCreateCommandPool)gipa(instance, "vkCreateCommandPool");
    auto allocateCommandBuffers = (PFN_vkAllocateCommandBuffers)gipa(instance, "vkAllocateCommandBuffers");
    auto beginCommandBuffer = (PFN_vkBeginCommandBuffer)gipa(instance, "vkBeginCommandBuffer");
    auto cmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)gipa(instance, "vkCmdPipelineBarrier");
    auto cmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)gipa(instance, "vkCmdBeginRenderPass");
    auto cmdEndRenderPass = (PFN_vkCmdEndRenderPass)gipa(instance, "vkCmdEndRenderPass");
    auto endCommandBuffer = (PFN_vkEndCommandBuffer)gipa(instance, "vkEndCommandBuffer");
    auto resetCommandPool = (PFN_vkResetCommandPool)gipa(instance, "vkResetCommandPool");
    auto queueSubmit = (PFN_vkQueueSubmit)gipa(instance, "vkQueueSubmit");
    auto queueWaitIdle = (PFN_vkQueueWaitIdle)gipa(instance, "vkQueueWaitIdle");
    auto acquireNextImage = (PFN_vkAcquireNextImageKHR)gipa(instance, "vkAcquireNextImageKHR");
    auto queuePresent = (PFN_vkQueuePresentKHR)gipa(instance, "vkQueuePresentKHR");
    auto createSemaphore = (PFN_vkCreateSemaphore)gipa(instance, "vkCreateSemaphore");
    auto createFence = (PFN_vkCreateFence)gipa(instance, "vkCreateFence");
    auto waitForFences = (PFN_vkWaitForFences)gipa(instance, "vkWaitForFences");
    auto resetFences = (PFN_vkResetFences)gipa(instance, "vkResetFences");
    if (!createSwapchain || !getImages || !createImageView || !createRenderPass || !createFramebuffer ||
        !createCommandPool || !allocateCommandBuffers || !beginCommandBuffer || !cmdPipelineBarrier ||
        !cmdBeginRenderPass || !cmdEndRenderPass || !endCommandBuffer || !resetCommandPool ||
        !queueSubmit || !queueWaitIdle ||
        !acquireNextImage || !queuePresent || !createSemaphore || !createFence || !waitForFences || !resetFences)
        return 18;

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) { extent.width = 640; extent.height = 480; }
    int32_t compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ?
                             VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    VkSwapchainCreateInfoKHR swapchainInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, nullptr, 0, surface,
        caps.minImageCount, chosen.format, chosen.colorSpace, extent, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, compositeAlpha,
        VK_PRESENT_MODE_FIFO_KHR, 1, nullptr};
    VkSwapchainKHR swapchain = nullptr;
    result = createSwapchain(device, &swapchainInfo, nullptr, &swapchain);
    if (result != VK_SUCCESS || !swapchain) return fail(19, "vkCreateSwapchainKHR", result);
    uint32_t imageCount = 0;
    result = getImages(device, swapchain, &imageCount, nullptr);
    if (result != VK_SUCCESS || !imageCount) return fail(19, "vkGetSwapchainImagesKHR", result);
    VkImage images[8]{};
    uint32_t imagesRead = imageCount < 8 ? imageCount : 8;
    result = getImages(device, swapchain, &imagesRead, images);
    if (result != VK_SUCCESS || !imagesRead) return fail(19, "vkGetSwapchainImagesKHR(array)", result);
    // The driver indexes its images by the DXGI back buffer index, so a
    // swapchain that reports fewer images than were asked for is presenting
    // through a buffer it does not track. Report both counts and refuse the
    // shortfall here rather than at the first acquire.
    if (imagesRead < swapchainInfo.minImageCount)
        return fail(19, "vkGetSwapchainImagesKHR(fewer than requested)", (VkResult)imagesRead);
    {char line[192]; char* p = append(line, "PLAY-VULKAN-DZN STAGE swapchain images=");
     p = appendUInt(p, imagesRead); p = append(p, " requested="); p = appendUInt(p, swapchainInfo.minImageCount);
     p = append(p, "\n"); *p = 0; emit(line);}

    VkAttachmentDescription attachment{0, chosen.format, VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, 0, 0,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, nullptr, 1, &reference, nullptr, nullptr, 0, nullptr};
    VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0,
                                          1, &attachment, 1, &subpass, 0, nullptr};
    VkRenderPass renderPass = nullptr;
    result = createRenderPass(device, &renderPassInfo, nullptr, &renderPass);
    if (result != VK_SUCCESS || !renderPass) return fail(20, "vkCreateRenderPass", result);

    VkImageView views[8]{};
    VkFramebuffer framebuffers[8]{};
    for (uint32_t i = 0; i < imagesRead; ++i) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, images[i],
            VK_IMAGE_VIEW_TYPE_2D, chosen.format,
            {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
             VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        result = createImageView(device, &viewInfo, nullptr, &views[i]);
        if (result != VK_SUCCESS || !views[i]) return fail(21, "vkCreateImageView", result);
        VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0,
            renderPass, 1, &views[i], extent.width, extent.height, 1};
        result = createFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]);
        if (result != VK_SUCCESS || !framebuffers[i]) return fail(21, "vkCreateFramebuffer", result);
    }

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, 0, family};
    VkCommandPool pool = nullptr;
    result = createCommandPool(device, &poolInfo, nullptr, &pool);
    if (result != VK_SUCCESS || !pool) return fail(22, "vkCreateCommandPool", result);
    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr,
                                             pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    VkCommandBuffer commandBuffer = nullptr;
    result = allocateCommandBuffers(device, &allocateInfo, &commandBuffer);
    if (result != VK_SUCCESS || !commandBuffer) return fail(22, "vkAllocateCommandBuffers", result);
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0};
    VkSemaphore acquired = nullptr;
    result = createSemaphore(device, &semaphoreInfo, nullptr, &acquired);
    if (result != VK_SUCCESS) return fail(22, "vkCreateSemaphore", result);
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
    VkFence fence = nullptr;
    result = createFence(device, &fenceInfo, nullptr, &fence);
    if (result != VK_SUCCESS) return fail(22, "vkCreateFence", result);

    for (int frame = 0; frame < 2; ++frame) {
        uint32_t index = 0;
        result = acquireNextImage(device, swapchain, 1000000000ull, acquired, nullptr, &index);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return fail(23, "vkAcquireNextImageKHR", result);
        // The pool has no VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, so the command buffer cannot be
        // reset individually; resetting the pool is what returns it to the initial state. Without this the
        // second vkBeginCommandBuffer is called on an executable buffer, which the runtime refuses.
        if (frame) {
            result = resetCommandPool(device, pool, 0);
            if (result != VK_SUCCESS) return fail(23, "vkResetCommandPool", result);
        }
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, 0, nullptr};
        result = beginCommandBuffer(commandBuffer, &beginInfo);
        if (result != VK_SUCCESS) return fail(23, "vkBeginCommandBuffer", result);
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageMemoryBarrier toColor{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, 0,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 0, images[index], range};
        cmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &toColor);
        VkClearValue clear{};
        clear.color.r = 0.10f; clear.color.g = 0.45f; clear.color.b = 0.85f; clear.color.a = 1.0f;
        VkRenderPassBeginInfo beginPass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr, renderPass,
            framebuffers[index], {0, 0, extent}, 1, &clear};
        cmdBeginRenderPass(commandBuffer, &beginPass, VK_SUBPASS_CONTENTS_INLINE);
        cmdEndRenderPass(commandBuffer);
        VkImageMemoryBarrier toPresent = toColor;
        toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toPresent.dstAccessMask = 0;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        cmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);
        result = endCommandBuffer(commandBuffer);
        if (result != VK_SUCCESS) return fail(23, "vkEndCommandBuffer", result);
        VkFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 1, &acquired, &waitStage, 1, &commandBuffer, 0, nullptr};
        result = queueSubmit(queue, 1, &submit, fence);
        if (result != VK_SUCCESS) return fail(24, "vkQueueSubmit", result);
        result = waitForFences(device, 1, &fence, 1, 1000000000ull);
        if (result != VK_SUCCESS) return fail(24, "vkWaitForFences", result);
        resetFences(device, 1, &fence);
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr, 0, nullptr, 1, &swapchain, &index, nullptr};
        result = queuePresent(queue, &present);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return fail(24, "vkQueuePresentKHR", result);
        {char line[160]; char* p = append(line, "PLAY-VULKAN-DZN STAGE frame-presented index=");
         p = appendUInt(p, index); p = append(p, " frame="); p = appendInt(p, frame);
         p = append(p, "\n"); *p = 0; emit(line);}
    }
    result = queueWaitIdle(queue);
    if (result != VK_SUCCESS) return fail(25, "vkQueueWaitIdle", result);
    emit("PLAY-VULKAN-DZN PASS frame-presented\n");
    return 42;
}
}
