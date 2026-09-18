#include "runtime_api.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#if !defined(SL_SPINE_C_NO_CLIPPING)
#include <spine/SkeletonClipping.h>
#endif
#if defined(SL_SPINE_C_HAS_WEIGHTED_MESH)
#include <spine/WeightedMeshAttachment.h>
#endif
#include <spine/extension.h>
#include <spine/spine.h>

#ifndef SL_RUNTIME_FACTORY_NAME
#error SL_RUNTIME_FACTORY_NAME must be defined.
#endif

#ifndef SL_RUNTIME_DISPLAY_VERSION
#error SL_RUNTIME_DISPLAY_VERSION must be defined.
#endif

#ifndef SL_RUNTIME_KIND_VALUE
#error SL_RUNTIME_KIND_VALUE must be defined.
#endif

namespace {

struct RuntimeTexture
{
	sl_runtime_v2::TextureInfo info;
};

struct RuntimeSlotOverride
{
	float alpha = 1.0f;
	sl_runtime_v2::SlotAttachmentMode attachmentMode = sl_runtime_v2::SlotAttachmentMode::Preserve;
};

#if defined(SL_SPINE_C_NO_BINARY)
struct SlSkinEntry
{
	int slotIndex;
	const char* name;
	spAttachment* attachment;
	SlSkinEntry* next;
};
struct SlSkinInternal
{
	spSkin super;
	SlSkinEntry* entries;
};
#else
using SlSkinEntry = _Entry;
using SlSkinInternal = _spSkin;
#endif

std::vector<std::unique_ptr<RuntimeTexture>>* g_loadingTextures = nullptr;

std::string ReadWholeFile(const std::string& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return {};
	file.seekg(0, std::ios::end);
	const std::streamoff size = file.tellg();
	file.seekg(0, std::ios::beg);
	std::string data;
	data.resize(static_cast<size_t>(size));
	if (size > 0)
		file.read(&data[0], size);
	return data;
}

std::string DirectoryOf(const std::string& path)
{
	const size_t pos = path.find_last_of("/\\");
	if (pos == std::string::npos)
		return {};
	return path.substr(0, pos + 1);
}

sl_runtime_v2::Color MakeColor(float r, float g, float b, float a)
{
	sl_runtime_v2::Color color;
	color.r = r;
	color.g = g;
	color.b = b;
	color.a = a;
	return color;
}

sl_runtime_v2::Color SkeletonColor(const spSkeleton* skeleton)
{
#if defined(SL_SPINE_CPP_LEGACY_COLOR_FIELDS)
	return MakeColor(skeleton->r, skeleton->g, skeleton->b, skeleton->a);
#else
	return MakeColor(skeleton->color.r, skeleton->color.g, skeleton->color.b, skeleton->color.a);
#endif
}

sl_runtime_v2::Color SlotColor(const spSlot* slot)
{
#if defined(SL_SPINE_CPP_LEGACY_COLOR_FIELDS)
	return MakeColor(slot->r, slot->g, slot->b, slot->a);
#else
	return MakeColor(slot->color.r, slot->color.g, slot->color.b, slot->color.a);
#endif
}

sl_runtime_v2::Color RegionColor(const spRegionAttachment* region)
{
#if defined(SL_SPINE_CPP_LEGACY_COLOR_FIELDS)
	return MakeColor(region->r, region->g, region->b, region->a);
#else
	return MakeColor(region->color.r, region->color.g, region->color.b, region->color.a);
#endif
}

sl_runtime_v2::Color MeshColor(const spMeshAttachment* mesh)
{
#if defined(SL_SPINE_CPP_LEGACY_COLOR_FIELDS)
	return MakeColor(mesh->r, mesh->g, mesh->b, mesh->a);
#else
	return MakeColor(mesh->color.r, mesh->color.g, mesh->color.b, mesh->color.a);
#endif
}

sl_runtime_v2::Color MultiplyColor(const sl_runtime_v2::Color& a, const sl_runtime_v2::Color& b, const sl_runtime_v2::Color& c)
{
	return MakeColor(a.r * b.r * c.r, a.g * b.g * c.g, a.b * b.b * c.b, a.a * b.a * c.a);
}

#if !defined(SL_SPINE_C_SLOT_ADDITIVE_BLENDING)
sl_runtime_v2::BlendMode ConvertBlendMode(spBlendMode blendMode)
{
	switch (blendMode)
	{
	case SP_BLEND_MODE_ADDITIVE: return sl_runtime_v2::BlendMode::Additive;
	case SP_BLEND_MODE_MULTIPLY: return sl_runtime_v2::BlendMode::Multiply;
	case SP_BLEND_MODE_SCREEN: return sl_runtime_v2::BlendMode::Screen;
	case SP_BLEND_MODE_NORMAL:
	default: return sl_runtime_v2::BlendMode::Normal;
	}
}
#endif

sl_runtime_v2::BlendMode ConvertSlotBlendMode(const spSlot* slot)
{
	if (slot == nullptr || slot->data == nullptr)
		return sl_runtime_v2::BlendMode::Normal;
#if defined(SL_SPINE_C_SLOT_ADDITIVE_BLENDING)
	return slot->data->additiveBlending ? sl_runtime_v2::BlendMode::Additive : sl_runtime_v2::BlendMode::Normal;
#else
	return ConvertBlendMode(slot->data->blendMode);
#endif
}

unsigned long long TextureIdFromRegion(const spAtlasRegion* region)
{
	if (region == nullptr || region->page == nullptr)
		return 0;
	const RuntimeTexture* texture = static_cast<const RuntimeTexture*>(region->page->rendererObject);
	return texture ? texture->info.id : 0;
}

bool TexturePremultipliedFromRegion(const spAtlasRegion* region)
{
	if (region == nullptr || region->page == nullptr)
		return false;
	const RuntimeTexture* texture = static_cast<const RuntimeTexture*>(region->page->rendererObject);
	return texture ? texture->info.renderPremultipliedAlpha : false;
}

}

extern "C" void _spAtlasPage_createTexture(spAtlasPage* page, const char* path)
{
	if (page == nullptr || g_loadingTextures == nullptr)
		return;

	std::unique_ptr<RuntimeTexture> texture(new RuntimeTexture());
	texture->info.id = static_cast<unsigned long long>(g_loadingTextures->size() + 1);
	texture->info.path = path ? path : "";
	RuntimeTexture* rawTexture = texture.get();
	g_loadingTextures->push_back(std::move(texture));
	page->rendererObject = rawTexture;
}

extern "C" void _spAtlasPage_disposeTexture(spAtlasPage* page)
{
	if (page != nullptr)
		page->rendererObject = nullptr;
}

extern "C" char* _spUtil_readFile(const char* path, int* length)
{
	if (length != nullptr)
		*length = 0;
	if (path == nullptr)
		return nullptr;

	std::string data = ReadWholeFile(path);
	if (data.empty())
		return nullptr;

	char* result = static_cast<char*>(std::malloc(data.size()));
	if (result == nullptr)
		return nullptr;
	std::memcpy(result, data.data(), data.size());
	if (length != nullptr)
		*length = static_cast<int>(data.size());
	return result;
}

namespace sl_runtime_v2 {
namespace {

class CAttachmentLoaderHandle
{
public:
	explicit CAttachmentLoaderHandle(spAtlas* atlas)
		: m_loader(spAtlasAttachmentLoader_create(atlas))
	{}

	~CAttachmentLoaderHandle()
	{
		if (m_loader != nullptr)
			spAttachmentLoader_dispose(&m_loader->super);
	}

	spAttachmentLoader* Get() const noexcept
	{
		return m_loader != nullptr ? &m_loader->super : nullptr;
	}

private:
	spAtlasAttachmentLoader* m_loader = nullptr;
};

class CRuntimeAdapter final : public IRuntime
{
public:
	~CRuntimeAdapter() override
	{
		Clear();
	}

	RuntimeInfo Info() const noexcept override
	{
		return RuntimeInfo{ SL_RUNTIME_KIND_VALUE, SL_RUNTIME_DISPLAY_VERSION, SL_RUNTIME_DISPLAY_VERSION };
	}

	bool Load(const LoadRequest& request) override
	{
		Clear();
#if defined(SL_SPINE_C_HAS_BONE_Y_DOWN)
		spBone_setYDown(0);
#endif
		const bool useMemory = !request.atlasData.empty() || !request.skeletonData.empty();
		return useMemory ? LoadFromMemory(request) : LoadFromFiles(request);
	}

	void Clear() noexcept override
	{
		DisposeCompositeSkin();
#if !defined(SL_SPINE_C_NO_CLIPPING)
		if (m_clipper != nullptr)
			spSkeletonClipping_dispose(m_clipper);
#endif
		if (m_state != nullptr)
			spAnimationState_dispose(m_state);
		if (m_stateData != nullptr)
			spAnimationStateData_dispose(m_stateData);
		if (m_skeleton != nullptr)
			spSkeleton_dispose(m_skeleton);
		if (m_skeletonData != nullptr)
			spSkeletonData_dispose(m_skeletonData);
		if (m_atlas != nullptr)
			spAtlas_dispose(m_atlas);

		m_state = nullptr;
		m_stateData = nullptr;
		m_skeleton = nullptr;
		m_skeletonData = nullptr;
		m_atlas = nullptr;
#if !defined(SL_SPINE_C_NO_CLIPPING)
		m_clipper = nullptr;
#endif
		m_textures.clear();
		m_textureInfos.clear();
		m_animationNames.clear();
		m_skinNames.clear();
		m_slotNames.clear();
		m_slotOverrides.clear();
		m_lastError.clear();
	}

	bool HasSkeleton() const noexcept override
	{
		return m_skeleton != nullptr;
	}

	void Update(float deltaSeconds) override
	{
		if (m_skeleton == nullptr || m_state == nullptr)
			return;
		spAnimationState_update(m_state, deltaSeconds);
#if defined(SL_SPINE_C_RESET_POSE_BEFORE_APPLY)
		spSkeleton_setToSetupPose(m_skeleton);
#endif
		spAnimationState_apply(m_state, m_skeleton);
		ApplySlotOverrides();
		spSkeleton_update(m_skeleton, deltaSeconds);
		spSkeleton_updateWorldTransform(m_skeleton);
	}

	void BuildFrame(int width, int height, Frame& outFrame) override
	{
		outFrame.width = width;
		outFrame.height = height;
		outFrame.draws.clear();
		if (m_skeleton == nullptr)
			return;

#if !defined(SL_SPINE_C_NO_CLIPPING)
		if (m_clipper != nullptr)
			spSkeletonClipping_clipEnd2(m_clipper);
#endif

		const Color skeletonColor = SkeletonColor(m_skeleton);
		for (int slotIndex = 0; slotIndex < m_skeleton->slotsCount; ++slotIndex)
		{
			spSlot* slot = m_skeleton->drawOrder[slotIndex];
			if (slot == nullptr)
				continue;

			if (slot->attachment == nullptr)
			{
				FinishClipAtSlot(slot);
				continue;
			}

			#if !defined(SL_SPINE_C_NO_CLIPPING)
			if (slot->attachment->type == SP_ATTACHMENT_CLIPPING)
			{
				if (m_clipper != nullptr)
					spSkeletonClipping_clipStart(m_clipper, slot, reinterpret_cast<spClippingAttachment*>(slot->attachment));
				continue;
			}
			#endif

			if (slot->attachment->type == SP_ATTACHMENT_REGION)
			{
				BuildRegionCommand(slot, skeletonColor, outFrame);
			}
			else if (slot->attachment->type == SP_ATTACHMENT_MESH)
			{
				BuildMeshCommand(slot, skeletonColor, outFrame);
			}
#if defined(SL_SPINE_C_HAS_SKINNED_MESH)
			else if (slot->attachment->type == SP_ATTACHMENT_SKINNED_MESH)
			{
				BuildSkinnedMeshCommand(slot, skeletonColor, outFrame);
			}
#endif
#if defined(SL_SPINE_C_HAS_WEIGHTED_MESH)
			else if (slot->attachment->type == SP_ATTACHMENT_WEIGHTED_MESH)
			{
				BuildWeightedMeshCommand(slot, skeletonColor, outFrame);
			}
#endif
			FinishClipAtSlot(slot);
		}

#if !defined(SL_SPINE_C_NO_CLIPPING)
		if (m_clipper != nullptr)
			spSkeletonClipping_clipEnd2(m_clipper);
#endif
	}

	const std::vector<std::string>& MotionNames() const noexcept override { return m_animationNames; }
	const std::vector<std::string>& LookNames() const noexcept override { return m_skinNames; }
	const std::vector<std::string>& SlotCatalog() const noexcept override { return m_slotNames; }
	const std::vector<TextureInfo>& TextureInfos() const noexcept override { return m_textureInfos; }

	void StartMotion(const char* name, bool loop) override
	{
		if (m_state != nullptr && name != nullptr)
			spAnimationState_setAnimationByName(m_state, 0, name, loop ? 1 : 0);
	}
	bool StartMotionWithMix(const char* name, bool loop, float mixSeconds) override
	{
		if (m_state == nullptr || m_skeletonData == nullptr || name == nullptr || name[0] == '\0' ||
			spSkeletonData_findAnimation(m_skeletonData, name) == nullptr) return false;
		spTrackEntry* entry = spAnimationState_setAnimationByName(m_state, 0, name, loop ? 1 : 0);
		if (entry && mixSeconds >= 0.0f) entry->mixDuration = mixSeconds;
		return entry != nullptr;
	}
	bool QueueMotion(const char* name, bool loop, float mixSeconds) override
	{
		if (m_state == nullptr || m_skeletonData == nullptr || name == nullptr || name[0] == '\0' ||
			spSkeletonData_findAnimation(m_skeletonData, name) == nullptr) return false;
		spTrackEntry* entry = spAnimationState_addAnimationByName(m_state, 0, name, loop ? 1 : 0, 0.0f);
		if (entry && mixSeconds >= 0.0f) entry->mixDuration = mixSeconds;
		return entry != nullptr;
	}
	bool SetCurrentMotionTimeScale(float timeScale) noexcept override
	{
		if (m_state == nullptr || m_state->tracksCount <= 0 || m_state->tracks == nullptr ||
			m_state->tracks[0] == nullptr) return false;
		m_state->tracks[0]->timeScale = (std::max)(0.0f, timeScale);
		return true;
	}

	float MotionDuration(const char* name) const override
	{
		if (m_skeletonData == nullptr || name == nullptr || name[0] == '\0')
			return 0.0f;
		spAnimation* animation = spSkeletonData_findAnimation(m_skeletonData, name);
		if (animation == nullptr)
			return 0.0f;
#if defined(SL_SPINE_C_ANIMATION_DURATION_FIELD)
		return animation->duration;
#else
		return animation->duration;
#endif
	}

	void SetMotionBlendSeconds(float seconds) override
	{
		if (m_stateData == nullptr)
			return;
		m_stateData->defaultMix = seconds > 0.0f ? seconds : 0.0f;
	}

	void SetSecondaryMotions(const std::vector<std::string>& names, bool loop) override
	{
		if (m_state == nullptr)
			return;

		for (int track = 1; track < m_state->tracksCount; ++track)
		{
#if defined(SL_SPINE_CPP_LEGACY_COLOR_FIELDS)
			spAnimationState_clearTrack(m_state, track);
#else
			spAnimationState_setEmptyAnimation(m_state, track, 0.0f);
#endif
		}

		if (names.empty())
			return;

		int trackIndex = 1;
		for (size_t i = 0; i < names.size(); ++i)
		{
			spAnimationState_setAnimationByName(m_state, trackIndex, names[i].c_str(), loop ? 1 : 0);
			++trackIndex;
		}
	}

	void ApplyLook(const char* name) override
	{
		if (m_skeleton == nullptr || m_skeletonData == nullptr || name == nullptr || name[0] == '\0')
			return;
		spSkin* skin = spSkeletonData_findSkin(m_skeletonData, name);
		if (skin == nullptr)
			return;
		spSkeleton_setSkin(m_skeleton, skin);
		spSkeleton_setSlotsToSetupPose(m_skeleton);
		ReapplyCurrentPose();
		DisposeCompositeSkin();
	}

	void ComposeLooks(const std::vector<std::string>& names) override
	{
		if (m_skeleton == nullptr || m_skeletonData == nullptr || names.empty())
			return;

		std::vector<spSkin*> skins;
		for (const std::string& name : names)
		{
			spSkin* skin = spSkeletonData_findSkin(m_skeletonData, name.c_str());
			if (skin != nullptr)
				skins.push_back(skin);
		}
		if (skins.empty())
			return;

		spSkin* combined = spSkin_create("__spinelove_composite");
		for (spSkin* skin : skins)
		{
			for (SlSkinEntry* entry = reinterpret_cast<SlSkinInternal*>(skin)->entries; entry != nullptr; entry = entry->next)
				spSkin_addAttachment(combined, entry->slotIndex, entry->name, entry->attachment);
		}

		spSkeleton_setSkin(m_skeleton, combined);
		spSkeleton_setSlotsToSetupPose(m_skeleton);
		ReapplyCurrentPose();
		DisposeCompositeSkin();
		m_compositeSkin = combined;
	}

	bool SetSlotOverride(const char* slotName, float alpha,
		SlotAttachmentMode attachmentMode) override
	{
		if (m_skeleton == nullptr || slotName == nullptr || slotName[0] == '\0') return false;
		spSlot* slot = spSkeleton_findSlot(m_skeleton, slotName);
		if (slot == nullptr) return false;
		RuntimeSlotOverride state;
		state.alpha = (std::max)(0.0f, (std::min)(1.0f, alpha));
		state.attachmentMode = attachmentMode;
		m_slotOverrides[slotName] = state;
		ApplySlotOverride(slot, state);
		spSkeleton_updateWorldTransform(m_skeleton);
		return true;
	}

	void ClearSlotOverrides() override { m_slotOverrides.clear(); }

	std::string LastError() const override
	{
		return m_lastError;
	}

private:
	void ApplySlotOverride(spSlot* slot, const RuntimeSlotOverride& state)
	{
		if (slot == nullptr) return;
		if (state.attachmentMode == SlotAttachmentMode::Clear)
			spSlot_setAttachment(slot, nullptr);
		else if (state.attachmentMode == SlotAttachmentMode::SetupIfEmpty && slot->attachment == nullptr)
			spSlot_setToSetupPose(slot);
		else if (state.attachmentMode == SlotAttachmentMode::NamedIfEmpty && slot->attachment == nullptr)
		{
			const int slotIndex = m_skeleton
				? spSkeleton_findSlotIndex(m_skeleton, slot->data->name) : -1;
			spAttachment* attachment = slotIndex >= 0
				? spSkeleton_getAttachmentForSlotIndex(m_skeleton, slotIndex, slot->data->name) : nullptr;
			if (attachment != nullptr) spSlot_setAttachment(slot, attachment);
			else spSlot_setToSetupPose(slot);
		}
#if defined(SL_SPINE_CPP_LEGACY_COLOR_FIELDS)
		slot->a = state.alpha;
#else
		slot->color.a = state.alpha;
#endif
	}

	void ApplySlotOverrides()
	{
		if (m_skeleton == nullptr) return;
		for (const auto& item : m_slotOverrides)
			ApplySlotOverride(spSkeleton_findSlot(m_skeleton, item.first.c_str()), item.second);
	}

	void ReapplyCurrentPose()
	{
		if (m_state != nullptr && m_skeleton != nullptr)
			spAnimationState_apply(m_state, m_skeleton);
		ApplySlotOverrides();
		if (m_skeleton != nullptr)
			spSkeleton_updateWorldTransform(m_skeleton);
	}

	void DisposeCompositeSkin() noexcept
	{
		if (m_compositeSkin == nullptr)
			return;
		if (m_skeleton != nullptr && m_skeleton->skin == m_compositeSkin)
			spSkeleton_setSkin(m_skeleton, nullptr);

		SlSkinInternal* skin = reinterpret_cast<SlSkinInternal*>(m_compositeSkin);
#if defined(SKIN_ENTRIES_HASH_TABLE_SIZE)
		for (int bucket = 0; bucket < SKIN_ENTRIES_HASH_TABLE_SIZE; ++bucket)
		{
			_SkinHashTableEntry* hashEntry = skin->entriesHashTable[bucket];
			while (hashEntry != nullptr)
			{
				_SkinHashTableEntry* next = hashEntry->next;
				FREE(hashEntry);
				hashEntry = next;
			}
			skin->entriesHashTable[bucket] = nullptr;
		}
#endif
		SlSkinEntry* entry = skin->entries;
		while (entry != nullptr)
		{
			SlSkinEntry* next = entry->next;
			FREE(entry->name);
			FREE(entry);
			entry = next;
		}
		skin->entries = nullptr;
		FREE(m_compositeSkin->name);
		FREE(m_compositeSkin);
		m_compositeSkin = nullptr;
	}

	bool LoadFromFiles(const LoadRequest& request)
	{
		if (request.atlasPaths.empty() || request.skeletonPaths.empty())
		{
			m_lastError = "No C runtime input file was provided.";
			return false;
		}

		g_loadingTextures = &m_textures;
		m_atlas = spAtlas_createFromFile(request.atlasPaths.front().c_str(), nullptr);
		g_loadingTextures = nullptr;
		if (m_atlas == nullptr)
		{
			m_lastError = "Failed to load C runtime atlas.";
			return false;
		}

		return LoadSkeletonData(request.skeletonPaths.front(), nullptr, request.binarySkeleton);
	}

	bool LoadFromMemory(const LoadRequest& request)
	{
		if (request.atlasData.empty() || request.skeletonData.empty())
		{
			m_lastError = "No C runtime memory data was provided.";
			return false;
		}

		const std::string directory = !request.textureDirectories.empty() ? request.textureDirectories.front() : std::string();
		g_loadingTextures = &m_textures;
		m_atlas = spAtlas_create(request.atlasData.front().data(), static_cast<int>(request.atlasData.front().size()), directory.c_str(), nullptr);
		g_loadingTextures = nullptr;
		if (m_atlas == nullptr)
		{
			m_lastError = "Failed to load C runtime atlas from memory.";
			return false;
		}

		const std::string& skeleton = request.skeletonData.front();
		return LoadSkeletonData(std::string(), &skeleton, request.binarySkeleton);
	}

	bool LoadSkeletonData(const std::string& skeletonPath, const std::string* skeletonData, bool binary)
	{
		CAttachmentLoaderHandle loader(m_atlas);
		if (loader.Get() == nullptr)
		{
			m_lastError = "Failed to create C runtime attachment loader.";
			return false;
		}

		if (binary)
		{
#if defined(SL_SPINE_C_NO_BINARY)
			m_lastError = "This official Spine runtime does not provide binary skeleton loading.";
			return false;
#else
			spSkeletonBinary* binaryLoader = spSkeletonBinary_createWithLoader(loader.Get());
			if (binaryLoader == nullptr)
			{
				m_lastError = "Failed to create C runtime binary loader.";
				return false;
			}
			if (skeletonData != nullptr)
			{
				m_skeletonData = spSkeletonBinary_readSkeletonData(binaryLoader,
					reinterpret_cast<const unsigned char*>(skeletonData->data()),
					static_cast<int>(skeletonData->size()));
			}
			else
			{
				m_skeletonData = spSkeletonBinary_readSkeletonDataFile(binaryLoader, skeletonPath.c_str());
			}
			if (m_skeletonData == nullptr && binaryLoader->error != nullptr)
				m_lastError = binaryLoader->error;
			spSkeletonBinary_dispose(binaryLoader);
#endif
		}
		else
		{
			spSkeletonJson* json = spSkeletonJson_createWithLoader(loader.Get());
			if (json == nullptr)
			{
				m_lastError = "Failed to create C runtime json loader.";
				return false;
			}
			if (skeletonData != nullptr)
				m_skeletonData = spSkeletonJson_readSkeletonData(json, skeletonData->c_str());
			else
				m_skeletonData = spSkeletonJson_readSkeletonDataFile(json, skeletonPath.c_str());
			if (m_skeletonData == nullptr && json->error != nullptr)
				m_lastError = json->error;
			spSkeletonJson_dispose(json);
		}

		if (m_skeletonData == nullptr)
		{
			if (m_lastError.empty())
				m_lastError = "Failed to load C runtime skeleton data.";
			return false;
		}

		m_skeleton = spSkeleton_create(m_skeletonData);
		m_stateData = spAnimationStateData_create(m_skeletonData);
		m_state = spAnimationState_create(m_stateData);
#if !defined(SL_SPINE_C_NO_CLIPPING)
		m_clipper = spSkeletonClipping_create();
		if (m_skeleton == nullptr || m_stateData == nullptr || m_state == nullptr || m_clipper == nullptr)
#else
		if (m_skeleton == nullptr || m_stateData == nullptr || m_state == nullptr)
#endif
		{
			m_lastError = "Failed to create C runtime skeleton state.";
			return false;
		}

		CollectNames();
		CollectTextureInfos();
		return true;
	}

	void CollectNames()
	{
		for (int i = 0; i < m_skeletonData->animationsCount; ++i)
		{
			const spAnimation* animation = m_skeletonData->animations[i];
			if (animation != nullptr && animation->name != nullptr)
				m_animationNames.push_back(animation->name);
		}
		for (int i = 0; i < m_skeletonData->skinsCount; ++i)
		{
			const spSkin* skin = m_skeletonData->skins[i];
			if (skin != nullptr && skin->name != nullptr)
				m_skinNames.push_back(skin->name);
		}
		for (int i = 0; i < m_skeletonData->slotsCount; ++i)
		{
			const spSlotData* slot = m_skeletonData->slots[i];
			if (slot != nullptr && slot->name != nullptr)
				m_slotNames.push_back(slot->name);
		}
	}

	void CollectTextureInfos()
	{
		for (const auto& texture : m_textures)
		{
			if (texture)
				m_textureInfos.push_back(texture->info);
		}
	}

	void BuildRegionCommand(spSlot* slot, const Color& skeletonColor, Frame& outFrame)
	{
		spRegionAttachment* region = reinterpret_cast<spRegionAttachment*>(slot->attachment);
		float vertices[8] = {};
#if defined(SL_SPINE_C_REGION_COMPUTE_LEGACY)
		spRegionAttachment_computeWorldVertices(region, slot->bone, vertices);
#else
		spRegionAttachment_computeWorldVertices(region, slot->bone, vertices, 0, 2);
#endif

		unsigned short indices[] = { 0, 1, 2, 2, 3, 0 };
		spAtlasRegion* atlasRegion = static_cast<spAtlasRegion*>(region->rendererObject);

		const Color tint = MultiplyColor(skeletonColor, SlotColor(slot), RegionColor(region));
		AppendTexturedGeometry(slot,
			atlasRegion,
			vertices,
			8,
			indices,
			6,
			region->uvs,
			tint,
			outFrame);
	}

	void BuildMeshCommand(spSlot* slot, const Color& skeletonColor, Frame& outFrame)
	{
		spMeshAttachment* mesh = reinterpret_cast<spMeshAttachment*>(slot->attachment);
		int worldVertexCount = 0;
#if defined(SL_SPINE_C_MESH_COMPUTE_LEGACY)
#if defined(SL_SPINE_C_MESH_WORLD_VERTICES_LENGTH)
		worldVertexCount = mesh->super.worldVerticesLength;
#elif defined(SL_SPINE_C_MESH_DIRECT_VERTICES)
		worldVertexCount = mesh->verticesCount;
#else
		worldVertexCount = mesh->super.verticesCount;
#endif
#else
		worldVertexCount = mesh->super.worldVerticesLength;
#endif
		if (worldVertexCount <= 0 || mesh->trianglesCount <= 0)
			return;

		std::vector<float> worldVertices(static_cast<size_t>(worldVertexCount));
#if defined(SL_SPINE_C_MESH_COMPUTE_LEGACY)
		spMeshAttachment_computeWorldVertices(mesh, slot, worldVertices.data());
#else
		spVertexAttachment_computeWorldVertices(&mesh->super, slot, 0, worldVertexCount, worldVertices.data(), 0, 2);
#endif

		spAtlasRegion* atlasRegion = static_cast<spAtlasRegion*>(mesh->rendererObject);
		const Color tint = MultiplyColor(skeletonColor, SlotColor(slot), MeshColor(mesh));
		AppendTexturedGeometry(slot,
			atlasRegion,
			worldVertices.data(),
			worldVertexCount,
			mesh->triangles,
			mesh->trianglesCount,
			mesh->uvs,
			tint,
			outFrame);
	}

#if defined(SL_SPINE_C_HAS_SKINNED_MESH)
	void BuildSkinnedMeshCommand(spSlot* slot, const Color& skeletonColor, Frame& outFrame)
	{
		spSkinnedMeshAttachment* mesh = reinterpret_cast<spSkinnedMeshAttachment*>(slot->attachment);
		const int worldVertexCount = mesh->uvsCount;
		if (worldVertexCount <= 0 || mesh->trianglesCount <= 0)
			return;

		std::vector<float> worldVertices(static_cast<size_t>(worldVertexCount));
		spSkinnedMeshAttachment_computeWorldVertices(mesh, slot, worldVertices.data());

		spAtlasRegion* atlasRegion = static_cast<spAtlasRegion*>(mesh->rendererObject);
		const Color tint = MultiplyColor(skeletonColor, SlotColor(slot), MakeColor(mesh->r, mesh->g, mesh->b, mesh->a));
		AppendTexturedGeometry(slot,
			atlasRegion,
			worldVertices.data(),
			worldVertexCount,
			mesh->triangles,
			mesh->trianglesCount,
			mesh->uvs,
			tint,
			outFrame);
	}
#endif

#if defined(SL_SPINE_C_HAS_WEIGHTED_MESH)
	void BuildWeightedMeshCommand(spSlot* slot, const Color& skeletonColor, Frame& outFrame)
	{
		spWeightedMeshAttachment* mesh = reinterpret_cast<spWeightedMeshAttachment*>(slot->attachment);
		const int worldVertexCount = mesh->uvsCount;
		if (worldVertexCount <= 0 || mesh->trianglesCount <= 0)
			return;

		std::vector<float> worldVertices(static_cast<size_t>(worldVertexCount));
		spWeightedMeshAttachment_computeWorldVertices(mesh, slot, worldVertices.data());

		spAtlasRegion* atlasRegion = static_cast<spAtlasRegion*>(mesh->rendererObject);
		const Color meshColor = MakeColor(mesh->r, mesh->g, mesh->b, mesh->a);
		const Color tint = MultiplyColor(skeletonColor, SlotColor(slot), meshColor);
		AppendTexturedGeometry(slot,
			atlasRegion,
			worldVertices.data(),
			worldVertexCount,
			mesh->triangles,
			mesh->trianglesCount,
			mesh->uvs,
			tint,
			outFrame);
	}
#endif

	void FinishClipAtSlot(spSlot* slot)
	{
#if !defined(SL_SPINE_C_NO_CLIPPING)
		if (m_clipper != nullptr && slot != nullptr)
			spSkeletonClipping_clipEnd(m_clipper, slot);
#else
		(void)slot;
#endif
	}

	void AppendTexturedGeometry(
		spSlot* slot,
		spAtlasRegion* atlasRegion,
		float* worldVertices,
		int worldVertexCount,
		unsigned short* indices,
		int indexCount,
		float* uvs,
		const Color& tint,
		Frame& outFrame)
	{
		const unsigned long long textureId = TextureIdFromRegion(atlasRegion);
		if (textureId == 0 || worldVertexCount <= 0 || indexCount <= 0 || worldVertices == nullptr || indices == nullptr || uvs == nullptr)
			return;

		float* finalVertices = worldVertices;
		float* finalUvs = uvs;
		unsigned short* finalIndices = indices;
		int finalVertexCount = worldVertexCount;
		int finalIndexCount = indexCount;

#if !defined(SL_SPINE_C_NO_CLIPPING)
		if (m_clipper != nullptr && spSkeletonClipping_isClipping(m_clipper))
		{
			spSkeletonClipping_clipTriangles(m_clipper, worldVertices, worldVertexCount, indices, indexCount, uvs, 2);
			finalVertices = m_clipper->clippedVertices->items;
			finalUvs = m_clipper->clippedUVs->items;
			finalIndices = m_clipper->clippedTriangles->items;
			finalVertexCount = m_clipper->clippedVertices->size;
			finalIndexCount = m_clipper->clippedTriangles->size;
		}
#endif

		if (finalVertexCount <= 0 || finalIndexCount <= 0)
			return;

		DrawCommand command;
		command.textureId = textureId;
		command.slotName = slot != nullptr && slot->data != nullptr && slot->data->name != nullptr ? slot->data->name : "";
		command.blendMode = ConvertSlotBlendMode(slot);
		command.premultipliedAlpha = TexturePremultipliedFromRegion(atlasRegion);
		command.vertices.resize(static_cast<size_t>(finalVertexCount / 2));
		command.indices.reserve(static_cast<size_t>(finalIndexCount));
		for (int i = 0; i < finalIndexCount; ++i)
			command.indices.push_back(finalIndices[i]);

		for (int i = 0; i < finalVertexCount / 2; ++i)
		{
			command.vertices[i].x = finalVertices[i * 2];
			command.vertices[i].y = finalVertices[i * 2 + 1];
			command.vertices[i].u = finalUvs[i * 2];
			command.vertices[i].v = finalUvs[i * 2 + 1];
			command.vertices[i].color = tint;
		}
		outFrame.draws.push_back(std::move(command));
	}

	spAtlas* m_atlas = nullptr;
	spSkeletonData* m_skeletonData = nullptr;
	spSkeleton* m_skeleton = nullptr;
	spSkin* m_compositeSkin = nullptr;
	spAnimationStateData* m_stateData = nullptr;
	spAnimationState* m_state = nullptr;
#if !defined(SL_SPINE_C_NO_CLIPPING)
	spSkeletonClipping* m_clipper = nullptr;
#endif
	std::vector<std::unique_ptr<RuntimeTexture>> m_textures;
	std::vector<TextureInfo> m_textureInfos;
	std::vector<std::string> m_animationNames;
	std::vector<std::string> m_skinNames;
	std::vector<std::string> m_slotNames;
	std::unordered_map<std::string, RuntimeSlotOverride> m_slotOverrides;
	std::string m_lastError;
};

}

std::unique_ptr<IRuntime> SL_RUNTIME_FACTORY_NAME()
{
	return std::unique_ptr<IRuntime>(new CRuntimeAdapter());
}

}
