#ifndef SPINELOVE_RUNTIME_SHARED_SPINE_ANIMATION_DECK_H_
#define SPINELOVE_RUNTIME_SHARED_SPINE_ANIMATION_DECK_H_

#include <string>
#include <vector>

using SlNameList = std::vector<std::string>;

struct SlMotionEvent
{
	std::string name;
	std::string stringValue;
	int intValue = 0;
	float floatValue = 0.0f;
	float time = 0.0f;
};

using SlMotionEventList = std::vector<SlMotionEvent>;

class SlMotionDeck
{
public:
	virtual ~SlMotionDeck() = default;

	virtual void TickPlayback(float deltaSeconds) = 0;
	virtual void StepToNextMotion() = 0;
	virtual void StepToPreviousMotion() = 0;
	virtual void PlayMotionByIndex(size_t index) = 0;
	virtual void PlayMotionByName(const char* motionId) = 0;
	virtual bool StartMotionByName(const char* motionId, bool loop, float mixSeconds = -1.0f) = 0;
	virtual bool QueueMotionByName(const char* motionId, bool loop, float mixSeconds = -1.0f) = 0;
	virtual bool SetActiveMotionTimeScale(float timeScale) noexcept = 0;
	virtual void RestartMotion(bool loop = true) = 0;
	virtual void SetLayeredMotions(const SlNameList& motions, bool loop = false) = 0;

	virtual bool StartMotionOnTrack(int, const char*, bool, float) { return false; }
	virtual bool ClearMotionTrack(int, float) { return false; }
	virtual bool HoldMotionTrack(int, float) { return false; }
	virtual void SetBlendWindowSeconds(float mixTime) = 0;
	virtual void SetTimeScale(float timeScale) = 0;
	virtual bool SetTimeScaleAt(size_t index, float timeScale) noexcept = 0;
	virtual float TimeScale() const noexcept = 0;
	virtual std::string ActiveMotionName() = 0;
	virtual void ReadMotionClock(float* track, float* last, float* start, float* end) = 0;
	virtual float MotionDuration(const char* motionId) = 0;
	virtual const SlNameList& MotionNames() const noexcept = 0;
	virtual void DrainMotionEventsAt(size_t index, SlMotionEventList& events) = 0;
};

#endif
