#include "vk_renderer.h"

#include <cstring>
#include <vector>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace sl_vk
{
	namespace
	{
		constexpr VkFormat kTargetFormat = VK_FORMAT_R8G8B8A8_UNORM;

		VkImageAspectFlags ColorAspect() { return VK_IMAGE_ASPECT_COLOR_BIT; }
	}

	uint32_t VulkanRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
	{
		VkPhysicalDeviceMemoryProperties memoryProperties{};
		vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);
		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1u << i)) != 0
				&& (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
				return i;
		}
		return UINT32_MAX;
	}

	void VulkanRenderer::Initialize(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue, uint32_t queueFamily)
	{
		m_physicalDevice = physicalDevice;
		m_device = device;
		m_queue = queue;
		m_queueFamily = queueFamily;

		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = queueFamily;
		if (vkCreateCommandPool(device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
			m_commandPool = VK_NULL_HANDLE;
	}

	void VulkanRenderer::Shutdown() noexcept
	{
		if (m_device == VK_NULL_HANDLE)
			return;
		vkDeviceWaitIdle(m_device);
		for (auto& entry : m_textures)
			entry.second.image.Destroy(m_device);
		m_textures.clear();
		for (auto& entry : m_renderTargets)
			DestroyRenderTarget(entry.second);
		m_renderTargets.clear();
		if (m_commandPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(m_device, m_commandPool, nullptr);
		m_commandPool = VK_NULL_HANDLE;
		m_currentTarget = 0;
		m_device = VK_NULL_HANDLE;
	}

	VkCommandBuffer VulkanRenderer::BeginOneShot() const
	{
		VkCommandBufferAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = m_commandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		vkAllocateCommandBuffers(m_device, &allocateInfo, &commandBuffer);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(commandBuffer, &beginInfo);
		return commandBuffer;
	}

	void VulkanRenderer::EndOneShot(VkCommandBuffer commandBuffer) const
	{
		vkEndCommandBuffer(commandBuffer);
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(m_queue);
		vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
	}

	void VulkanRenderer::TransitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout from,
		VkImageLayout to, VkPipelineStageFlags sourceStage, VkPipelineStageFlags destinationStage,
		VkAccessFlags sourceAccess, VkAccessFlags destinationAccess) const
	{
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = from;
		barrier.newLayout = to;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange = {ColorAspect(), 0, 1, 0, 1};
		barrier.srcAccessMask = sourceAccess;
		barrier.dstAccessMask = destinationAccess;
		vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	SlTextureId VulkanRenderer::LoadTexture(const char* utf8Path, bool premultiplyAlpha, bool generateMips)
	{
		(void)generateMips;
		if (m_device == VK_NULL_HANDLE || utf8Path == nullptr)
			return 0;

		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc* pixels = stbi_load(utf8Path, &width, &height, &channels, 4);
		if (pixels == nullptr || width <= 0 || height <= 0)
			return 0;

		if (premultiplyAlpha)
		{
			for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i)
			{
				const unsigned int alpha = pixels[i * 4 + 3];
				pixels[i * 4 + 0] = static_cast<stbi_uc>((pixels[i * 4 + 0] * alpha) / 255);
				pixels[i * 4 + 1] = static_cast<stbi_uc>((pixels[i * 4 + 1] * alpha) / 255);
				pixels[i * 4 + 2] = static_cast<stbi_uc>((pixels[i * 4 + 2] * alpha) / 255);
			}
		}

		const VkDeviceSize byteCount = VkDeviceSize(width) * VkDeviceSize(height) * 4;

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = byteCount;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VkBuffer staging = VK_NULL_HANDLE;
		if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &staging) != VK_SUCCESS)
		{
			stbi_image_free(pixels);
			return 0;
		}

		VkMemoryRequirements bufferRequirements{};
		vkGetBufferMemoryRequirements(m_device, staging, &bufferRequirements);
		VkMemoryAllocateInfo bufferAllocateInfo{};
		bufferAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		bufferAllocateInfo.allocationSize = bufferRequirements.size;
		bufferAllocateInfo.memoryTypeIndex = FindMemoryType(bufferRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
		bool stagingReady = bufferAllocateInfo.memoryTypeIndex != UINT32_MAX
			&& vkAllocateMemory(m_device, &bufferAllocateInfo, nullptr, &stagingMemory) == VK_SUCCESS
			&& vkBindBufferMemory(m_device, staging, stagingMemory, 0) == VK_SUCCESS;
		if (!stagingReady)
		{
			stbi_image_free(pixels);
			vkDestroyBuffer(m_device, staging, nullptr);
			return 0;
		}

		void* mapped = nullptr;
		if (vkMapMemory(m_device, stagingMemory, 0, byteCount, 0, &mapped) != VK_SUCCESS)
		{
			stbi_image_free(pixels);
			vkDestroyBuffer(m_device, staging, nullptr);
			vkFreeMemory(m_device, stagingMemory, nullptr);
			return 0;
		}
		std::memcpy(mapped, pixels, static_cast<size_t>(byteCount));
		vkUnmapMemory(m_device, stagingMemory);
		stbi_image_free(pixels);

		TextureSlot slot;
		slot.image.CreateImage(m_device, m_physicalDevice, width, height, 1, kTargetFormat,
			VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		slot.image.CreateView(m_device, kTargetFormat, ColorAspect(), 1);
		slot.image.CreateSampler(m_device, 1.0f, 1);

		VkCommandBuffer commandBuffer = BeginOneShot();
		slot.image.SetImageLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, ColorAspect());
		VkBufferImageCopy region{};
		region.imageSubresource = {ColorAspect(), 0, 0, 1};
		region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
		vkCmdCopyBufferToImage(commandBuffer, staging, slot.image.GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		slot.image.SetImageLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, ColorAspect());
		EndOneShot(commandBuffer);
		slot.image.SetCurrentLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		vkDestroyBuffer(m_device, staging, nullptr);
		vkFreeMemory(m_device, stagingMemory, nullptr);

		const SlTextureId id = m_nextTextureId++;
		m_textures.emplace(id, std::move(slot));
		return id;
	}

	void VulkanRenderer::ReleaseTexture(SlTextureId texture) noexcept
	{
		const auto found = m_textures.find(texture);
		if (found == m_textures.end())
			return;
		if (m_device != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_device);
			found->second.image.Destroy(m_device);
		}
		m_textures.erase(found);
	}

	Csm::CubismImageVulkan* VulkanRenderer::GetCubismImage(SlTextureId texture) noexcept
	{
		const auto found = m_textures.find(texture);
		return found == m_textures.end() ? nullptr : &found->second.image;
	}

	void VulkanRenderer::DestroyRenderTarget(RenderTargetSlot& slot) noexcept
	{
		if (m_device == VK_NULL_HANDLE)
			return;
		if (slot.view != VK_NULL_HANDLE)
			vkDestroyImageView(m_device, slot.view, nullptr);
		if (slot.image != VK_NULL_HANDLE)
			vkDestroyImage(m_device, slot.image, nullptr);
		if (slot.memory != VK_NULL_HANDLE)
			vkFreeMemory(m_device, slot.memory, nullptr);
		slot = {};
	}

	SlTextureId VulkanRenderer::CreateRenderTarget(int width, int height)
	{
		if (m_device == VK_NULL_HANDLE || width <= 0 || height <= 0)
			return 0;

		RenderTargetSlot slot;
		slot.width = width;
		slot.height = height;

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = kTargetFormat;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			| VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateImage(m_device, &imageInfo, nullptr, &slot.image) != VK_SUCCESS)
			return 0;

		VkMemoryRequirements requirements{};
		vkGetImageMemoryRequirements(m_device, slot.image, &requirements);
		VkMemoryAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = requirements.size;
		allocateInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (allocateInfo.memoryTypeIndex == UINT32_MAX
			|| vkAllocateMemory(m_device, &allocateInfo, nullptr, &slot.memory) != VK_SUCCESS
			|| vkBindImageMemory(m_device, slot.image, slot.memory, 0) != VK_SUCCESS)
		{
			DestroyRenderTarget(slot);
			return 0;
		}

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = slot.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = kTargetFormat;
		viewInfo.subresourceRange = {ColorAspect(), 0, 1, 0, 1};
		if (vkCreateImageView(m_device, &viewInfo, nullptr, &slot.view) != VK_SUCCESS)
		{
			DestroyRenderTarget(slot);
			return 0;
		}

		slot.layout = VK_IMAGE_LAYOUT_UNDEFINED;
		const SlTextureId id = m_nextTextureId++;
		m_renderTargets.emplace(id, slot);
		return id;
	}

	void VulkanRenderer::ReleaseRenderTarget(SlTextureId target) noexcept
	{
		const auto found = m_renderTargets.find(target);
		if (found == m_renderTargets.end())
			return;
		if (m_currentTarget == target)
			m_currentTarget = 0;
		if (m_device != VK_NULL_HANDLE)
			vkDeviceWaitIdle(m_device);
		DestroyRenderTarget(found->second);
		m_renderTargets.erase(found);
	}

	bool VulkanRenderer::BeginRenderTarget(SlTextureId target)
	{
		const auto found = m_renderTargets.find(target);
		if (found == m_renderTargets.end())
			return false;
		RenderTargetSlot& slot = found->second;
		if (slot.layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		{
			VkCommandBuffer commandBuffer = BeginOneShot();
			TransitionImage(commandBuffer, slot.image, slot.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
			EndOneShot(commandBuffer);
			slot.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		m_currentTarget = target;
		return true;
	}

	void VulkanRenderer::EndRenderTarget(SlTextureId target)
	{
		const auto found = m_renderTargets.find(target);
		if (found == m_renderTargets.end())
			return;
		RenderTargetSlot& slot = found->second;
		if (slot.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			VkCommandBuffer commandBuffer = BeginOneShot();
			TransitionImage(commandBuffer, slot.image, slot.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
			EndOneShot(commandBuffer);
			slot.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
	}

	VkImage VulkanRenderer::GetRenderTargetImage(SlTextureId target) const noexcept
	{
		const auto found = m_renderTargets.find(target);
		return found == m_renderTargets.end() ? VK_NULL_HANDLE : found->second.image;
	}

	VkImageView VulkanRenderer::GetRenderTargetView(SlTextureId target) const noexcept
	{
		const auto found = m_renderTargets.find(target);
		return found == m_renderTargets.end() ? VK_NULL_HANDLE : found->second.view;
	}

	bool VulkanRenderer::GetRenderTargetSize(SlTextureId target, int& outWidth, int& outHeight) const noexcept
	{
		const auto found = m_renderTargets.find(target);
		if (found == m_renderTargets.end())
			return false;
		outWidth = found->second.width;
		outHeight = found->second.height;
		return true;
	}
}
