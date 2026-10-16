// Stand-in for the Live2D bridge when the bundled Cubism backend is disabled
// (SPINELOVE_ENABLE_LIVE2D=OFF), for example on Termux where the Cubism Core
// is only available for glibc. The viewer keeps working, Live2D reports as
// unavailable.

#include "live2d_bridge.h"

#include <QVariantMap>

struct Live2DBridge::Impl
{
};

Live2DBridge::Live2DBridge() : m(std::make_unique<Impl>()) {}
Live2DBridge::~Live2DBridge() = default;

void Live2DBridge::command(const QString&, const QVariant&) {}
QVariantMap Live2DBridge::state() const
{
    return QVariantMap{{"mode", "live2d"}, {"loaded", false}, {"backendAvailable", false}};
}

#if defined(Q_OS_WIN)
bool Live2DBridge::initialize(ID3D11Device*, ID3D11DeviceContext*, const QString&) { return false; }
#else
bool Live2DBridge::initialize(const VulkanHandles&) { return false; }
#endif

bool Live2DBridge::processPendingCommands() { return false; }
bool Live2DBridge::render(float, int, int, float, float) { return false; }
quint64 Live2DBridge::nativeTexture() const noexcept { return 0; }
QSize Live2DBridge::textureSize() const noexcept { return {}; }
void Live2DBridge::shutdown() {}

bool Live2DBridge::beginExportSession(int, float) { return false; }
bool Live2DBridge::exportSessionSwitchMotion(int) { return false; }
void Live2DBridge::endExportSession() {}
