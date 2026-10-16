// Offscreen smoke test for the Cubism Vulkan renderer on Linux.
//
// It creates a headless Vulkan device, asks the Cubism Framework to draw a
// model into an application owned VkImage, reads that image back and verifies
// that something was actually rasterised. This is the same code path the Qt
// viewer uses, without Qt in the way, so a green run proves Core + Framework +
// SPIR-V + renderer are wired correctly.
//
// Usage: spinelove_live2d_smoke <resource-dir> [output.ppm]
//   <resource-dir> must contain Wanko/Wanko.moc3 and
//   Wanko/Wanko.1024/texture_00.png.

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "CubismFramework.hpp"
#include "Math/CubismMatrix44.hpp"
#include "Model/CubismUserModel.hpp"
#include "Rendering/Vulkan/CubismClass_Vulkan.hpp"
#include "Rendering/Vulkan/CubismRenderer_Vulkan.hpp"
#include "live2d/live2d_module.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Csm = Live2D::Cubism::Framework;

namespace {

constexpr int kWidth = 512;
constexpr int kHeight = 512;
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr float kClippingMaskBufferSize = 2048.0f;

void Log(const char* message)
{
    if (message != nullptr)
        std::fprintf(stderr, "[cubism] %s\n", message);
}

std::vector<char> ReadFile(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        return {};
    const std::streamsize size = stream.tellg();
    stream.seekg(0, std::ios::beg);
    std::vector<char> bytes(static_cast<size_t>(size));
    if (size > 0 && !stream.read(bytes.data(), size))
        return {};
    return bytes;
}

class SmokeAllocator final : public Csm::ICubismAllocator
{
public:
    void* Allocate(const Csm::csmSizeType size) override { return std::malloc(size); }
    void Deallocate(void* memory) override { std::free(memory); }
    void* AllocateAligned(const Csm::csmSizeType size, const Csm::csmUint32 alignment) override
    {
        return aligned_alloc(alignment, size);
    }
    void DeallocateAligned(void* alignedMemory) override { std::free(alignedMemory); }
};

struct VulkanContext
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
};

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1u << i)) != 0
            && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return UINT32_MAX;
}

bool CreateVulkanContext(VulkanContext& context)
{
    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "spinelove-live2d-smoke";
    applicationInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &applicationInfo;
    if (vkCreateInstance(&instanceInfo, nullptr, &context.instance) != VK_SUCCESS)
    {
        std::fprintf(stderr, "vkCreateInstance failed\n");
        return false;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(context.instance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        std::fprintf(stderr, "no Vulkan physical device found\n");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(context.instance, &deviceCount, devices.data());

    for (VkPhysicalDevice candidate : devices)
    {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (uint32_t i = 0; i < familyCount; ++i)
        {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            {
                context.physicalDevice = candidate;
                context.queueFamily = i;
                break;
            }
        }
        if (context.physicalDevice != VK_NULL_HANDLE)
            break;
    }
    if (context.physicalDevice == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "no Vulkan device with a graphics queue found\n");
        return false;
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = context.queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    // Cubism's Vulkan renderer drives culling through vkCmdSetCullModeEXT.
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(context.physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(context.physicalDevice, nullptr, &extensionCount, extensions.data());
    bool hasExtendedDynamicState = false;
    for (const VkExtensionProperties& extension : extensions)
    {
        if (std::strcmp(extension.extensionName, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME) == 0)
            hasExtendedDynamicState = true;
    }
    if (!hasExtendedDynamicState)
    {
        std::fprintf(stderr, "VK_EXT_extended_dynamic_state is required by the Cubism renderer\n");
        return false;
    }
    const char* enabledExtensions[] = {VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME};

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicState{};
    extendedDynamicState.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    extendedDynamicState.extendedDynamicState = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering{};
    dynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamicRendering.pNext = &extendedDynamicState;
    dynamicRendering.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &dynamicRendering;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = enabledExtensions;
    deviceInfo.pEnabledFeatures = &features;
    if (vkCreateDevice(context.physicalDevice, &deviceInfo, nullptr, &context.device) != VK_SUCCESS)
    {
        std::fprintf(stderr, "vkCreateDevice failed (dynamic rendering / Vulkan 1.3 required)\n");
        return false;
    }
    vkGetDeviceQueue(context.device, context.queueFamily, 0, &context.queue);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context.queueFamily;
    if (vkCreateCommandPool(context.device, &poolInfo, nullptr, &context.commandPool) != VK_SUCCESS)
    {
        std::fprintf(stderr, "vkCreateCommandPool failed\n");
        return false;
    }
    return true;
}

VkCommandBuffer BeginOneShot(const VulkanContext& context)
{
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = context.commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(context.device, &allocateInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void EndOneShot(const VulkanContext& context, VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(context.queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(context.queue);
    vkFreeCommandBuffers(context.device, context.commandPool, 1, &commandBuffer);
}

void TransitionLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout from, VkImageLayout to,
                      VkPipelineStageFlags sourceStage, VkPipelineStageFlags destinationStage,
                      VkAccessFlags sourceAccess, VkAccessFlags destinationAccess)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

bool CreateColorTarget(const VulkanContext& context, VkImage& image, VkDeviceMemory& memory, VkImageView& view)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {kWidth, kHeight, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = kColorFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(context.device, &imageInfo, nullptr, &image) != VK_SUCCESS)
        return false;

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(context.device, image, &requirements);
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = FindMemoryType(context.physicalDevice, requirements.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocateInfo.memoryTypeIndex == UINT32_MAX
        || vkAllocateMemory(context.device, &allocateInfo, nullptr, &memory) != VK_SUCCESS)
        return false;
    if (vkBindImageMemory(context.device, image, memory, 0) != VK_SUCCESS)
        return false;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kColorFormat;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return vkCreateImageView(context.device, &viewInfo, nullptr, &view) == VK_SUCCESS;
}

// Uploads RGBA8 pixels into a Cubism image and leaves it in
// VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, which is what BindTexture expects.
bool CreateModelTexture(const VulkanContext& context, const char* path, Csm::CubismImageVulkan& image)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path, &width, &height, &channels, 4);
    if (pixels == nullptr)
    {
        std::fprintf(stderr, "failed to decode texture %s: %s\n", path, stbi_failure_reason());
        return false;
    }

    const VkDeviceSize byteCount = VkDeviceSize(width) * VkDeviceSize(height) * 4;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = byteCount;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging = VK_NULL_HANDLE;
    if (vkCreateBuffer(context.device, &bufferInfo, nullptr, &staging) != VK_SUCCESS)
    {
        stbi_image_free(pixels);
        return false;
    }
    VkMemoryRequirements bufferRequirements{};
    vkGetBufferMemoryRequirements(context.device, staging, &bufferRequirements);
    VkMemoryAllocateInfo bufferAllocateInfo{};
    bufferAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bufferAllocateInfo.allocationSize = bufferRequirements.size;
    bufferAllocateInfo.memoryTypeIndex = FindMemoryType(context.physicalDevice, bufferRequirements.memoryTypeBits,
                                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                                            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    if (bufferAllocateInfo.memoryTypeIndex == UINT32_MAX
        || vkAllocateMemory(context.device, &bufferAllocateInfo, nullptr, &stagingMemory) != VK_SUCCESS
        || vkBindBufferMemory(context.device, staging, stagingMemory, 0) != VK_SUCCESS)
    {
        stbi_image_free(pixels);
        return false;
    }
    void* mapped = nullptr;
    vkMapMemory(context.device, stagingMemory, 0, byteCount, 0, &mapped);
    std::memcpy(mapped, pixels, static_cast<size_t>(byteCount));
    vkUnmapMemory(context.device, stagingMemory);
    stbi_image_free(pixels);

    image.CreateImage(context.device, context.physicalDevice, width, height, 1, kColorFormat,
                      VK_IMAGE_TILING_OPTIMAL,
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    image.CreateView(context.device, kColorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    image.CreateSampler(context.device, 1.0f, 1);

    VkCommandBuffer commandBuffer = BeginOneShot(context);
    image.SetImageLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, VK_IMAGE_ASPECT_COLOR_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyBufferToImage(commandBuffer, staging, image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    image.SetImageLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, VK_IMAGE_ASPECT_COLOR_BIT);
    EndOneShot(context, commandBuffer);

    image.SetCurrentLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkDestroyBuffer(context.device, staging, nullptr);
    vkFreeMemory(context.device, stagingMemory, nullptr);
    return true;
}

class SmokeModel final : public Csm::CubismUserModel
{
public:
    bool LoadMoc(const std::string& path)
    {
        const std::vector<char> bytes = ReadFile(path);
        if (bytes.empty())
        {
            std::fprintf(stderr, "failed to read moc3: %s\n", path.c_str());
            return false;
        }
        LoadModel(reinterpret_cast<const Csm::csmByte*>(bytes.data()),
                  static_cast<Csm::csmSizeInt>(bytes.size()), true);
        if (_model == nullptr)
        {
            std::fprintf(stderr, "Cubism could not revive the moc3\n");
            return false;
        }
        _model->Update();
        return true;
    }

    // Maps the model's drawable bounds onto roughly [-1, 1] in clip space.
    Csm::CubismMatrix44 BuildMvp() const
    {
        bool hasBounds = false;
        float minimumX = 0.0f;
        float minimumY = 0.0f;
        float maximumX = 0.0f;
        float maximumY = 0.0f;
        for (Csm::csmInt32 drawable = 0; drawable < _model->GetDrawableCount(); ++drawable)
        {
            if (_model->GetDrawableOpacity(drawable) <= 0.001f)
                continue;
            const Csm::csmInt32 vertexCount = _model->GetDrawableVertexCount(drawable);
            const Csm::csmFloat32* vertices = _model->GetDrawableVertices(drawable);
            if (vertexCount <= 0 || vertices == nullptr)
                continue;
            for (Csm::csmInt32 vertex = 0; vertex < vertexCount; ++vertex)
            {
                const float x = vertices[vertex * 2];
                const float y = vertices[vertex * 2 + 1];
                if (!std::isfinite(x) || !std::isfinite(y))
                    continue;
                if (!hasBounds)
                {
                    minimumX = maximumX = x;
                    minimumY = maximumY = y;
                    hasBounds = true;
                }
                else
                {
                    minimumX = std::min(minimumX, x);
                    maximumX = std::max(maximumX, x);
                    minimumY = std::min(minimumY, y);
                    maximumY = std::max(maximumY, y);
                }
            }
        }

        Csm::CubismMatrix44 mvp;
        mvp.LoadIdentity();
        if (!hasBounds)
            return mvp;

        const float centerX = (minimumX + maximumX) * 0.5f;
        const float centerY = (minimumY + maximumY) * 0.5f;
        const float extent = std::max(maximumX - minimumX, maximumY - minimumY);
        if (extent <= 0.0f)
            return mvp;

        const float scale = 1.8f / extent;
        mvp.Scale(scale, scale);
        mvp.Translate(-centerX, -centerY);
        return mvp;
    }
};

void WritePpm(const std::string& path, const std::vector<unsigned char>& rgba)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
        return;
    stream << "P6\n" << kWidth << " " << kHeight << "\n255\n";
    for (size_t i = 0; i < rgba.size(); i += 4)
    {
        const unsigned char pixel[3] = {rgba[i], rgba[i + 1], rgba[i + 2]};
        stream.write(reinterpret_cast<const char*>(pixel), 3);
    }
}

// Copies an image back to host memory after moving it to the transfer layout.
bool ReadBackImage(const VulkanContext& context, VkImage image, VkImageLayout currentLayout,
                   std::vector<unsigned char>& pixels)
{
    const VkDeviceSize byteCount = VkDeviceSize(kWidth) * VkDeviceSize(kHeight) * 4;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = byteCount;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer readback = VK_NULL_HANDLE;
    if (vkCreateBuffer(context.device, &bufferInfo, nullptr, &readback) != VK_SUCCESS)
        return false;

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(context.device, readback, &requirements);
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = FindMemoryType(context.physicalDevice, requirements.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                                      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (allocateInfo.memoryTypeIndex == UINT32_MAX
        || vkAllocateMemory(context.device, &allocateInfo, nullptr, &memory) != VK_SUCCESS
        || vkBindBufferMemory(context.device, readback, memory, 0) != VK_SUCCESS)
    {
        vkDestroyBuffer(context.device, readback, nullptr);
        return false;
    }

    VkCommandBuffer copy = BeginOneShot(context);
    TransitionLayout(copy, image, currentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {kWidth, kHeight, 1};
    vkCmdCopyImageToBuffer(copy, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &region);
    EndOneShot(context, copy);

    void* mapped = nullptr;
    pixels.assign(static_cast<size_t>(byteCount), 0);
    if (vkMapMemory(context.device, memory, 0, byteCount, 0, &mapped) == VK_SUCCESS)
    {
        std::memcpy(pixels.data(), mapped, static_cast<size_t>(byteCount));
        vkUnmapMemory(context.device, memory);
    }

    vkDestroyBuffer(context.device, readback, nullptr);
    vkFreeMemory(context.device, memory, nullptr);
    return true;
}

size_t CountNonBackground(const std::vector<unsigned char>& pixels)
{
    size_t differing = 0;
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        if (pixels[i] != pixels[0] || pixels[i + 1] != pixels[1]
            || pixels[i + 2] != pixels[2] || pixels[i + 3] != pixels[3])
            ++differing;
    }
    return differing;
}

// Exercises the real viewer module (main/live2d/live2d_module.cpp) instead of
// driving the Cubism renderer by hand.
int RunModulePath(const VulkanContext& context, const std::string& resourceDir, const std::string& outputPpm)
{
    live2d::Live2DModule module;
    live2d::VulkanHandles handles;
    handles.physicalDevice = context.physicalDevice;
    handles.device = context.device;
    handles.queue = context.queue;
    handles.queueFamily = context.queueFamily;
    if (!module.Initialize(handles))
    {
        std::fprintf(stderr, "Live2DModule::Initialize failed: %s\n", module.LastError().c_str());
        return 1;
    }

    const std::string manifestUtf8 = resourceDir + "/Wanko/Wanko.model3.json";
    const std::wstring manifest(manifestUtf8.begin(), manifestUtf8.end());
    if (!module.ImportModel(manifest))
    {
        std::fprintf(stderr, "Live2DModule::ImportModel failed: %s\n", module.LastError().c_str());
        return 1;
    }

    if (!module.TickAndRender(0.5f, kWidth, kHeight, 0.0f, 0.0f, true))
    {
        std::fprintf(stderr, "Live2DModule::TickAndRender failed: %s\n", module.LastError().c_str());
        return 1;
    }

    const VkImage image = reinterpret_cast<VkImage>(
        static_cast<std::uintptr_t>(module.NativeRenderTargetImage()));
    if (image == VK_NULL_HANDLE
        || module.NativeRenderTargetWidth() != kWidth || module.NativeRenderTargetHeight() != kHeight)
    {
        std::fprintf(stderr, "the module did not expose a %dx%d render target\n", kWidth, kHeight);
        return 1;
    }

    std::vector<unsigned char> pixels;
    if (!ReadBackImage(context, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, pixels))
    {
        std::fprintf(stderr, "readback failed\n");
        return 1;
    }

    const size_t differing = CountNonBackground(pixels);
    const double coverage = 100.0 * static_cast<double>(differing) / static_cast<double>(kWidth * kHeight);
    std::printf("live2d module smoke: %zu/%d pixels differ from the clear value (%.2f%%)\n",
                differing, kWidth * kHeight, coverage);
    if (!outputPpm.empty())
        WritePpm(outputPpm, pixels);
    if (differing == 0)
    {
        std::fprintf(stderr, "the module produced no visible pixels\n");
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <resource-dir> [output.ppm] [--module]\n", argv[0]);
        return 2;
    }

    bool moduleMode = false;
    std::string resourceDir;
    std::string outputPpm;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument == "--module")
            moduleMode = true;
        else if (resourceDir.empty())
            resourceDir = argument;
        else if (outputPpm.empty())
            outputPpm = argument;
    }

    const std::string mocPath = resourceDir + "/Wanko/Wanko.moc3";
    const std::string texturePath = resourceDir + "/Wanko/Wanko.1024/texture_00.png";

    VulkanContext context;
    if (!CreateVulkanContext(context))
        return 1;

    if (moduleMode)
    {
        const int code = RunModulePath(context, resourceDir, outputPpm);
        vkDestroyCommandPool(context.device, context.commandPool, nullptr);
        vkDestroyDevice(context.device, nullptr);
        vkDestroyInstance(context.instance, nullptr);
        return code;
    }

    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE;
    if (!CreateColorTarget(context, colorImage, colorMemory, colorView))
    {
        std::fprintf(stderr, "failed to create the offscreen colour target\n");
        return 1;
    }

    // The renderer needs its constant settings before a model creates a renderer.
    Csm::Rendering::CubismRenderer_Vulkan::InitializeConstantSettings(
        context.device, context.physicalDevice, context.commandPool, context.queue,
        VkExtent2D{kWidth, kHeight}, kColorFormat, kColorFormat, colorImage, colorView, kDepthFormat);
    Csm::Rendering::CubismRenderer_Vulkan::EnableChangeRenderTarget();
    Csm::Rendering::CubismRenderer_Vulkan::SetRenderTarget(colorImage, colorView);

    SmokeAllocator allocator;
    Csm::CubismFramework::Option option{};
    option.LogFunction = &Log;
    option.LoggingLevel = Csm::CubismFramework::Option::LogLevel_Warning;
    if (!Csm::CubismFramework::StartUp(&allocator, &option))
    {
        std::fprintf(stderr, "CubismFramework::StartUp failed\n");
        return 1;
    }
    Csm::CubismFramework::Initialize();

    int exitCode = 1;
    {
        SmokeModel model;
        if (!model.LoadMoc(mocPath))
        {
            Csm::CubismFramework::Dispose();
            Csm::CubismFramework::CleanUp();
            return 1;
        }

        model.CreateRenderer();
        auto* renderer = model.GetRenderer<Csm::Rendering::CubismRenderer_Vulkan>();
        if (renderer == nullptr)
        {
            std::fprintf(stderr, "CubismRenderer_Vulkan was not created\n");
            Csm::CubismFramework::Dispose();
            Csm::CubismFramework::CleanUp();
            return 1;
        }
        if (model.GetModel()->IsUsingMasking())
            renderer->SetClippingMaskBufferSize(kClippingMaskBufferSize, kClippingMaskBufferSize);

        Csm::CubismImageVulkan texture;
        if (!CreateModelTexture(context, texturePath.c_str(), texture))
        {
            Csm::CubismFramework::Dispose();
            Csm::CubismFramework::CleanUp();
            return 1;
        }
        renderer->BindTexture(texture);
        renderer->IsPremultipliedAlpha(false);

        // Put the target into the layout the renderer's barrier expects.
        VkCommandBuffer setup = BeginOneShot(context);
        TransitionLayout(setup, colorImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        EndOneShot(context, setup);

        Csm::CubismMatrix44 mvp = model.BuildMvp();
        renderer->SetMvpMatrix(&mvp);
        renderer->DrawModel();

        // Read the result back.
        const VkDeviceSize byteCount = VkDeviceSize(kWidth) * VkDeviceSize(kHeight) * 4;
        VkBufferCreateInfo readbackInfo{};
        readbackInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        readbackInfo.size = byteCount;
        readbackInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        readbackInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer readback = VK_NULL_HANDLE;
        vkCreateBuffer(context.device, &readbackInfo, nullptr, &readback);
        VkMemoryRequirements readbackRequirements{};
        vkGetBufferMemoryRequirements(context.device, readback, &readbackRequirements);
        VkMemoryAllocateInfo readbackAllocateInfo{};
        readbackAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        readbackAllocateInfo.allocationSize = readbackRequirements.size;
        readbackAllocateInfo.memoryTypeIndex = FindMemoryType(
            context.physicalDevice, readbackRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
        vkAllocateMemory(context.device, &readbackAllocateInfo, nullptr, &readbackMemory);
        vkBindBufferMemory(context.device, readback, readbackMemory, 0);

        VkCommandBuffer copy = BeginOneShot(context);
        TransitionLayout(copy, colorImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kWidth, kHeight, 1};
        vkCmdCopyImageToBuffer(copy, colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &region);
        EndOneShot(context, copy);

        void* mapped = nullptr;
        vkMapMemory(context.device, readbackMemory, 0, byteCount, 0, &mapped);
        std::vector<unsigned char> pixels(static_cast<size_t>(byteCount));
        std::memcpy(pixels.data(), mapped, static_cast<size_t>(byteCount));
        vkUnmapMemory(context.device, readbackMemory);

        // The background is whatever the renderer clears to; anything that
        // differs from the top-left pixel was rasterised.
        const unsigned char* background = pixels.data();
        size_t differing = 0;
        for (size_t i = 0; i < pixels.size(); i += 4)
        {
            if (pixels[i] != background[0] || pixels[i + 1] != background[1]
                || pixels[i + 2] != background[2] || pixels[i + 3] != background[3])
                ++differing;
        }
        const double coverage = 100.0 * static_cast<double>(differing) / static_cast<double>(kWidth * kHeight);
        std::printf("live2d vulkan smoke: %zu/%d pixels differ from the clear value (%.2f%%)\n",
                    differing, kWidth * kHeight, coverage);

        if (argc > 2)
            WritePpm(argv[2], pixels);

        if (differing > 0)
            exitCode = 0;
        else
            std::fprintf(stderr, "the model produced no visible pixels\n");

        vkDestroyBuffer(context.device, readback, nullptr);
        vkFreeMemory(context.device, readbackMemory, nullptr);
        texture.Destroy(context.device);
        model.DeleteRenderer();
    }

    Csm::CubismFramework::Dispose();
    Csm::CubismFramework::CleanUp();

    vkDestroyImageView(context.device, colorView, nullptr);
    vkDestroyImage(context.device, colorImage, nullptr);
    vkFreeMemory(context.device, colorMemory, nullptr);
    vkDestroyCommandPool(context.device, context.commandPool, nullptr);
    vkDestroyDevice(context.device, nullptr);
    vkDestroyInstance(context.instance, nullptr);
    return exitCode;
}
