#include "cpp_runtime_manager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

namespace {

bool HasBinaryExtension(const char* path)
{
	if (path == nullptr)
		return false;
	const std::string value(path);
	const std::size_t dot = value.find_last_of('.');
	return dot != std::string::npos && value.substr(dot) == ".skel";
}

int ExerciseRuntime(sl_runtime_v2::IRuntime& runtime, const char* atlasPath, const char* skeletonPath)
{
	sl_runtime_v2::LoadRequest request;
	request.atlasPaths.push_back(atlasPath);
	request.skeletonPaths.push_back(skeletonPath);
	request.binarySkeleton = HasBinaryExtension(skeletonPath);
	if (!runtime.Load(request)) return 20;
	if (!runtime.HasSkeleton()) return 21;
	if (runtime.MotionNames().empty()) return 22;
	if (runtime.LookNames().empty()) return 23;
	if (runtime.TextureInfos().empty()) return 24;

	runtime.StartMotion(runtime.MotionNames().front().c_str(), true);
	for (int i = 0; i < 10; ++i)
		runtime.Update(1.0f / 60.0f);
	sl_runtime_v2::Frame frame;
	runtime.BuildFrame(1280, 720, frame);
	if (frame.draws.empty()) return 25;
	for (const sl_runtime_v2::DrawCommand& draw : frame.draws)
	{
		if (draw.vertices.empty() || draw.indices.empty()) return 26;
	}
	return 0;
}

bool FramesMatch(const sl_runtime_v2::Frame& a, const sl_runtime_v2::Frame& b)
{
	if (a.draws.size() != b.draws.size())
		return false;
	for (std::size_t i = 0; i < a.draws.size(); ++i)
	{
		const sl_runtime_v2::DrawCommand& left = a.draws[i];
		const sl_runtime_v2::DrawCommand& right = b.draws[i];
		if (left.slotName != right.slotName || left.indices != right.indices || left.vertices.size() != right.vertices.size())
			return false;
		for (std::size_t vertex = 0; vertex < left.vertices.size(); ++vertex)
		{
			if (std::fabs(left.vertices[vertex].x - right.vertices[vertex].x) > 0.001f ||
				std::fabs(left.vertices[vertex].y - right.vertices[vertex].y) > 0.001f)
				return false;
		}
	}
	return true;
}

int VerifyEmptyMotionClearsPreviousPose(sl_runtime_v2::IRuntime& runtime, const char* atlasPath, const char* skeletonPath)
{
	const std::vector<std::string>& motions = runtime.MotionNames();
	const auto empty = std::find(motions.begin(), motions.end(), "empty");
	const auto source = std::find_if(motions.begin(), motions.end(), [](const std::string& name) { return name != "empty"; });
	if (empty == motions.end() || source == motions.end())
		return 0;

	runtime.StartMotion(source->c_str(), true);
	for (int i = 0; i < 30; ++i)
		runtime.Update(1.0f / 60.0f);
	runtime.StartMotion(empty->c_str(), true);
	runtime.Update(1.0f / 60.0f);
	sl_runtime_v2::Frame switchedFrame;
	runtime.BuildFrame(1280, 720, switchedFrame);

	std::unique_ptr<sl_runtime_v2::IRuntime> clean = sl_runtime_v2::CreateCpp34Runtime();
	if (!clean) return 27;
	sl_runtime_v2::LoadRequest request;
	request.atlasPaths.push_back(atlasPath);
	request.skeletonPaths.push_back(skeletonPath);
	request.binarySkeleton = HasBinaryExtension(skeletonPath);
	if (!clean->Load(request)) return 28;
	clean->StartMotion(empty->c_str(), true);
	clean->Update(1.0f / 60.0f);
	sl_runtime_v2::Frame cleanFrame;
	clean->BuildFrame(1280, 720, cleanFrame);
	return FramesMatch(switchedFrame, cleanFrame) ? 0 : 29;
}

}

int main(int argc, char** argv)
{
	std::unique_ptr<sl_runtime_v2::IRuntime> runtime31 = sl_runtime_v2::CreateCpp31Runtime();
	if (!runtime31) return 1;
	const sl_runtime_v2::RuntimeInfo info31 = runtime31->Info();
	if (info31.kind != sl_runtime_v2::RuntimeKind::Cpp31) return 2;
	if (info31.versionPrefix == nullptr || std::strcmp(info31.versionPrefix, "3.1") != 0) return 3;

	std::unique_ptr<sl_runtime_v2::IRuntime> runtime34 = sl_runtime_v2::CreateCpp34Runtime();
	if (!runtime34) return 4;
	const sl_runtime_v2::RuntimeInfo info34 = runtime34->Info();
	if (info34.kind != sl_runtime_v2::RuntimeKind::Cpp34) return 5;
	if (info34.versionPrefix == nullptr || std::strcmp(info34.versionPrefix, "3.4") != 0) return 6;

	if (argc == 3)
	{
		const int result = ExerciseRuntime(*runtime31, argv[1], argv[2]);
		if (result != 0) return result;
	}
	else if (argc == 4)
	{
		sl_runtime_v2::IRuntime* runtime = nullptr;
		if (std::strcmp(argv[1], "3.1") == 0)
			runtime = runtime31.get();
		else if (std::strcmp(argv[1], "3.4") == 0)
			runtime = runtime34.get();
		else
			return 7;
		const int result = ExerciseRuntime(*runtime, argv[2], argv[3]);
		if (result != 0) return result;
		if (runtime == runtime34.get())
		{
			const int switchResult = VerifyEmptyMotionClearsPreviousPose(*runtime, argv[2], argv[3]);
			if (switchResult != 0) return switchResult;
		}
	}
	else if (argc != 1) return 8;

	sl_runtime_v2::CppRuntimeManager manager;
	if (!manager.Resolve("3.8")) return 30;
	if (!manager.Resolve("4.0")) return 31;
	if (!manager.Resolve("4.1")) return 32;
	if (!manager.Resolve("4.2")) return 33;
	return 0;
}
