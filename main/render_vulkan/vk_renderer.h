#ifndef SPINELOVE_RENDER_VULKAN_VK_RENDERER_H_
#define SPINELOVE_RENDER_VULKAN_VK_RENDERER_H_

#include <cstdint>
#include <string>
#include <unordered_map>

#include <vulkan/vulkan.h>

#include "../sl_gfx_types.h"
#include <Rendering/Vulkan/CubismClass_Vulkan.hpp>

namespace sl_vk
{
	// Owns the Vulkan objects the Live2D module needs outside of the Cubism
	// renderer itself: model textures, the offscreen colour target and the
	// small command-buffer helpers used to move images between layouts.
	class VulkanRenderer
	{
	public:
		void Initialize(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue, uint32_t queueFamily);
		void Shutdown() noexcept;

		VkPhysicalDevice PhysicalDevice() const noexcept { return m_physicalDevice; }
		VkDevice Device() const noexcept { return m_device; }
		VkQueue Queue() const noexcept { return m_queue; }
		uint32_t QueueFamily() const noexcept { return m_queueFamily; }
		VkCommandPool CommandPool() const noexcept { return m_commandPool; }

		// Loads an image file into GPU memory and leaves it in
		// VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ready for Cubism.
		SlTextureId LoadTexture(const char* utf8Path, bool premultiplyAlpha, bool generateMips);
		void ReleaseTexture(SlTextureId texture) noexcept;
		Csm::CubismImageVulkan* GetCubismImage(SlTextureId texture) noexcept;

		SlTextureId CreateRenderTarget(int width, int height);
		void ReleaseRenderTarget(SlTextureId target) noexcept;
		bool BeginRenderTarget(SlTextureId target);
		void EndRenderTarget(SlTextureId target);
		VkImage GetRenderTargetImage(SlTextureId target) const noexcept;
		VkImageView GetRenderTargetView(SlTextureId target) const noexcept;
		bool GetRenderTargetSize(SlTextureId target, int& outWidth, int& outHeight) const noexcept;

		SlTextureId CurrentRenderTarget() const noexcept { return m_currentTarget; }
		VkImage CurrentRenderTargetImage() const noexcept { return GetRenderTargetImage(m_currentTarget); }
		VkImageView CurrentRenderTargetView() const noexcept { return GetRenderTargetView(m_currentTarget); }
		bool CurrentRenderTargetSize(int& outWidth, int& outHeight) const noexcept
		{
			return GetRenderTargetSize(m_currentTarget, outWidth, outHeight);
		}

		VkCommandBuffer BeginOneShot() const;
		void EndOneShot(VkCommandBuffer commandBuffer) const;

		void TransitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout from, VkImageLayout to,
			VkPipelineStageFlags sourceStage, VkPipelineStageFlags destinationStage,
			VkAccessFlags sourceAccess, VkAccessFlags destinationAccess) const;

	private:
		struct TextureSlot
		{
			Csm::CubismImageVulkan image;
		};

		struct RenderTargetSlot
		{
			VkImage image = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
			int width = 0;
			int height = 0;
		};

		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
		void DestroyRenderTarget(RenderTargetSlot& slot) noexcept;

		VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
		VkDevice m_device = VK_NULL_HANDLE;
		VkQueue m_queue = VK_NULL_HANDLE;
		VkCommandPool m_commandPool = VK_NULL_HANDLE;
		uint32_t m_queueFamily = 0;
		std::unordered_map<SlTextureId, TextureSlot> m_textures;
		std::unordered_map<SlTextureId, RenderTargetSlot> m_renderTargets;
		SlTextureId m_nextTextureId = 1;
		SlTextureId m_currentTarget = 0;
	};
}

#endif
