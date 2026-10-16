// Probes what a default QRhi Vulkan device offers, because the Cubism Vulkan
// renderer has hard requirements on top of it:
//
//   * Vulkan 1.3 dynamic rendering (vkCmdBeginRendering / vkCmdEndRendering),
//   * a cull mode entry point (core vkCmdSetCullMode or VK_EXT_extended_dynamic_state),
//   * QRhiTexture::createFrom() with an application owned VkImage, which is how
//     the QML scene samples what Cubism drew.
//
// Run it under any Vulkan driver (CI uses lavapipe + QT_QPA_PLATFORM=offscreen).

#include <QGuiApplication>
#include <QSize>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QVulkanInstance>

#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <vulkan/vulkan.h>

#include <cstdio>

namespace {

constexpr int kWidth = 64;
constexpr int kHeight = 64;

int FindMemoryType(QVulkanFunctions* vk, VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                   VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vk->vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1u << i)) != 0
            && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return static_cast<int>(i);
    }
    return -1;
}

void ReportEntryPoint(QVulkanFunctions* vk, VkDevice device, const char* name)
{
    std::printf("  %-28s %s\n", name,
                vk->vkGetDeviceProcAddr(device, name) != nullptr ? "available" : "missing");
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    QVulkanInstance instance;
    if (!instance.create())
    {
        std::fprintf(stderr, "QVulkanInstance::create() failed; no Vulkan driver?\n");
        return 1;
    }
    QVulkanFunctions* vk = instance.functions();

    QRhiVulkanInitParams params;
    params.inst = &instance;
    params.window = nullptr;
    QRhi* rhi = QRhi::create(QRhi::Vulkan, &params);
    if (rhi == nullptr)
    {
        std::fprintf(stderr, "QRhi::create(Vulkan) failed\n");
        return 1;
    }

    const auto* native = static_cast<const QRhiVulkanNativeHandles*>(rhi->nativeHandles());
    if (native == nullptr || native->dev == VK_NULL_HANDLE || native->physDev == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "QRhi did not expose Vulkan native handles\n");
        return 1;
    }
    QVulkanDeviceFunctions* dev = instance.deviceFunctions(native->dev);
    if (dev == nullptr)
    {
        std::fprintf(stderr, "QVulkanInstance::deviceFunctions() failed\n");
        return 1;
    }

    VkPhysicalDeviceProperties properties{};
    vk->vkGetPhysicalDeviceProperties(native->physDev, &properties);
    std::printf("QRhi Vulkan device: %s\n", properties.deviceName);
    std::printf("  apiVersion %u.%u.%u\n", VK_VERSION_MAJOR(properties.apiVersion),
                VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion));
    std::printf("  queue family %u, queue %u\n", native->gfxQueueFamilyIdx, native->gfxQueueIdx);
    ReportEntryPoint(vk, native->dev, "vkCmdBeginRendering");
    ReportEntryPoint(vk, native->dev, "vkCmdBeginRenderingKHR");
    ReportEntryPoint(vk, native->dev, "vkCmdSetCullMode");
    ReportEntryPoint(vk, native->dev, "vkCmdSetCullModeEXT");

    bool ok = true;
    const bool dynamicRendering =
        vk->vkGetDeviceProcAddr(native->dev, "vkCmdBeginRendering") != nullptr
        || vk->vkGetDeviceProcAddr(native->dev, "vkCmdBeginRenderingKHR") != nullptr;
    if (!dynamicRendering)
    {
        std::fprintf(stderr, "FATAL: no dynamic rendering on the QRhi Vulkan device\n");
        ok = false;
    }

    // Can an image created by the application be handed to QRhi?
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {kWidth, kHeight, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkImage image = VK_NULL_HANDLE;
    if (dev->vkCreateImage(native->dev, &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        std::fprintf(stderr, "vkCreateImage failed\n");
        return 1;
    }
    VkMemoryRequirements requirements{};
    dev->vkGetImageMemoryRequirements(native->dev, image, &requirements);
    int memoryType = FindMemoryType(vk, native->physDev, requirements.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType < 0)
        memoryType = FindMemoryType(vk, native->physDev, requirements.memoryTypeBits, 0);
    if (memoryType < 0)
    {
        std::fprintf(stderr, "no usable memory type for the probe image\n");
        dev->vkDestroyImage(native->dev, image, nullptr);
        return 1;
    }
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = static_cast<uint32_t>(memoryType);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (dev->vkAllocateMemory(native->dev, &allocateInfo, nullptr, &memory) != VK_SUCCESS
        || dev->vkBindImageMemory(native->dev, image, memory, 0) != VK_SUCCESS)
    {
        std::fprintf(stderr, "image memory allocation failed\n");
        return 1;
    }

    QRhiTexture* texture = rhi->newTexture(QRhiTexture::RGBA8, QSize(kWidth, kHeight), 1);
    texture->setNativeLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const bool imported = texture->createFrom(
        {static_cast<quint64>(reinterpret_cast<quintptr>(image)),
         static_cast<int>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)});
    std::printf("  QRhiTexture::createFrom(external VkImage) %s\n", imported ? "accepted" : "REJECTED");

    if (imported)
    {
        rhi->finish();
        const QRhiTexture::NativeTexture roundTrip = texture->nativeTexture();
        const bool sameImage = roundTrip.object == static_cast<quint64>(reinterpret_cast<quintptr>(image));
        std::printf("  nativeTexture() round-trip       %s\n", sameImage ? "same VkImage" : "different object");
        if (!sameImage)
            ok = false;
        texture->destroy();
    }
    else
    {
        ok = false;
    }
    delete texture;

    dev->vkDeviceWaitIdle(native->dev);
    dev->vkDestroyImage(native->dev, image, nullptr);
    dev->vkFreeMemory(native->dev, memory, nullptr);
    delete rhi;

    std::printf("qrhi vulkan probe: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
