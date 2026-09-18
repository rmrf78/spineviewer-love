#ifndef SPINELOVE_LIVE2D_MODULE_H_
#define SPINELOVE_LIVE2D_MODULE_H_

#include <memory>
#include <string>
#include <vector>
#include <array>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace sl_d3d11
{
	class D3D11Renderer;
}

namespace live2d
{
	struct ParameterState
	{
		std::string id;

		std::string displayName;
		float value = 0.0f;
		float minimum = 0.0f;
		float maximum = 0.0f;
		float defaultValue = 0.0f;
		bool overridden = false;
	};

	struct PartState
	{
		std::string id;
		std::string displayName;
		float opacity = 1.0f;
		float defaultOpacity = 1.0f;
		bool overridden = false;
	};

	struct DragSettings
	{
		float sensitivity = 1.0f;
		float angleX = 30.0f;
		float angleY = 30.0f;
		float angleZ = -30.0f;
		float bodyAngleX = 10.0f;
		float eyeBallX = 1.0f;
		float eyeBallY = 1.0f;
	};

	struct GazePose
	{
		bool enabled = false;
		std::array<float, 6> values{};
	};

	struct EffectSettings
	{
		bool eyeBlink = true;
		bool breath = true;
		bool physics = true;
		bool lipSync = true;
		bool gazeFollow = true;
	};

	struct RenderBounds
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};

	class Live2DModule
	{
	public:
		Live2DModule();
		~Live2DModule();

		Live2DModule(const Live2DModule&) = delete;
		Live2DModule& operator=(const Live2DModule&) = delete;

		bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, sl_d3d11::D3D11Renderer* textureRenderer);
		void Shutdown() noexcept;
		bool ImportModel(const std::wstring& manifestPath);
		void Clear() noexcept;

		bool TickAndRender(float deltaSeconds, int viewportWidth, int viewportHeight,
			float centerOffsetX = 0.0f, float centerOffsetY = 0.0f,
			bool captureBounds = false);
		bool QueryLastRenderedBounds(RenderBounds& outBounds) const noexcept;

		bool PlayMotion(size_t index);

		bool PlayMotionOnce(size_t index);
		bool IsMotionFinished() const noexcept;
		bool PlayExpression(size_t index);
		bool PlayRandomExpression();
		void ClearExpression();

		bool TapAt(float normalizedX, float normalizedY);

		std::string HitAreaAt(float normalizedX, float normalizedY) const;
		void SetTimeScale(float value) noexcept;
		float TimeScale() const noexcept;
		void SetModelScale(float value) noexcept;
		float ModelScale() const noexcept;
		void PanByPixels(float deltaX, float deltaY) noexcept;
		void SetViewOffset(float x, float y) noexcept;
		float ViewOffsetX() const noexcept;
		float ViewOffsetY() const noexcept;
		void ResetView() noexcept;
		void SetDragTarget(float normalizedX, float normalizedY) noexcept;
		void EndDrag() noexcept;
		void SetDragSettings(const DragSettings& settings) noexcept;
		DragSettings GetDragSettings() const noexcept;
		void ResetDragSettings() noexcept;
		std::array<int, 6> GazeParameterIndices() const noexcept;
		void SetGazePose(const GazePose& pose) noexcept;
		GazePose GetGazePose() const noexcept;
		void SetEffects(const EffectSettings& settings) noexcept;
		EffectSettings Effects() const noexcept;
		bool SetParameter(size_t index, float value);
		bool SetParameterOverride(size_t index, bool enabled);
		bool ResetParameter(size_t index);
		void ClearParameterOverrides() noexcept;
		bool SetPartOpacity(size_t index, float opacity);
		bool SetPartOverride(size_t index, bool enabled);
		bool ResetPart(size_t index);
		void SetVoiceVolume(float volume) noexcept;
		void StopVoice() noexcept;
		float VoiceVolume() const noexcept;

		void SetLoopAll(bool enabled) noexcept;
		bool LoopAll() const noexcept;

		bool BeginExportSession(size_t motionIndex, float fps);

		bool ExportSessionSwitchMotion(size_t motionIndex);
		void EndExportSession();
		bool ExportSessionActive() const noexcept;

		float MotionDuration(size_t index) const noexcept;

		unsigned int ModelGeneration() const noexcept;

		bool HasImportedModel() const noexcept;
		bool RenderingBackendAvailable() const noexcept;
		const std::wstring& ManifestPath() const noexcept;
		const std::string& DisplayName() const noexcept;
		const std::string& LastError() const noexcept;
		const std::vector<std::string>& MotionNames() const noexcept;
		const std::vector<float>& MotionDurations() const noexcept;
		const std::vector<std::string>& ExpressionNames() const noexcept;
		const std::vector<ParameterState>& Parameters() const noexcept;
		const std::vector<PartState>& Parts() const noexcept;
		int CurrentMotionIndex() const noexcept;
		int CurrentExpressionIndex() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}

#endif
