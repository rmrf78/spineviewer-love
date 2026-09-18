#include "model_audio.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <xaudio2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace sl_audio
{

	namespace
	{
		template <class T>
		struct mf_holder
		{
			T* ptr = nullptr;
			~mf_holder() { if (ptr) ptr->Release(); }
			T** out() { if (ptr) { ptr->Release(); ptr = nullptr; } return &ptr; }
			T* get() const noexcept { return ptr; }
		};

		std::wstring lc(std::wstring s)
		{
			for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c));
			return s;
		}

		bool decode_to_pcm(const std::wstring& path, pcm_clip& out)
		{
			mf_holder<IMFAttributes> attribs;
			if (FAILED(::MFCreateAttributes(attribs.out(), 1))) return false;

			mf_holder<IMFSourceReader> reader;
			if (FAILED(::MFCreateSourceReaderFromURL(path.c_str(), attribs.get(), reader.out())))
				return false;

			mf_holder<IMFMediaType> partial;
			if (FAILED(::MFCreateMediaType(partial.out()))) return false;
			partial.get()->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
			partial.get()->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);

			if (FAILED(reader.get()->SetCurrentMediaType(
				static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, partial.get())))
				return false;

			mf_holder<IMFMediaType> actual;
			if (FAILED(reader.get()->GetCurrentMediaType(
				static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), actual.out())))
				return false;

			UINT32 rate = 0, channels = 0, bits = 0, block_align = 0;
			actual.get()->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
			actual.get()->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
			actual.get()->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
			actual.get()->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &block_align);
			if (rate == 0 || channels == 0 || bits == 0) return false;

			out.sample_rate = rate;
			out.channels = static_cast<std::uint16_t>(channels);
			out.bits = static_cast<std::uint16_t>(bits);
			out.block_align = static_cast<std::uint16_t>(block_align != 0
				? block_align : (channels * bits / 8));

			reader.get()->SetStreamSelection(
				static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
			reader.get()->SetStreamSelection(
				static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);

			for (;;)
			{
				mf_holder<IMFSample> sample;
				DWORD flags = 0;
				HRESULT hr = reader.get()->ReadSample(
					static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
					0, nullptr, &flags, nullptr, sample.out());
				if (FAILED(hr)) return false;
				if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
				if (sample.get() == nullptr) continue;

				mf_holder<IMFMediaBuffer> media;
				if (FAILED(sample.get()->ConvertToContiguousBuffer(media.out()))) return false;

				BYTE* ptr = nullptr;
				DWORD len = 0;
				if (FAILED(media.get()->Lock(&ptr, nullptr, &len))) return false;
				out.samples.insert(out.samples.end(), ptr, ptr + len);
				media.get()->Unlock();
			}
			return !out.samples.empty();
		}

		WAVEFORMATEX make_wfx(const pcm_clip& clip)
		{
			WAVEFORMATEX w{};
			w.wFormatTag = WAVE_FORMAT_PCM;
			w.nChannels = clip.channels;
			w.nSamplesPerSec = clip.sample_rate;
			w.wBitsPerSample = clip.bits;
			w.nBlockAlign = clip.block_align;
			w.nAvgBytesPerSec = w.nSamplesPerSec * w.nBlockAlign;
			w.cbSize = 0;
			return w;
		}
	}

	struct ModelAudio::voice_sink : public IXAudio2VoiceCallback
	{
		ModelAudio* owner = nullptr;

		void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
		void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
		void STDMETHODCALLTYPE OnStreamEnd() override
		{
			if (!owner) return;
			if (owner)
			{
				owner->m_voice_active.store(false, std::memory_order_release);
				owner->m_voice_ended.store(true, std::memory_order_release);
			}
		}
		void STDMETHODCALLTYPE OnBufferStart(void*) override {}
		void STDMETHODCALLTYPE OnBufferEnd(void*) override {}
		void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
		void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override
		{
			if (owner == nullptr) return;
			if (owner)
			{
				owner->m_voice_active.store(false, std::memory_order_release);
				owner->m_voice_ended.store(true, std::memory_order_release);
			}
		}
	};

	ModelAudio::ModelAudio() = default;
	ModelAudio::~ModelAudio() { shut(); }

	bool ModelAudio::boot()
	{
		if (m_x2 != nullptr) return true;

		if (!m_mf_started)
		{
			if (FAILED(::MFStartup(MF_VERSION, MFSTARTUP_LITE))) return false;
			m_mf_started = true;
		}

		if (FAILED(::XAudio2Create(&m_x2, 0, XAUDIO2_DEFAULT_PROCESSOR)) || m_x2 == nullptr)
		{
			::MFShutdown();
			m_mf_started = false;
			return false;
		}
		if (FAILED(m_x2->CreateMasteringVoice(&m_master)) || m_master == nullptr)
		{
			m_x2->Release();
			m_x2 = nullptr;
			::MFShutdown();
			m_mf_started = false;
			return false;
		}

		m_voice_sink = std::make_unique<voice_sink>();
		m_voice_sink->owner = this;

		return true;
	}

	void ModelAudio::shut()
	{
		release_voice_voice();

		if (m_master) { m_master->DestroyVoice(); m_master = nullptr; }
		if (m_x2) { m_x2->Release(); m_x2 = nullptr; }

		if (m_mf_started)
		{
			::MFShutdown();
			m_mf_started = false;
		}

		m_voice_sink.reset();

		{
			std::lock_guard<std::mutex> g(m_cache_mutex);
			m_cache.clear();
		}
	}

	void ModelAudio::release_voice_voice()
	{
		if (m_voice_voice)
		{
			m_voice_voice->Stop(0);
			m_voice_voice->FlushSourceBuffers();
			m_voice_voice->DestroyVoice();
			m_voice_voice = nullptr;
		}
		m_voice_clip.reset();
		m_voice_active.store(false, std::memory_order_release);
		m_voice_ended.store(false, std::memory_order_release);
	}

	std::shared_ptr<const pcm_clip> ModelAudio::decode_or_cache(const std::wstring& path)
	{
		if (!m_mf_started) return nullptr;

		const std::wstring key = lc(path);
		{
			std::lock_guard<std::mutex> g(m_cache_mutex);
			auto it = m_cache.find(key);
			if (it != m_cache.end()) return it->second;
		}

		auto clip = std::make_shared<pcm_clip>();
		if (!decode_to_pcm(path, *clip)) return nullptr;

		{
			std::lock_guard<std::mutex> g(m_cache_mutex);

			auto [it, inserted] = m_cache.emplace(key, clip);
			return it->second;
		}
	}

	bool ModelAudio::voice_start(const std::wstring& file_path, float volume, bool loop)
	{
		if (!ready()) return false;
		auto clip = decode_or_cache(file_path);
		if (!clip || clip->empty()) return false;

		release_voice_voice();

		WAVEFORMATEX wfx = make_wfx(*clip);
		if (FAILED(m_x2->CreateSourceVoice(&m_voice_voice, &wfx, 0, 2.0f, m_voice_sink.get())) ||
			m_voice_voice == nullptr)
		{
			m_voice_voice = nullptr;
			return false;
		}

		m_voice_clip = clip;
		m_voice_volume = std::clamp(volume, 0.f, 1.f);
		m_voice_ended.store(false, std::memory_order_release);

		XAUDIO2_BUFFER xb{};
		xb.AudioBytes = static_cast<UINT32>(m_voice_clip->samples.size());
		xb.pAudioData = m_voice_clip->samples.data();
		xb.Flags = XAUDIO2_END_OF_STREAM;
		if (loop)
		{
			xb.LoopCount = XAUDIO2_LOOP_INFINITE;
			xb.LoopBegin = 0;
			xb.LoopLength = 0;
		}
		if (FAILED(m_voice_voice->SubmitSourceBuffer(&xb)))
		{
			release_voice_voice();
			return false;
		}
		m_voice_voice->SetVolume(m_voice_volume);
		if (FAILED(m_voice_voice->Start(0)))
		{
			release_voice_voice();
			return false;
		}
		m_voice_active.store(true, std::memory_order_release);
		m_voice_start_tick = ::GetTickCount64();
		return true;
	}

	void ModelAudio::voice_stop()
	{
		release_voice_voice();
	}

	void ModelAudio::voice_set_volume(float v)
	{
		m_voice_volume = std::clamp(v, 0.f, 1.f);
		if (m_voice_voice) m_voice_voice->SetVolume(m_voice_volume);
	}

	bool ModelAudio::voice_is_active() const
	{
		return m_voice_active.load(std::memory_order_acquire);
	}

	std::uint64_t ModelAudio::voice_elapsed_ms() const
	{
		if (m_voice_start_tick == 0) return 0;
		const std::uint64_t now = ::GetTickCount64();
		return now > m_voice_start_tick ? now - m_voice_start_tick : 0;
	}

	float ModelAudio::voice_level() const
	{
		if (!m_voice_active.load(std::memory_order_acquire))
			return 0.0f;
		std::shared_ptr<const pcm_clip> clip = m_voice_clip;
		if (!clip || clip->empty() || clip->sample_rate == 0 ||
			clip->block_align == 0 || clip->bits != 16)
		{
			return 0.0f;
		}

		const std::uint64_t frameCount = clip->samples.size() / clip->block_align;
		if (frameCount == 0)
			return 0.0f;

		const std::uint64_t positionFrame =
			(voice_elapsed_ms() * clip->sample_rate / 1000) % frameCount;

		const std::uint64_t windowFrames = (std::max<std::uint64_t>)(1, clip->sample_rate / 20);
		const std::uint64_t beginFrame = positionFrame > windowFrames / 2 ? positionFrame - windowFrames / 2 : 0;
		const std::uint64_t endFrame = (std::min<std::uint64_t>)(frameCount, beginFrame + windowFrames);

		const std::int16_t* samples = reinterpret_cast<const std::int16_t*>(clip->samples.data());
		const std::uint32_t stride = clip->block_align / 2;
		double accumulated = 0.0;
		std::uint64_t counted = 0;
		for (std::uint64_t frame = beginFrame; frame < endFrame; ++frame)
		{
			const double value = static_cast<double>(samples[frame * stride]) / 32768.0;
			accumulated += value * value;
			++counted;
		}
		if (counted == 0)
			return 0.0f;
		return static_cast<float>(std::sqrt(accumulated / static_cast<double>(counted)));
	}

}
