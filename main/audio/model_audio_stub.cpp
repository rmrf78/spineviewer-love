// Non-Windows stand-in for the XAudio2/Media Foundation based ModelAudio.
// Live2D lip sync and motion sounds are simply unavailable, but the module
// compiles and links unchanged.

#include "model_audio.h"

#if !defined(_WIN32)

namespace sl_audio
{
	struct ModelAudio::voice_sink
	{
	};

	ModelAudio::ModelAudio() = default;
	ModelAudio::~ModelAudio() = default;

	bool ModelAudio::boot() { return false; }
	void ModelAudio::shut() {}
	bool ModelAudio::voice_start(const std::wstring&, float, bool) { return false; }
	void ModelAudio::voice_stop() {}
	void ModelAudio::voice_set_volume(float) {}
	bool ModelAudio::voice_is_active() const { return false; }
	std::uint64_t ModelAudio::voice_elapsed_ms() const { return 0; }
	float ModelAudio::voice_level() const { return 0.0f; }
	std::shared_ptr<const pcm_clip> ModelAudio::decode_or_cache(const std::wstring&) { return {}; }
	void ModelAudio::release_voice_voice() {}
}

#endif
