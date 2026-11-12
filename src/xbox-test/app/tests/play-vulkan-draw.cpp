// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that draws with Vulkan through Play's routes, the
// way a Vulkan game starts up (research/VULKAN-COVERAGE.md). The vulkan-dzn
// fixture proves instance/device/swapchain; this one proves the driver renders:
// a graphics pipeline from the checked-in SPIR-V (tests/vulkan_spirv.h), a
// vertex buffer, one triangle into an offscreen target, a copy to a host-
// visible buffer and a pixel check, so a driver that answers every query but
// draws nothing fails here rather than passing. Then the API surface games
// build on: compute dispatch with a push constant, a timeline semaphore
// signalled and waited by value, synchronization2 barriers, and dynamic
// rendering (no render pass, the format chain instead). Exit codes:
//   42  every stage passed, the triangle read back where it should, compute
//       doubled the buffer, and the timeline/sync2 stages passed
//   43  BLOCKED (never a pass): the pipeline ran but the pixels did not read
//       back as drawn — the host drew nothing the guest could read
//   other  a stage before that failed
//
// The Vulkan ABI is hand-declared: mingw-w64 ships no vulkan.h, and the fixture
// must not depend on the loader's own headers (it is a guest, not a driver).
// Every value below is cross-checked against the pinned Vulkan-Headers
// (vulkan_core.h 1.4.313); the SPIR-V modules are SPIR-V 1.0 so the fixture
// also runs against a Vulkan 1.0 driver.
#include <windows.h>
#include <stdint.h>
#include "vulkan_spirv.h"

extern "C" {
// No CRT: the compiler lowers struct copies to these libc names (the other
// fixtures carry the same definitions).
void* memset(void* d, int c, size_t n) {
    auto p = (unsigned char*)d; while (n--)*p++ = (unsigned char)c; return d;
}
void* memcpy(void* d, const void* s, size_t n) {
    auto a = (unsigned char*)d; auto b = (const unsigned char*)s; while (n--)*a++ = *b++; return d;
}

int vk_draw_anchor;
void* vk_draw_relocation = &vk_draw_anchor;

// The compiler emits a stack-probe call for the fixture's large locals; with
// /nodefaultlib there is no runtime to carry it (the other fixtures do the same).
extern "C" void ___chkstk_ms(void) {}

static void emit(const char* text) {
    DWORD written = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, text, (DWORD)lstrlenA(text), &written, nullptr);
    // A packaged process has no stdout handle; Play routes this into the probe
    // log, which is the same record the verifier reads.
    OutputDebugStringA(text);
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

typedef int32_t VkResult;
typedef uint32_t VkFlags;
typedef uint64_t VkDeviceSize;
typedef struct VkInstance_T* VkInstance;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkDevice_T* VkDevice;
typedef struct VkQueue_T* VkQueue;
typedef struct VkCommandBuffer_T* VkCommandBuffer;
typedef struct VkSemaphore_T* VkSemaphore;
typedef struct VkFence_T* VkFence;
typedef struct VkImage_T* VkImage;
typedef struct VkImageView_T* VkImageView;
typedef struct VkBuffer_T* VkBuffer;
typedef struct VkPipeline_T* VkPipeline;
typedef struct VkPipelineLayout_T* VkPipelineLayout;
typedef struct VkShaderModule_T* VkShaderModule;
typedef struct VkCommandPool_T* VkCommandPool;
typedef struct VkDescriptorSetLayout_T* VkDescriptorSetLayout;
typedef struct VkDescriptorPool_T* VkDescriptorPool;
typedef struct VkDescriptorSet_T* VkDescriptorSet;
typedef struct VkDeviceMemory_T* VkDeviceMemory;
typedef struct VkAllocationCallbacks VkAllocationCallbacks;

enum { VK_SUCCESS = 0 };
enum {
    VK_STRUCTURE_TYPE_APPLICATION_INFO = 0, VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1,
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO = 8,
    VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO = 9, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO = 12,
    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO = 14, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO = 16,
    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO = 18,
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO = 19,
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO = 20,
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO = 22,
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO = 23,
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO = 24,
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO = 26,
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO = 27,
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO = 28,
    VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO = 29,
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO = 30,
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO = 32,
    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO = 33,
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO = 34,
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET = 35,
    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO = 39,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO = 40,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO = 42, VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO = 15,
    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO = 2, VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO = 3,
    VK_STRUCTURE_TYPE_SUBMIT_INFO = 4,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 = 1000059000,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 = 1000059001,
    VK_STRUCTURE_TYPE_RENDERING_INFO = 1000044000,
    VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO = 1000044001,
    VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO = 1000044002,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES = 1000044003,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES = 1000207000,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES = 1000207001,
    VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO = 1000207002,
    VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO = 1000207004,
    VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO = 1000207005,
    VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 = 1000314000,
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 = 1000314002,
    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 = 1000314001,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES = 1000314007,
    VK_STRUCTURE_TYPE_DEPENDENCY_INFO = 1000314003,
    VK_STRUCTURE_TYPE_SUBMIT_INFO_2 = 1000314004,
    VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO = 1000314005,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO = 1000314006,
};
enum { VK_API_VERSION_1_0 = (1u << 22), VK_API_VERSION_1_1 = (2u << 22), VK_API_VERSION_1_2 = (3u << 22), VK_API_VERSION_1_3 = (4u << 22) };
enum { VK_QUEUE_GRAPHICS_BIT = 0x1, VK_QUEUE_COMPUTE_BIT = 0x2 };
enum { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT = 0x1, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT = 0x400,
       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT = 0x800, VK_PIPELINE_STAGE_HOST_BIT = 0x2000 };
enum { VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT = 0x100, VK_ACCESS_SHADER_WRITE_BIT = 0x100000,
       VK_ACCESS_HOST_READ_BIT = 0x100 };
// synchronization2 stage/access masks (64-bit).
enum { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT = 0x00000800ull, VK_PIPELINE_STAGE_2_COPY_BIT = 0x100000000ull,
       VK_PIPELINE_STAGE_2_HOST_BIT = 0x00004000ull };
enum { VK_ACCESS_2_NONE = 0ull, VK_ACCESS_2_SHADER_WRITE_BIT = 0x00000040ull,
       VK_ACCESS_2_TRANSFER_READ_BIT = 0x00000800ull, VK_ACCESS_2_HOST_READ_BIT = 0x00002000ull };
enum { VK_DEPENDENCY_BY_REGION_BIT = 0x1 };
enum { VK_PIPELINE_BIND_POINT_GRAPHICS = 0, VK_PIPELINE_BIND_POINT_COMPUTE = 1 };
enum { VK_COMMAND_BUFFER_LEVEL_PRIMARY = 0, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT = 0x4 };
enum { VK_FORMAT_R8G8B8A8_UNORM = 37, VK_FORMAT_R32G32_SFLOAT = 103, VK_FORMAT_R32G32B32A32_SFLOAT = 109 };
enum { VK_DYNAMIC_STATE_VIEWPORT = 0, VK_DYNAMIC_STATE_SCISSOR = 1 };
enum { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST = 3 };
enum { VK_SHARING_MODE_EXCLUSIVE = 0 };
enum { VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT = 0x2, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT = 0x4 };
enum { VK_BUFFER_USAGE_TRANSFER_SRC_BIT = 0x1, VK_BUFFER_USAGE_TRANSFER_DST_BIT = 0x2,
       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT = 0x20, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT = 0x80 };
enum { VK_IMAGE_TYPE_2D = 1, VK_IMAGE_TILING_OPTIMAL = 0 };
enum { VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT = 0x10, VK_IMAGE_USAGE_TRANSFER_SRC_BIT = 0x1 };
enum { VK_IMAGE_LAYOUT_UNDEFINED = 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL = 2,
       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL = 6 };
enum { VK_ATTACHMENT_LOAD_OP_CLEAR = 1, VK_ATTACHMENT_STORE_OP_STORE = 0 };
enum { VK_IMAGE_ASPECT_COLOR_BIT = 0x1 };
enum { VK_SHADER_STAGE_VERTEX_BIT = 0x1, VK_SHADER_STAGE_FRAGMENT_BIT = 0x10, VK_SHADER_STAGE_COMPUTE_BIT = 0x20 };
enum { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER = 7 };
enum { VK_SAMPLE_COUNT_1_BIT = 0x1 };
enum { VK_COLOR_COMPONENT_R_BIT = 0x1, VK_COLOR_COMPONENT_G_BIT = 0x2,
       VK_COLOR_COMPONENT_B_BIT = 0x4, VK_COLOR_COMPONENT_A_BIT = 0x8 };
enum { VK_POLYGON_MODE_FILL = 0, VK_CULL_MODE_NONE = 0 };
enum { VK_WHOLE_SIZE = (~0ull) };
enum { VK_SEMAPHORE_TYPE_TIMELINE = 1 };
enum { VK_VERTEX_INPUT_RATE_VERTEX = 0 };

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
typedef struct VkExtent3D_ { uint32_t width, height, depth; } VkExtent3D;
// Only the fields the fixture reads; the rest of the real struct (limits,
// sparse properties, buffer/queue limits, name strings) is left out on purpose
// — the fixture never touches it.
typedef struct VkPhysicalDeviceProperties_ {
    uint32_t apiVersion, driverVersion, vendorID, deviceID, deviceType;
    char pad[256 + 8 * 256]; // deviceName[256] + pipelineCacheUUID etc.
} VkPhysicalDeviceProperties;
typedef struct VkQueueFamilyProperties_ {
    VkFlags queueFlags; uint32_t queueCount, timestampValidBits; VkExtent3D minImageTransferGranularity;
} VkQueueFamilyProperties;
typedef struct VkPhysicalDeviceMemoryProperties_ {
    uint32_t memoryTypeCount;
    struct { VkFlags propertyFlags; uint32_t heapIndex; } memoryTypes[32];
    uint32_t memoryHeapCount;
    struct { VkDeviceSize size; VkFlags flags; } memoryHeaps[16];
} VkPhysicalDeviceMemoryProperties;
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
typedef struct VkPhysicalDeviceFeatures2_ {
    int32_t sType; void* pNext; VkFlags features[55];   // VkPhysicalDeviceFeatures: 55 x VkBool32
} VkPhysicalDeviceFeatures2;
typedef struct VkPhysicalDeviceTimelineSemaphoreFeatures_ {
    int32_t sType; void* pNext; int32_t timelineSemaphore;
} VkPhysicalDeviceTimelineSemaphoreFeatures;
typedef struct VkPhysicalDeviceSynchronization2Features_ {
    int32_t sType; void* pNext; int32_t synchronization2;
} VkPhysicalDeviceSynchronization2Features;
typedef struct VkPhysicalDeviceDynamicRenderingFeatures_ {
    int32_t sType; void* pNext; int32_t dynamicRendering;
} VkPhysicalDeviceDynamicRenderingFeatures;
typedef struct VkPhysicalDeviceTimelineSemaphoreProperties_ {
    int32_t sType; void* pNext; uint64_t maxTimelineSemaphoreValueDifference;
} VkPhysicalDeviceTimelineSemaphoreProperties;
typedef struct VkBufferCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; VkDeviceSize size; VkFlags usage;
    int32_t sharingMode; uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices;
} VkBufferCreateInfo;
typedef struct VkImageCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; int32_t imageType; int32_t format;
    VkExtent3D extent; uint32_t mipLevels, arrayLayers; int32_t samples; int32_t tiling;
    VkFlags usage; int32_t sharingMode; uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices;
    int32_t initialLayout;
} VkImageCreateInfo;
typedef struct VkMemoryRequirements_ { VkDeviceSize size, alignment; uint32_t memoryTypeBits; } VkMemoryRequirements;
typedef struct VkMemoryAllocateInfo_ {
    int32_t sType; const void* pNext; VkDeviceSize allocationSize; uint32_t memoryTypeIndex;
} VkMemoryAllocateInfo;
typedef struct VkShaderModuleCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; size_t codeSize; const uint32_t* pCode;
} VkShaderModuleCreateInfo;
typedef struct VkDescriptorSetLayoutBinding_ {
    uint32_t binding; int32_t descriptorType; uint32_t descriptorCount;
    int32_t stageFlags; const void* pImmutableSamplers;
} VkDescriptorSetLayoutBinding;
typedef struct VkDescriptorSetLayoutCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags;
    uint32_t bindingCount; const VkDescriptorSetLayoutBinding* pBindings;
} VkDescriptorSetLayoutCreateInfo;
typedef struct VkDescriptorPoolSize_ { int32_t type; uint32_t descriptorCount; } VkDescriptorPoolSize;
typedef struct VkDescriptorPoolCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; uint32_t maxSets;
    uint32_t poolSizeCount; const VkDescriptorPoolSize* pPoolSizes;
} VkDescriptorPoolCreateInfo;
typedef struct VkDescriptorSetAllocateInfo_ {
    int32_t sType; const void* pNext; VkDescriptorPool descriptorPool;
    uint32_t descriptorSetCount; const VkDescriptorSetLayout* pSetLayouts;
} VkDescriptorSetAllocateInfo;
typedef struct VkDescriptorBufferInfo_ { VkBuffer buffer; VkDeviceSize offset, range; } VkDescriptorBufferInfo;
typedef struct VkWriteDescriptorSet_ {
    int32_t sType; const void* pNext; VkDescriptorSet dstSet; uint32_t dstBinding;
    uint32_t dstArrayElement; uint32_t descriptorCount; int32_t descriptorType;
    const void* pImageInfo; const VkDescriptorBufferInfo* pBufferInfo; const void* pTexelBufferView;
} VkWriteDescriptorSet;
typedef struct VkPushConstantRange_ { int32_t stageFlags; uint32_t offset, size; } VkPushConstantRange;
typedef struct VkPipelineLayoutCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags;
    uint32_t setLayoutCount; const VkDescriptorSetLayout* pSetLayouts;
    uint32_t pushConstantRangeCount; const VkPushConstantRange* pPushConstantRanges;
} VkPipelineLayoutCreateInfo;
typedef struct VkPipelineShaderStageCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; int32_t stage;
    VkShaderModule module; const char* pName; const void* pSpecializationInfo;
} VkPipelineShaderStageCreateInfo;
typedef struct VkVertexInputBindingDescription_ {
    uint32_t binding, stride; int32_t inputRate;
} VkVertexInputBindingDescription;
typedef struct VkVertexInputAttributeDescription_ {
    uint32_t location, binding; int32_t format; uint32_t offset;
} VkVertexInputAttributeDescription;
typedef struct VkPipelineVertexInputStateCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags;
    uint32_t vertexBindingDescriptionCount; const VkVertexInputBindingDescription* pVertexBindingDescriptions;
    uint32_t vertexAttributeDescriptionCount; const VkVertexInputAttributeDescription* pVertexAttributeDescriptions;
} VkPipelineVertexInputStateCreateInfo;
typedef struct VkPipelineInputAssemblyStateCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; int32_t topology; int32_t primitiveRestartEnable;
} VkPipelineInputAssemblyStateCreateInfo;
typedef struct VkViewport_ { float x, y, width, height, minDepth, maxDepth; } VkViewport;
typedef struct VkRect2D_ { struct { int32_t x, y; } offset; struct { uint32_t width, height; } extent; } VkRect2D;
typedef struct VkPipelineViewportStateCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; uint32_t viewportCount; const VkViewport* pViewports;
    uint32_t scissorCount; const VkRect2D* pScissors;
} VkPipelineViewportStateCreateInfo;
typedef struct VkPipelineRasterizationStateCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; int32_t depthClampEnable; int32_t rasterizerDiscardEnable;
    int32_t polygonMode; VkFlags cullMode; int32_t frontFace; int32_t depthBiasEnable;
    float depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor, lineWidth;
} VkPipelineRasterizationStateCreateInfo;
typedef struct VkPipelineMultisampleStateCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; int32_t rasterizationSamples; int32_t sampleShadingEnable;
    float minSampleShading; const VkFlags* pSampleMask; int32_t alphaToCoverageEnable, alphaToOneEnable;
} VkPipelineMultisampleStateCreateInfo;
typedef struct VkPipelineColorBlendAttachmentState_ {
    int32_t blendEnable; VkFlags srcColorBlendFactor, dstColorBlendFactor, colorBlendOp;
    VkFlags srcAlphaBlendFactor, dstAlphaBlendFactor, alphaBlendOp; VkFlags colorWriteMask;
} VkPipelineColorBlendAttachmentState;
typedef struct VkPipelineColorBlendStateCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; int32_t logicOpEnable; int32_t logicOp;
    uint32_t attachmentCount; const VkPipelineColorBlendAttachmentState* pAttachments;
    float blendConstants[4];
} VkPipelineColorBlendStateCreateInfo;
typedef struct VkPipelineDynamicStateCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; uint32_t dynamicStateCount; const int32_t* pDynamicStates;
} VkPipelineDynamicStateCreateInfo;
typedef struct VkGraphicsPipelineCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags;
    uint32_t stageCount; const VkPipelineShaderStageCreateInfo* pStages;
    const VkPipelineVertexInputStateCreateInfo* pVertexInputState;
    const VkPipelineInputAssemblyStateCreateInfo* pInputAssemblyState;
    const void* pTessellationState; const VkPipelineViewportStateCreateInfo* pViewportState;
    const VkPipelineRasterizationStateCreateInfo* pRasterizationState;
    const VkPipelineMultisampleStateCreateInfo* pMultisampleState; const void* pDepthStencilState;
    const VkPipelineColorBlendStateCreateInfo* pColorBlendState;
    const VkPipelineDynamicStateCreateInfo* pDynamicState;
    VkPipelineLayout layout; void* renderPass; uint32_t subpass;
    VkPipeline basePipelineHandle; int32_t basePipelineIndex;
} VkGraphicsPipelineCreateInfo;
typedef struct VkComputePipelineCreateInfo_ {
    int32_t sType; const void* pNext; VkFlags flags;
    VkPipelineShaderStageCreateInfo stage; VkPipelineLayout layout;
    VkPipeline basePipelineHandle; int32_t basePipelineIndex;
} VkComputePipelineCreateInfo;
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
typedef struct VkRenderingAttachmentInfo_ {
    int32_t sType; const void* pNext; VkImageView imageView; int32_t imageLayout;
    int32_t resolveMode; VkImageView resolveImageView; int32_t resolveImageLayout;
    int32_t loadOp, storeOp; VkClearValue clearValue;
} VkRenderingAttachmentInfo;
typedef struct VkPipelineRenderingCreateInfo_ {
    int32_t sType; const void* pNext; uint32_t viewMask; uint32_t colorAttachmentCount;
    const int32_t* pColorAttachmentFormats; int32_t depthAttachmentFormat; int32_t stencilAttachmentFormat;
} VkPipelineRenderingCreateInfo;
typedef struct VkImageSubresourceRange_ {
    VkFlags aspectMask; uint32_t baseMipLevel, levelCount, baseArrayLayer, layerCount;
} VkImageSubresourceRange;
typedef struct VkImageMemoryBarrier2_ {
    int32_t sType; const void* pNext; uint64_t srcStageMask, srcAccessMask;
    uint64_t dstStageMask, dstAccessMask; int32_t oldLayout, newLayout;
    uint32_t srcQueueFamilyIndex, dstQueueFamilyIndex; VkImage image;
    VkImageSubresourceRange subresourceRange;
} VkImageMemoryBarrier2;
typedef struct VkRenderingInfo_ {
    int32_t sType; const void* pNext; VkFlags flags;
    VkRect2D renderArea;
    uint32_t layerCount, viewMask, colorAttachmentCount;
    const VkRenderingAttachmentInfo* pColorAttachments;
    const VkRenderingAttachmentInfo* pDepthAttachment; const VkRenderingAttachmentInfo* pStencilAttachment;
} VkRenderingInfo;
typedef struct VkBufferMemoryBarrier2_ {
    int32_t sType; const void* pNext; uint64_t srcStageMask, srcAccessMask;
    uint64_t dstStageMask, dstAccessMask; uint32_t srcQueueFamilyIndex, dstQueueFamilyIndex;
    VkBuffer buffer; VkDeviceSize offset, size;
} VkBufferMemoryBarrier2;
typedef struct VkDependencyInfo_ {
    int32_t sType; const void* pNext; VkFlags dependencyFlags;
    uint32_t memoryBarrierCount; const void* pMemoryBarriers;
    uint32_t bufferMemoryBarrierCount; const VkBufferMemoryBarrier2* pBufferMemoryBarriers;
    uint32_t imageMemoryBarrierCount; const VkImageMemoryBarrier2* pImageMemoryBarriers;
} VkDependencyInfo;
typedef struct VkSemaphoreSubmitInfo_ {
    int32_t sType; const void* pNext; VkSemaphore semaphore; uint64_t value;
    uint64_t stageMask; uint32_t deviceIndex;
} VkSemaphoreSubmitInfo;
typedef struct VkCommandBufferSubmitInfo_ {
    int32_t sType; const void* pNext; VkCommandBuffer commandBuffer; uint32_t deviceMask;
} VkCommandBufferSubmitInfo;
typedef struct VkSubmitInfo2_ {
    int32_t sType; const void* pNext; VkFlags flags;
    uint32_t waitSemaphoreInfoCount; const VkSemaphoreSubmitInfo* pWaitSemaphoreInfos;
    uint32_t commandBufferInfoCount; const VkCommandBufferSubmitInfo* pCommandBufferInfos;
    uint32_t signalSemaphoreInfoCount; const VkSemaphoreSubmitInfo* pSignalSemaphoreInfos;
} VkSubmitInfo2;
typedef struct VkSubmitInfo_ {
    int32_t sType; const void* pNext;
    uint32_t waitSemaphoreCount; const VkSemaphore* pWaitSemaphores; const VkFlags* pWaitDstStageMask;
    uint32_t commandBufferCount; const VkCommandBuffer* pCommandBuffers;
    uint32_t signalSemaphoreCount; const VkSemaphore* pSignalSemaphores;
} VkSubmitInfo;
typedef struct VkFenceCreateInfo_ { int32_t sType; const void* pNext; VkFlags flags; } VkFenceCreateInfo;
typedef struct VkSemaphoreCreateInfo_ { int32_t sType; const void* pNext; VkFlags flags; } VkSemaphoreCreateInfo;
typedef struct VkSemaphoreTypeCreateInfo_ {
    int32_t sType; const void* pNext; int32_t semaphoreType; uint64_t initialValue;
} VkSemaphoreTypeCreateInfo;
typedef struct VkSemaphoreWaitInfo_ {
    int32_t sType; const void* pNext; VkFlags flags; uint32_t semaphoreCount;
    const VkSemaphore* pSemaphores; const uint64_t* pValues;
} VkSemaphoreWaitInfo;
typedef struct VkSemaphoreSignalInfo_ {
    int32_t sType; const void* pNext; VkSemaphore semaphore; uint64_t value;
} VkSemaphoreSignalInfo;
typedef struct VkBufferCopy_ { VkDeviceSize srcOffset, dstOffset, size; } VkBufferCopy;

typedef VkResult (__stdcall *PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
typedef void* (__stdcall *PFN_vkGetInstanceProcAddr)(VkInstance, const char*);
typedef void* (__stdcall *PFN_vkGetDeviceProcAddr)(VkDevice, const char*);
typedef VkResult (__stdcall *PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef void (__stdcall *PFN_vkGetPhysicalDeviceMemoryProperties)(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*);
typedef void (__stdcall *PFN_vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
typedef void (__stdcall *PFN_vkGetPhysicalDeviceFeatures2)(VkPhysicalDevice, void*);
typedef void (__stdcall *PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice, VkPhysicalDeviceProperties*);
typedef VkResult (__stdcall *PFN_vkCreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*);
typedef void (__stdcall *PFN_vkGetDeviceQueue)(VkDevice, uint32_t, uint32_t, VkQueue*);
typedef VkResult (__stdcall *PFN_vkCreateBuffer)(VkDevice, const VkBufferCreateInfo*, const VkAllocationCallbacks*, VkBuffer*);
typedef void (__stdcall *PFN_vkGetBufferMemoryRequirements)(VkDevice, VkBuffer, VkMemoryRequirements*);
typedef VkResult (__stdcall *PFN_vkAllocateMemory)(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory*);
typedef VkResult (__stdcall *PFN_vkBindBufferMemory)(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);
typedef VkResult (__stdcall *PFN_vkMapMemory)(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkFlags, void**);
typedef VkResult (__stdcall *PFN_vkCreateImage)(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*);
typedef void (__stdcall *PFN_vkGetImageMemoryRequirements)(VkDevice, VkImage, VkMemoryRequirements*);
typedef VkResult (__stdcall *PFN_vkBindImageMemory)(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize);
typedef VkResult (__stdcall *PFN_vkCreateImageView)(VkDevice, const void*, const VkAllocationCallbacks*, VkImageView*);
typedef VkResult (__stdcall *PFN_vkCreateShaderModule)(VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule*);
typedef VkResult (__stdcall *PFN_vkCreateDescriptorSetLayout)(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout*);
typedef VkResult (__stdcall *PFN_vkCreateDescriptorPool)(VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool*);
typedef VkResult (__stdcall *PFN_vkAllocateDescriptorSets)(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*);
typedef void (__stdcall *PFN_vkUpdateDescriptorSets)(VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const void*);
typedef VkResult (__stdcall *PFN_vkCreatePipelineLayout)(VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout*);
typedef VkResult (__stdcall *PFN_vkCreateGraphicsPipelines)(VkDevice, void*, uint32_t, const VkGraphicsPipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*);
typedef VkResult (__stdcall *PFN_vkCreateComputePipelines)(VkDevice, void*, uint32_t, const VkComputePipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*);
typedef VkResult (__stdcall *PFN_vkCreateCommandPool)(VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool*);
typedef VkResult (__stdcall *PFN_vkAllocateCommandBuffers)(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*);
typedef VkResult (__stdcall *PFN_vkBeginCommandBuffer)(VkCommandBuffer, const VkCommandBufferBeginInfo*);
typedef void (__stdcall *PFN_vkCmdBindPipeline)(VkCommandBuffer, int32_t, VkPipeline);
typedef void (__stdcall *PFN_vkCmdBindVertexBuffers)(VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*);
typedef void (__stdcall *PFN_vkCmdDraw)(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void (__stdcall *PFN_vkCmdDispatch)(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
typedef void (__stdcall *PFN_vkCmdPushConstants)(VkCommandBuffer, VkPipelineLayout, int32_t, uint32_t, uint32_t, const void*);
typedef void (__stdcall *PFN_vkCmdBindDescriptorSets)(VkCommandBuffer, int32_t, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*);
typedef void (__stdcall *PFN_vkCmdSetViewport)(VkCommandBuffer, uint32_t, uint32_t, const VkViewport*);
typedef void (__stdcall *PFN_vkCmdSetScissor)(VkCommandBuffer, uint32_t, uint32_t, const VkRect2D*);
typedef void (__stdcall *PFN_vkCmdPipelineBarrier2)(VkCommandBuffer, const VkDependencyInfo*);
typedef void (__stdcall *PFN_vkCmdBeginRendering)(VkCommandBuffer, const VkRenderingInfo*);
typedef void (__stdcall *PFN_vkCmdEndRendering)(VkCommandBuffer);
typedef void (__stdcall *PFN_vkCmdCopyBuffer)(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy*);
typedef VkResult (__stdcall *PFN_vkEndCommandBuffer)(VkCommandBuffer);
typedef VkResult (__stdcall *PFN_vkQueueSubmit)(VkQueue, uint32_t, const VkSubmitInfo*, VkFence);
typedef VkResult (__stdcall *PFN_vkQueueSubmit2)(VkQueue, uint32_t, const VkSubmitInfo2*, VkFence);
typedef VkResult (__stdcall *PFN_vkQueueWaitIdle)(VkQueue);
typedef VkResult (__stdcall *PFN_vkCreateSemaphore)(VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore*);
typedef VkResult (__stdcall *PFN_vkCreateFence)(VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence*);
typedef VkResult (__stdcall *PFN_vkWaitForFences)(VkDevice, uint32_t, const VkFence*, int32_t, uint64_t);
typedef VkResult (__stdcall *PFN_vkResetFences)(VkDevice, uint32_t, const VkFence*);
typedef VkResult (__stdcall *PFN_vkWaitSemaphores)(VkDevice, const VkSemaphoreWaitInfo*, uint64_t);
typedef VkResult (__stdcall *PFN_vkSignalSemaphore)(VkDevice, const VkSemaphoreSignalInfo*);

static int fail(int code, const char* stage, VkResult result) {
    char line[192]; char* p = append(line, "PLAY-VULKAN-DRAW FAIL ");
    p = append(p, stage); p = append(p, " result="); p = appendInt(p, result);
    p = append(p, "\n"); *p = 0;
    emit(line);
    return code;
}

// Host-visible + host-coherent memory type, or UINT32_MAX when the driver
// splits them (the fixture then takes visible without coherent and flushes
// implicitly through QueueWaitIdle).
static uint32_t findMemoryType(const VkPhysicalDeviceMemoryProperties* mem, VkFlags want, VkFlags avoid) {
    for (uint32_t i = 0; i < mem->memoryTypeCount; ++i)
        if ((mem->memoryTypes[i].propertyFlags & want) == want &&
            !(mem->memoryTypes[i].propertyFlags & avoid))
            return i;
    return UINT32_MAX;
}

int vk_draw_entry() {
    HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
    if (!loader) return fail(11, "LoadLibraryW(vulkan-1.dll)", 0);
    auto gipa = (PFN_vkGetInstanceProcAddr)(void* (__stdcall*)(VkInstance, const char*))GetProcAddress(loader, "vkGetInstanceProcAddr");
    if (!gipa) return fail(11, "GetProcAddress(vkGetInstanceProcAddr)", 0);
    auto createInstance = (PFN_vkCreateInstance)GetProcAddress(loader, "vkCreateInstance");
    if (!createInstance) return fail(11, "GetProcAddress(vkCreateInstance)", 0);

    const char* extensions[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
    // The instance asks for 1.3 (the loader caps to what it and the ICD
    // support; the spec forbids failing a higher request) so the promoted
    // core entry points — features2 chaining at device creation, the 1.3
    // command exports — exist. A driver that refuses the request falls back
    // to a 1.0 instance and the fixture degrades with it.
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "play-vulkan-draw", 1, "play", 1, VK_API_VERSION_1_3};
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app, 0, nullptr, 2, extensions};
    VkInstance instance = nullptr;
    VkResult result = createInstance(&instanceInfo, nullptr, &instance);
    if (result != VK_SUCCESS || !instance) {
        app.apiVersion = VK_API_VERSION_1_0;
        result = createInstance(&instanceInfo, nullptr, &instance);
    }
    if (result != VK_SUCCESS || !instance) return fail(12, "vkCreateInstance", result);
    emit("PLAY-VULKAN-DRAW STAGE instance-created\n");

    auto enumerate = (PFN_vkEnumeratePhysicalDevices)gipa(instance, "vkEnumeratePhysicalDevices");
    auto getMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)gipa(instance, "vkGetPhysicalDeviceMemoryProperties");
    auto queueFamilies = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    auto getFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)gipa(instance, "vkGetPhysicalDeviceFeatures2");
    auto createDevice = (PFN_vkCreateDevice)gipa(instance, "vkCreateDevice");
    if (!enumerate || !getMemoryProperties || !queueFamilies || !createDevice)
        return fail(12, "instance proc addrs", 0);
    auto getProperties = (PFN_vkGetPhysicalDeviceProperties)gipa(instance, "vkGetPhysicalDeviceProperties");
    if (!getProperties) return fail(12, "vkGetPhysicalDeviceProperties", 0);

    uint32_t count = 0;
    result = enumerate(instance, &count, nullptr);
    if (result != VK_SUCCESS || !count) return fail(13, "vkEnumeratePhysicalDevices", result);
    VkPhysicalDevice devices[8]{};
    uint32_t listed = count < 8 ? count : 8;
    result = enumerate(instance, &listed, devices);
    if (result != VK_SUCCESS || !listed) return fail(13, "vkEnumeratePhysicalDevices(array)", result);
    VkPhysicalDevice physical = devices[0];
    // The apiVersion a game would read off the physical device before deciding
    // what to request (the instance itself stays 1.0 — the loader caps, the
    // device does not).
    VkPhysicalDeviceProperties props{};
    getProperties(physical, &props);
    uint32_t physicalApiVersion = props.apiVersion;

    // Feature probes a game does before choosing its path: the three features
    // this fixture exercises, chained off one vkGetPhysicalDeviceFeatures2.
    // A driver that lacks one is recorded here and the stages that need it
    // report BLOCKED below — never a silent skip.
    int32_t hasTimeline = 0, hasSync2 = 0, hasDynamicRendering = 0;
    if (getFeatures2) {
        VkPhysicalDeviceTimelineSemaphoreFeatures t{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES, nullptr, 0};
        VkPhysicalDeviceSynchronization2Features s{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES, &t, 0};
        VkPhysicalDeviceDynamicRenderingFeatures d{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES, &s, 0};
        VkPhysicalDeviceFeatures2 top{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &d, {0}};
        getFeatures2(physical, &top);
        hasTimeline = t.timelineSemaphore; hasSync2 = s.synchronization2; hasDynamicRendering = d.dynamicRendering;
    }
    {char line[160]; char* p = append(line, "PLAY-VULKAN-DRAW STAGE features timeline=");
     p = appendInt(p, hasTimeline); p = append(p, " sync2="); p = appendInt(p, hasSync2);
     p = append(p, " dynamicRendering="); p = appendInt(p, hasDynamicRendering); p = append(p, "\n"); *p = 0; emit(line);}

    // One queue family serving both graphics and compute (dzn reports 4
    // families; llvmpipe one combined family — both shapes must find one).
    uint32_t familyCount = 0;
    queueFamilies(physical, &familyCount, nullptr);
    if (!familyCount) return 16;
    VkQueueFamilyProperties families[16]{};
    uint32_t familiesRead = familyCount < 16 ? familyCount : 16;
    queueFamilies(physical, &familiesRead, families);
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < familiesRead; ++i)
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) { family = i; break; }
    if (family == UINT32_MAX) return fail(16, "graphics+compute queue family", 0);
    {char line[128]; char* p = append(line, "PLAY-VULKAN-DRAW STAGE queue-family index=");
     p = appendUInt(p, family); p = append(p, "\n"); *p = 0; emit(line);}

    // Enable the three features through pNext when the driver reports them,
    // and ask for the highest apiVersion the physical device actually
    // supports: core-1.1+ commands are exported by a device only at the
    // apiVersion it was created with (winevulkan enforces this strictly).
    // The device's apiVersion rides in the instance's app info; a device
    // create info takes no app info of its own (the pEnabledFeatures slot
    // must be null when the features arrive through pNext).
    uint32_t wantApi = VK_API_VERSION_1_0;
    if (hasDynamicRendering || hasSync2) wantApi = VK_API_VERSION_1_3;
    else if (hasTimeline) wantApi = VK_API_VERSION_1_2;
    else if (physicalApiVersion >= VK_API_VERSION_1_1) wantApi = VK_API_VERSION_1_1;
    VkPhysicalDeviceTimelineSemaphoreFeatures t{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES, nullptr, hasTimeline};
    VkPhysicalDeviceSynchronization2Features s{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES, &t, hasSync2};
    VkPhysicalDeviceDynamicRenderingFeatures d{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES, &s, hasDynamicRendering};
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, family, 1, &priority};
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &d, 0, 1, &queueInfo, 0, nullptr, 0, nullptr, nullptr};
    VkDevice device = nullptr;
    result = createDevice(physical, &deviceInfo, nullptr, &device);
    if (result != VK_SUCCESS || !device) return fail(17, "vkCreateDevice", result);
    emit("PLAY-VULKAN-DRAW STAGE device-created\n");

    // vkGetDeviceProcAddr is only guaranteed for a valid instance (the spec
    // allows a loader to return NULL for one); the fixture's first Wine run
    // hit exactly that.
    auto dpa = (PFN_vkGetDeviceProcAddr)gipa(instance, "vkGetDeviceProcAddr");
    if (!dpa) return fail(17, "vkGetDeviceProcAddr", 0);
#define DEV(name) auto name = (PFN_vk##name)dpa(device, "vk" #name)
    DEV(GetDeviceQueue); DEV(CreateBuffer); DEV(GetBufferMemoryRequirements); DEV(AllocateMemory);
    DEV(BindBufferMemory); DEV(MapMemory); DEV(CreateImage); DEV(GetImageMemoryRequirements);
    DEV(BindImageMemory); DEV(CreateImageView); DEV(CreateShaderModule); DEV(CreateDescriptorSetLayout);
    DEV(CreateDescriptorPool); DEV(AllocateDescriptorSets); DEV(UpdateDescriptorSets);
    DEV(CreatePipelineLayout); DEV(CreateGraphicsPipelines); DEV(CreateComputePipelines);
    DEV(CreateCommandPool); DEV(AllocateCommandBuffers); DEV(BeginCommandBuffer);
    DEV(CmdBindPipeline); DEV(CmdBindVertexBuffers); DEV(CmdDraw); DEV(CmdDispatch);
    DEV(CmdPushConstants); DEV(CmdBindDescriptorSets); DEV(CmdSetViewport); DEV(CmdSetScissor);
    DEV(CmdPipelineBarrier2); DEV(CmdBeginRendering); DEV(CmdEndRendering); DEV(CmdCopyBuffer);
    DEV(EndCommandBuffer); DEV(QueueSubmit); DEV(QueueSubmit2); DEV(QueueWaitIdle);
    DEV(CreateSemaphore); DEV(CreateFence); DEV(WaitForFences); DEV(ResetFences);
    DEV(WaitSemaphores); DEV(SignalSemaphore);
#undef DEV
    // Name the first missing entry point: a driver that stops exporting one of
    // these is exactly what the fixture is here to catch, and "device proc
    // addrs" alone does not say which.
    {
        struct { const char* name; void* p; } required[] = {
            {"vkGetDeviceQueue", (void*)GetDeviceQueue},
            {"vkCreateBuffer", (void*)CreateBuffer},
            {"vkGetBufferMemoryRequirements", (void*)GetBufferMemoryRequirements},
            {"vkAllocateMemory", (void*)AllocateMemory},
            {"vkBindBufferMemory", (void*)BindBufferMemory},
            {"vkMapMemory", (void*)MapMemory},
            {"vkCreateImage", (void*)CreateImage},
            {"vkGetImageMemoryRequirements", (void*)GetImageMemoryRequirements},
            {"vkBindImageMemory", (void*)BindImageMemory},
            {"vkCreateImageView", (void*)CreateImageView},
            {"vkCreateShaderModule", (void*)CreateShaderModule},
            {"vkCreateDescriptorSetLayout", (void*)CreateDescriptorSetLayout},
            {"vkCreateDescriptorPool", (void*)CreateDescriptorPool},
            {"vkAllocateDescriptorSets", (void*)AllocateDescriptorSets},
            {"vkUpdateDescriptorSets", (void*)UpdateDescriptorSets},
            {"vkCreatePipelineLayout", (void*)CreatePipelineLayout},
            {"vkCreateGraphicsPipelines", (void*)CreateGraphicsPipelines},
            {"vkCreateComputePipelines", (void*)CreateComputePipelines},
            {"vkCreateCommandPool", (void*)CreateCommandPool},
            {"vkAllocateCommandBuffers", (void*)AllocateCommandBuffers},
            {"vkBeginCommandBuffer", (void*)BeginCommandBuffer},
            {"vkCmdBindPipeline", (void*)CmdBindPipeline},
            {"vkCmdBindVertexBuffers", (void*)CmdBindVertexBuffers},
            {"vkCmdDraw", (void*)CmdDraw},
            {"vkCmdDispatch", (void*)CmdDispatch},
            {"vkCmdPushConstants", (void*)CmdPushConstants},
            {"vkCmdBindDescriptorSets", (void*)CmdBindDescriptorSets},
            {"vkCmdSetViewport", (void*)CmdSetViewport},
            {"vkCmdSetScissor", (void*)CmdSetScissor},
            {"vkCmdPipelineBarrier2", (void*)CmdPipelineBarrier2},
            {"vkCmdBeginRendering", (void*)CmdBeginRendering},
            {"vkCmdEndRendering", (void*)CmdEndRendering},
            {"vkCmdCopyBuffer", (void*)CmdCopyBuffer},
            {"vkEndCommandBuffer", (void*)EndCommandBuffer},
            {"vkQueueSubmit", (void*)QueueSubmit},
            {"vkQueueSubmit2", (void*)QueueSubmit2},
            {"vkQueueWaitIdle", (void*)QueueWaitIdle},
            {"vkCreateSemaphore", (void*)CreateSemaphore},
            {"vkCreateFence", (void*)CreateFence},
            {"vkWaitForFences", (void*)WaitForFences},
            {"vkResetFences", (void*)ResetFences},
            {"vkWaitSemaphores", (void*)WaitSemaphores},
            {"vkSignalSemaphore", (void*)SignalSemaphore},
        };
        for (size_t i = 0; i < sizeof required / sizeof *required; ++i)
            if (!required[i].p) return fail(17, required[i].name, 0);
    }
    VkQueue queue = nullptr;
    GetDeviceQueue(device, family, 0, &queue);
    if (!queue) return fail(17, "vkGetDeviceQueue", 0);

    // One host-visible allocation backs every buffer: vertex data in, compute
    // storage in place, readback out (the way a small game or a tool does it).
    VkPhysicalDeviceMemoryProperties mem{};
    getMemoryProperties(physical, &mem);
    uint32_t memType = findMemoryType(&mem, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
    if (memType == UINT32_MAX) return fail(20, "host-visible memory type", 0);
    VkBuffer vertexBuffer = nullptr, storageBuffer = nullptr, readbackBuffer = nullptr;
    // Three vertices at the pipeline's stride: pos(4 floats) + colour(4), the
    // layout the vertex input below declares and vertices[] above fills.
    VkBufferCreateInfo vbInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, 3 * 8 * 4,
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    if (CreateBuffer(device, &vbInfo, nullptr, &vertexBuffer) != VK_SUCCESS || !vertexBuffer)
        return fail(20, "vkCreateBuffer(vertex)", 0);
    // 64 uints in place: compute reads and writes the same buffer.
    VkBufferCreateInfo sbInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, 64 * 4,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    if (CreateBuffer(device, &sbInfo, nullptr, &storageBuffer) != VK_SUCCESS || !storageBuffer)
        return fail(20, "vkCreateBuffer(storage)", 0);
    VkBufferCreateInfo rbInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, 64 * 4,
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    if (CreateBuffer(device, &rbInfo, nullptr, &readbackBuffer) != VK_SUCCESS || !readbackBuffer)
        return fail(20, "vkCreateBuffer(readback)", 0);
    VkMemoryRequirements req{};
    VkDeviceSize used = 0;
    VkDeviceMemory memory = nullptr;
    // One allocation sized for the largest, each buffer bound at its own
    // required alignment offset inside it.
    VkDeviceSize offsets[3] = {0, 0, 0};
    VkBuffer buffers[3] = {vertexBuffer, storageBuffer, readbackBuffer};
    VkDeviceSize sizes[3] = {vbInfo.size, sbInfo.size, rbInfo.size};
    VkDeviceSize total = 0;
    for (int i = 0; i < 3; ++i) {
        GetBufferMemoryRequirements(device, buffers[i], &req);
        used = (used + req.alignment - 1) & ~(req.alignment - 1);
        offsets[i] = used;
        used += sizes[i];
        total = used;
    }
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, total, memType};
    if (AllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS || !memory)
        return fail(20, "vkAllocateMemory", 0);
    for (int i = 0; i < 3; ++i)
        if (BindBufferMemory(device, buffers[i], memory, offsets[i]) != VK_SUCCESS)
            return fail(20, "vkBindBufferMemory", 0);
    // Write the vertex data and the compute input through the mapped
    // allocation: three vertices (a red triangle), 64 uints starting at 1.
    void* mapped = nullptr;
    if (MapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS || !mapped)
        return fail(20, "vkMapMemory", 0);
    static const float vertices[3][8] = {
        {-0.75f, -0.75f, 0, 1, 1, 0, 0, 1},
        { 0.75f, -0.75f, 0, 1, 1, 0, 0, 1},
        { 0.0f,   0.75f, 0, 1, 1, 0, 0, 1}};
    memcpy(mapped, vertices, sizeof vertices);
    uint32_t* words = (uint32_t*)((char*)mapped + offsets[1]);
    for (uint32_t i = 0; i < 64; ++i) words[i] = i + 1u;
    emit("PLAY-VULKAN-DRAW STAGE buffers-mapped\n");

    VkShaderModule vsModule = nullptr, fsModule = nullptr, csModule = nullptr;
    {VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, sizeof vulkan_vs_spv, (const uint32_t*)vulkan_vs_spv};
     if (CreateShaderModule(device, &info, nullptr, &vsModule) != VK_SUCCESS || !vsModule) return fail(21, "vkCreateShaderModule(vs)", 0);}
    {VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, sizeof vulkan_fs_spv, (const uint32_t*)vulkan_fs_spv};
     if (CreateShaderModule(device, &info, nullptr, &fsModule) != VK_SUCCESS || !fsModule) return fail(21, "vkCreateShaderModule(fs)", 0);}
    {VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, sizeof vulkan_cs_spv, (const uint32_t*)vulkan_cs_spv};
     if (CreateShaderModule(device, &info, nullptr, &csModule) != VK_SUCCESS || !csModule) return fail(21, "vkCreateShaderModule(cs)", 0);}
    emit("PLAY-VULKAN-DRAW STAGE shader-modules\n");

    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &binding};
    VkDescriptorSetLayout setLayout = nullptr;
    if (CreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout) != VK_SUCCESS || !setLayout)
        return fail(22, "vkCreateDescriptorSetLayout", 0);
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 1, &poolSize};
    VkDescriptorPool descPool = nullptr;
    if (CreateDescriptorPool(device, &poolInfo, nullptr, &descPool) != VK_SUCCESS || !descPool)
        return fail(22, "vkCreateDescriptorPool", 0);
    VkDescriptorSetAllocateInfo setAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descPool, 1, &setLayout};
    VkDescriptorSet descSet = nullptr;
    if (AllocateDescriptorSets(device, &setAlloc, &descSet) != VK_SUCCESS || !descSet)
        return fail(22, "vkAllocateDescriptorSets", 0);
    VkDescriptorBufferInfo bufferInfo{storageBuffer, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descSet, 0, 0, 1,
                               VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bufferInfo, nullptr};
    UpdateDescriptorSets(device, 1, &write, 0, nullptr);

    VkPushConstantRange pcRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
    VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &setLayout, 1, &pcRange};
    VkPipelineLayout pipelineLayout = nullptr;
    if (CreatePipelineLayout(device, &plInfo, nullptr, &pipelineLayout) != VK_SUCCESS || !pipelineLayout)
        return fail(22, "vkCreatePipelineLayout", 0);
    emit("PLAY-VULKAN-DRAW STAGE pipeline-layout\n");

    // The offscreen colour target: a 64x64 image the triangle lands in, copied
    // to a host-visible buffer afterwards. Created before the pipeline so the
    // rendering-info chain can name its format.
    VkImageCreateInfo imgInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0, VK_IMAGE_TYPE_2D,
        VK_FORMAT_R8G8B8A8_UNORM, {64, 64, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr, VK_IMAGE_LAYOUT_UNDEFINED};
    VkImage colorImage = nullptr;
    if (CreateImage(device, &imgInfo, nullptr, &colorImage) != VK_SUCCESS || !colorImage)
        return fail(23, "vkCreateImage", 0);
    GetImageMemoryRequirements(device, colorImage, &req);
    VkDeviceSize imgOffset = (used + req.alignment - 1) & ~(req.alignment - 1);
    VkMemoryAllocateInfo imgAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, imgOffset + req.size, memType};
    // The colour target may need a type the vertex memory is not (device-local
    // tiling); try the same type first, then any type the requirements allow.
    VkDeviceMemory imageMemory = nullptr;
    if (AllocateMemory(device, &imgAlloc, nullptr, &imageMemory) != VK_SUCCESS || !imageMemory) {
        uint32_t bit = 0;
        for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
            if (req.memoryTypeBits & (1u << i)) { bit = i; break; }
        if (!bit && !(req.memoryTypeBits & 1u)) return fail(23, "image memory type", 0);
        VkMemoryAllocateInfo retry{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, imgOffset + req.size, bit};
        if (AllocateMemory(device, &retry, nullptr, &imageMemory) != VK_SUCCESS || !imageMemory)
            return fail(23, "vkAllocateMemory(image)", 0);
    }
    if (BindImageMemory(device, colorImage, imageMemory, imgOffset) != VK_SUCCESS)
        return fail(23, "vkBindImageMemory", 0);
    typedef struct { int32_t sType; const void* pNext; VkFlags flags; VkImage image;
                     int32_t viewType, format; struct { int32_t r, g, b, a; } components;
                     VkImageSubresourceRange subresourceRange; } ImageViewCreateInfo;
    ImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, colorImage,
        1 /* VK_IMAGE_VIEW_TYPE_2D */, VK_FORMAT_R8G8B8A8_UNORM, {0, 0, 0, 0},
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkImageView colorView = nullptr;
    auto createImageViewT = (VkResult (__stdcall*)(VkDevice, const ImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView*))CreateImageView;
    if (createImageViewT(device, &viewInfo, nullptr, &colorView) != VK_SUCCESS || !colorView)
        return fail(23, "vkCreateImageView", 0);
    emit("PLAY-VULKAN-DRAW STAGE color-target\n");

    // The graphics pipeline: dynamic viewport/scissor, no render pass — the
    // rendering-info chain names the colour format (dynamic rendering, core in
    // 1.3 / KHR_dynamic_rendering).
    VkPipelineShaderStageCreateInfo stages[2]{
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vsModule, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fsModule, "main", nullptr}};
    // Stride and attribute offsets match the uploaded layout: pos is floats
    // 0..3 (xyzw), colour floats 4..7 — byte 16, not 8.
    VkVertexInputBindingDescription vib{0, 8 * 4, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription via[2]{
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0, 1, &vib, 2, via};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0};
    static const int32_t dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dynamics};
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr};
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0,
        0, 0, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, 0, 0, 0.0f, 0.0f, 1.0f, 1.0f};
    static const VkFlags sampleMask = 0xFFFFFFFFu;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0,
        VK_SAMPLE_COUNT_1_BIT, 0, 1.0f, &sampleMask, 0, 0};
    VkPipelineColorBlendAttachmentState blend{0, 0, 0, 0, 0, 0, 0,
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0,
        0, 0, 1, &blend, {0, 0, 0, 0}};
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr, 0,
        2, stages, &vi, &ia, nullptr, &vp, &rs, &ms, nullptr, &cb, &dyn,
        pipelineLayout, nullptr, 0, nullptr, 0};
    int32_t colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo pr{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, nullptr, 0, 1, &colorFormat, 0, 0};
    if (hasDynamicRendering) gp.pNext = &pr;
    VkPipeline graphicsPipeline = nullptr;
    result = CreateGraphicsPipelines(device, nullptr, 1, &gp, nullptr, &graphicsPipeline);
    if (result != VK_SUCCESS || !graphicsPipeline) return fail(24, "vkCreateGraphicsPipelines", result);
    emit("PLAY-VULKAN-DRAW STAGE graphics-pipeline\n");

    VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, csModule, "main", nullptr},
        pipelineLayout, nullptr, 0};
    VkPipeline computePipeline = nullptr;
    result = CreateComputePipelines(device, nullptr, 1, &cp, nullptr, &computePipeline);
    if (result != VK_SUCCESS || !computePipeline) return fail(24, "vkCreateComputePipelines", result);
    emit("PLAY-VULKAN-DRAW STAGE compute-pipeline\n");

    VkCommandPoolCreateInfo poolCreateInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, 0, family};
    VkCommandPool pool = nullptr;
    if (CreateCommandPool(device, &poolCreateInfo, nullptr, &pool) != VK_SUCCESS || !pool)
        return fail(25, "vkCreateCommandPool", 0);
    VkCommandBufferAllocateInfo cbAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, pool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    VkCommandBuffer cmd = nullptr;
    if (AllocateCommandBuffers(device, &cbAlloc, &cmd) != VK_SUCCESS || !cmd)
        return fail(25, "vkAllocateCommandBuffers", 0);

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
    VkFence fence = nullptr;
    if (CreateFence(device, &fenceInfo, nullptr, &fence) != VK_SUCCESS || !fence) return fail(25, "vkCreateFence", 0);
    VkSemaphoreTypeCreateInfo timelineInfo{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr, VK_SEMAPHORE_TYPE_TIMELINE, 0};
    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timelineInfo, 0};
    VkSemaphore timeline = nullptr;
    result = CreateSemaphore(device, &semInfo, nullptr, &timeline);
    if (result != VK_SUCCESS || !timeline) return fail(25, "vkCreateSemaphore(timeline)", result);
    emit("PLAY-VULKAN-DRAW STAGE timeline-semaphore\n");

    // ---- one command buffer: compute, then the triangle, then the copy ----
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
    if (BeginCommandBuffer(cmd, &begin) != VK_SUCCESS) return fail(26, "vkBeginCommandBuffer", 0);

    // 1. Compute: data[i] *= 3 through the push constant, one workgroup of 64.
    CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSet, 0, nullptr);
    uint32_t multiplier = 3;
    CmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &multiplier);
    CmdDispatch(cmd, 1, 1, 1);
    // sync2: compute writes finish before the copy reads them.
    VkBufferMemoryBarrier2 bufBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        0, 0, storageBuffer, 0, VK_WHOLE_SIZE};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 1, &bufBarrier, 0, nullptr};
    CmdPipelineBarrier2(cmd, &dep);
    // Offsets are relative to each buffer, not to the shared allocation:
    // both buffers were filled/bound at their own start.
    VkBufferCopy copy{0, 0, 64 * 4};
    CmdCopyBuffer(cmd, storageBuffer, readbackBuffer, 1, &copy);
    emit("PLAY-VULKAN-DRAW STAGE compute-recorded\n");

    // 2. Graphics: the triangle into the offscreen target through dynamic
    //    rendering (no render pass object), then the image is left transfer-
    //    source for the host-side copy below.
    VkViewport viewport{0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
    VkRect2D scissor{}; scissor.offset.x = 0; scissor.offset.y = 0; scissor.extent.width = 64; scissor.extent.height = 64;
    VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, colorView,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, nullptr, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {}};
    att.clearValue.color.r = 0.0f; att.clearValue.color.g = 0.0f; att.clearValue.color.b = 0.0f; att.clearValue.color.a = 1.0f;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0,
        VkRect2D{scissor.offset, scissor.extent}, 1, 0, 1, &att, nullptr, nullptr};
    // Undefined -> colour-attachment through the sync2 image barrier the
    // rendering info requires (the layout transition is explicit here).
    VkImageMemoryBarrier2 imgBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 0,
        colorImage, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfo imgDep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 0, nullptr};
    imgDep.imageMemoryBarrierCount = 1;
    imgDep.pImageMemoryBarriers = &imgBarrier;
    CmdPipelineBarrier2(cmd, &imgDep);
    CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    CmdSetViewport(cmd, 0, 1, &viewport);
    CmdSetScissor(cmd, 0, 1, &scissor);
    VkDeviceSize vbOffset = offsets[0];
    CmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &vbOffset);
    CmdBeginRendering(cmd, &rendering);
    CmdDraw(cmd, 3, 1, 0, 0);
    CmdEndRendering(cmd);
    emit("PLAY-VULKAN-DRAW STAGE draw-recorded\n");

    if (EndCommandBuffer(cmd) != VK_SUCCESS) return fail(26, "vkEndCommandBuffer", 0);

    // Submit twice through the timeline semaphore: submit2 signals value 1
    // when the compute+draw work is done, the host waits on 1, then signals 2
    // and waits again — the timeline round-trip a frame-throttling game does.
    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, cmd, 0};
    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, timeline, 1,
        VK_PIPELINE_STAGE_2_COPY_BIT, 0};
    VkSubmitInfo2 submit2{VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0, 0, nullptr, 1, &cmdInfo, 1, &signalInfo};
    if (hasSync2) {
        if (QueueSubmit2(queue, 1, &submit2, fence) != VK_SUCCESS) return fail(27, "vkQueueSubmit2", 0);
    } else {
        // No sync2: the legacy submit carries the same work; the timeline
        // signal happens through vkSignalSemaphore after the queue drains.
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &cmd, 0, nullptr};
        if (QueueSubmit(queue, 1, &submit, fence) != VK_SUCCESS) return fail(27, "vkQueueSubmit", 0);
    }
    if (WaitForFences(device, 1, &fence, 1, 1000000000ull) != VK_SUCCESS) return fail(27, "vkWaitForFences", 0);
    ResetFences(device, 1, &fence);
    if (!hasSync2) {
        VkSemaphoreSignalInfo sig{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, nullptr, timeline, 1};
        if (SignalSemaphore(device, &sig) != VK_SUCCESS) return fail(27, "vkSignalSemaphore", 0);
    }
    uint64_t waited = 0;
    VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr, 0, 1, &timeline, &waited};
    waited = 1;
    if (WaitSemaphores(device, &waitInfo, 1000000000ull) != VK_SUCCESS) return fail(28, "vkWaitSemaphores(1)", 0);
    // The host signals 2 and waits for it: the round trip works.
    VkSemaphoreSignalInfo sig{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, nullptr, timeline, 2};
    if (SignalSemaphore(device, &sig) != VK_SUCCESS) return fail(28, "vkSignalSemaphore(2)", 0);
    waited = 2;
    if (WaitSemaphores(device, &waitInfo, 1000000000ull) != VK_SUCCESS) return fail(28, "vkWaitSemaphores(2)", 0);
    emit("PLAY-VULKAN-DRAW STAGE timeline-roundtrip\n");
    if (QueueWaitIdle(queue) != VK_SUCCESS) return fail(27, "vkQueueWaitIdle", 0);

    // ---- readbacks: compute output, then the triangle ----
    // The compute buffer was copied to readbackBuffer; the host reads it back
    // through the same mapped allocation (host-coherent, or flushed by the
    // queue wait).
    {
        const uint32_t* out = (const uint32_t*)((const char*)mapped + offsets[2]);
        for (uint32_t i = 0; i < 64; ++i)
            if (out[i] != (i + 1u) * 3u) {
                {char line[160]; char* p = append(line, "PLAY-VULKAN-DRAW BLOCKED compute-readback index=");
                 p = appendUInt(p, i); p = append(p, " got="); p = appendUInt(p, out[i]);
                 p = append(p, " want="); p = appendUInt(p, (i + 1u) * 3u); p = append(p, "\n"); *p = 0; emit(line);}
                return 43;
            }
    }
    emit("PLAY-VULKAN-DRAW STAGE compute-readback\n");

    // The triangle: copy the colour target into a host-visible buffer and
    // check the pixels. The copy needs the image as TRANSFER_SRC; the layout
    // change happens in a second command buffer (the first is done).
    VkBufferCreateInfo imgRbInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, 64 * 64 * 4,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    VkBuffer imageReadback = nullptr;
    if (CreateBuffer(device, &imgRbInfo, nullptr, &imageReadback) != VK_SUCCESS || !imageReadback)
        return fail(30, "vkCreateBuffer(image-readback)", 0);
    GetBufferMemoryRequirements(device, imageReadback, &req);
    VkDeviceSize imgRbOffset = 0;   // fresh allocation: no alignment neighbours
    VkMemoryAllocateInfo rbAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, memType};
    VkDeviceMemory rbMemory = nullptr;
    if (AllocateMemory(device, &rbAlloc, nullptr, &rbMemory) != VK_SUCCESS || !rbMemory)
        return fail(30, "vkAllocateMemory(image-readback)", 0);
    if (BindBufferMemory(device, imageReadback, rbMemory, imgRbOffset) != VK_SUCCESS)
        return fail(30, "vkBindBufferMemory(image-readback)", 0);
    void* rbMapped = nullptr;
    if (MapMemory(device, rbMemory, 0, VK_WHOLE_SIZE, 0, &rbMapped) != VK_SUCCESS || !rbMapped)
        return fail(30, "vkMapMemory(image-readback)", 0);

    if (AllocateCommandBuffers(device, &cbAlloc, &cmd) != VK_SUCCESS || !cmd)
        return fail(30, "vkAllocateCommandBuffers(copy)", 0);
    if (BeginCommandBuffer(cmd, &begin) != VK_SUCCESS) return fail(30, "vkBeginCommandBuffer(copy)", 0);
    // Colour-attachment -> transfer-source, the sync2 way when it is on.
    VkImageMemoryBarrier2 toSrc{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0,
        colorImage, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfo srcDep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 0, nullptr};
    srcDep.imageMemoryBarrierCount = 1;
    srcDep.pImageMemoryBarriers = &toSrc;
    CmdPipelineBarrier2(cmd, &srcDep);
    // Buffer-image copy: the one command both the readback and a screenshot
    // go through. Fetched by name because the fixture uses it only for this.
    auto cmdCopyImageToBuffer = (void (__stdcall*)(VkCommandBuffer, VkImage, int32_t, VkBuffer, uint32_t, const void*))dpa(device, "vkCmdCopyImageToBuffer");
    if (!cmdCopyImageToBuffer) return fail(30, "vkGetDeviceProcAddr(vkCmdCopyImageToBuffer)", 0);
    // VkBufferImageCopy: the subresource (aspect, mip, layers) sits between
    // the buffer pitch fields and the image offset/extent.
    typedef struct { VkDeviceSize bufferOffset; uint32_t bufferRowLength, bufferImageHeight;
                     struct { uint32_t aspectMask, mipLevel, baseArrayLayer, layerCount; } imageSubresource;
                     struct { int32_t x, y, z; } imageOffset;
                     struct { uint32_t width, height, depth; } imageExtent; } BufferImageCopy;
    BufferImageCopy region{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {64, 64, 1}};
    cmdCopyImageToBuffer(cmd, colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, imageReadback, 1, &region);
    if (EndCommandBuffer(cmd) != VK_SUCCESS) return fail(30, "vkEndCommandBuffer(copy)", 0);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &cmd, 0, nullptr};
    if (QueueSubmit(queue, 1, &submit, fence) != VK_SUCCESS) return fail(30, "vkQueueSubmit(copy)", 0);
    if (WaitForFences(device, 1, &fence, 1, 1000000000ull) != VK_SUCCESS) return fail(30, "vkWaitForFences(copy)", 0);
    ResetFences(device, 1, &fence);
    if (QueueWaitIdle(queue) != VK_SUCCESS) return fail(30, "vkQueueWaitIdle(copy)", 0);

    // The triangle covers the middle-bottom-ish band; the clear colour is
    // everywhere else. Row pitch is 64*4 (no padding at 64 width).
    {
        const unsigned char* base = (const unsigned char*)rbMapped;
        // Centre of the triangle (y=40 is inside; the triangle spans y 16..64
        // at its widest) must be red; the top-left corner must be the clear.
        const unsigned char* inside = base + (40 * 64 + 32) * 4;
        const unsigned char* outside = base + (4 * 64 + 4) * 4;
        const bool insideRed = inside[0] > 0x80 && inside[1] < 0x40 && inside[2] < 0x40;
        const bool outsideBlack = outside[0] < 0x20 && outside[1] < 0x20 && outside[2] < 0x20;
        if (!insideRed || !outsideBlack) {
            {char line[224]; char* p = append(line, "PLAY-VULKAN-DRAW BLOCKED pixel-readback inside=");
             p = appendUInt(p, (uint32_t)inside[0]); p = append(p, ","); p = appendUInt(p, (uint32_t)inside[1]);
             p = append(p, ","); p = appendUInt(p, (uint32_t)inside[2]);
             p = append(p, " outside="); p = appendUInt(p, (uint32_t)outside[0]);
             p = append(p, ","); p = appendUInt(p, (uint32_t)outside[1]);
             p = append(p, ","); p = appendUInt(p, (uint32_t)outside[2]); p = append(p, "\n"); *p = 0; emit(line);}
            return 43;
        }
    }
    emit("PLAY-VULKAN-DRAW STAGE pixel-readback\n");

    emit("PLAY-VULKAN-DRAW PASS draw-readback\n");
    return 42;
}
}
