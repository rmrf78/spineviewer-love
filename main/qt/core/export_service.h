#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QSize>
#include <QStringList>
#include <QTimer>

#include <functional>

namespace slqt {

enum class ImageFormat { Png, Jpeg };
enum class MovieFormat { Mp4, Webm, Gif };

struct ExportMotion {
    QString name;
    double durationSeconds = 0.0;
};

struct ExportRequest {

    QString outputPath;
    ImageFormat imageFormat = ImageFormat::Png;
    MovieFormat movieFormat = MovieFormat::Mp4;
    int fps = 30;
    bool keepAlpha = true;
    QColor matteColor = Qt::black;
    QList<ExportMotion> motions;
};

class ExportService final : public QObject {
    Q_OBJECT
public:
    using RenderFrame = std::function<QImage(int motionIndex, int frameInMotion,
                                             float advanceSeconds, QString* error)>;

    explicit ExportService(QObject* parent = nullptr);
    ~ExportService() override;

    bool startFrames(const ExportRequest& request, RenderFrame render, QString* error = nullptr);
    bool startMovie(const ExportRequest& request, RenderFrame render, QString* error = nullptr);
    void cancel();
    bool isRunning() const noexcept { return m_running; }
    bool isBusy() const noexcept { return m_running || m_rendering; }
    bool cancellationRequested() const noexcept { return m_cancelled; }
    void resetCancellation() noexcept { if (!isBusy()) m_cancelled = false; }

    static int clampFps(int fps) noexcept;
    static int frameCount(double durationSeconds, int fps) noexcept;
    static QImage outputPixels(const QImage& image, bool keepAlpha, const QColor& matteColor);
    static bool saveImage(const QString& path, ImageFormat format, const QImage& image,
                          bool keepAlpha, const QColor& matteColor, QString* error = nullptr);
    static QString findFfmpeg();
    static QList<QStringList> movieArguments(const QString& frameFolder, const QString& outputPath,
                                             MovieFormat format, bool keepAlpha, int fps);

signals:
    void progressChanged(int completedFrames, int totalFrames, const QString& status);

    void finished(bool success, const QString& error, const QString& recoveryFolder);

private:
    bool start(const ExportRequest& request, RenderFrame render, bool movie, QString* error);
    void renderNext();
    void encodeNext();
    void finish(bool success, const QString& error);
    void removeStagedMovie();
    bool commitMovie(QString* error);

    QTimer m_frameTimer;
    QProcess m_encoder;
    ExportRequest m_request;
    RenderFrame m_render;
    QList<int> m_frameCounts;
    QList<QStringList> m_encodeArguments;
    QString m_frameFolder;
    QString m_ffmpeg;
    QString m_encoderError;
    QString m_stagedMoviePath;
    QSize m_frameSize;
    int m_totalFrames = 0;
    int m_completedFrames = 0;
    int m_motionIndex = 0;
    int m_frameInMotion = 0;
    int m_encodeAttempt = 0;
    bool m_running = false;
    bool m_rendering = false;
    bool m_movie = false;
    bool m_ownedTemporaryDirectory = false;
    bool m_cancelled = false;
    bool m_keepStagedMovie = false;
    quint64 m_generation = 0;
};

}
