#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;
namespace sl_audio {
struct pcm_clip {
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 0, bits = 0, block_align = 0;
    std::vector<std::uint8_t> samples;
    bool empty() const noexcept { return samples.empty(); }
};
class ModelAudio {
public:
    ModelAudio();
    ~ModelAudio();
    ModelAudio(const ModelAudio&) = delete;
    ModelAudio& operator=(const ModelAudio&) = delete;
    bool boot();
    void shut();
    bool ready() const noexcept { return m_x2 != nullptr; }
    bool voice_start(const std::wstring& path, float volume, bool loop);
    void voice_stop();
    void voice_set_volume(float value);
    bool voice_is_active() const;
    std::uint64_t voice_elapsed_ms() const;
    float voice_level() const;
private:
    struct voice_sink;
    std::shared_ptr<const pcm_clip> decode_or_cache(const std::wstring& path);
    void release_voice_voice();
    IXAudio2* m_x2 = nullptr;
    IXAudio2MasteringVoice* m_master = nullptr;
    IXAudio2SourceVoice* m_voice_voice = nullptr;
    bool m_mf_started = false;
    std::shared_ptr<const pcm_clip> m_voice_clip;
    std::unique_ptr<voice_sink> m_voice_sink;
    float m_voice_volume = 1.0f;
    std::atomic<bool> m_voice_active{false}, m_voice_ended{false};
    std::uint64_t m_voice_start_tick = 0;
    mutable std::mutex m_cache_mutex;
    std::unordered_map<std::wstring,std::shared_ptr<const pcm_clip>> m_cache;
};
}
