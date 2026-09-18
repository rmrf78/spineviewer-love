#pragma once

#include <QSize>
#include <QString>
#include <QVariantMap>
#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

class Live2DBridge final {
public:
    Live2DBridge();
    ~Live2DBridge();
    Live2DBridge(const Live2DBridge&) = delete;
    Live2DBridge& operator=(const Live2DBridge&) = delete;

    void command(const QString& name, const QVariant& value = {});
    QVariantMap state() const;

    bool initialize(ID3D11Device* device, ID3D11DeviceContext* context, const QString& shaderPath);

    bool processPendingCommands();
    bool render(float deltaSeconds, int width, int height, float centerOffsetX = 0, float centerOffsetY = 0);
    ID3D11Texture2D* nativeTexture() const noexcept;
    QSize textureSize() const noexcept;
    void shutdown();

    bool beginExportSession(int motionIndex, float fps);
    bool exportSessionSwitchMotion(int motionIndex);
    void endExportSession();

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
