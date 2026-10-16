#pragma once

#include <QSize>
#include <QString>
#include <QVariantMap>
#include <memory>

#if defined(Q_OS_WIN)
struct ID3D11Device;
struct ID3D11DeviceContext;
#endif

class Live2DBridge final {
public:
    // Native handles the Live2D backend is created from. On Linux these are
    // the VkPhysicalDevice / VkDevice / VkQueue owned by Qt's QRhi.
    struct VulkanHandles {
        void* physicalDevice = nullptr;
        void* device = nullptr;
        void* queue = nullptr;
        unsigned int queueFamily = 0;
    };

    Live2DBridge();
    ~Live2DBridge();
    Live2DBridge(const Live2DBridge&) = delete;
    Live2DBridge& operator=(const Live2DBridge&) = delete;

    void command(const QString& name, const QVariant& value = {});
    QVariantMap state() const;

#if defined(Q_OS_WIN)
    bool initialize(ID3D11Device* device, ID3D11DeviceContext* context, const QString& shaderPath);
#else
    bool initialize(const VulkanHandles& handles);
#endif

    bool processPendingCommands();
    bool render(float deltaSeconds, int width, int height, float centerOffsetX = 0, float centerOffsetY = 0);
    // Native render target: ID3D11Texture2D* on Windows, VkImage on Linux.
    // Zero when nothing has been rendered yet.
    quint64 nativeTexture() const noexcept;
    QSize textureSize() const noexcept;
    void shutdown();

    bool beginExportSession(int motionIndex, float fps);
    bool exportSessionSwitchMotion(int motionIndex);
    void endExportSession();

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
