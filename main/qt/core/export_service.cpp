#include "export_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageWriter>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <utility>

namespace slqt {
namespace {
bool failWith(QString* target, const QString& error)
{
    if (target)
        *target = error;
    return false;
}
int straight(int channel, int alpha)
{
    return alpha == 0 ? 0 : std::min(255, (channel * 255 + alpha / 2) / alpha);
}
}

ExportService::ExportService(QObject* parent) : QObject(parent)
{
    m_frameTimer.setSingleShot(true);
    connect(&m_frameTimer, &QTimer::timeout, this, &ExportService::renderNext);
    connect(&m_encoder, &QProcess::readyReadStandardError, this, [this] {
        m_encoderError += QString::fromUtf8(m_encoder.readAllStandardError());

        if (m_encoderError.size() > 8192)
            m_encoderError = m_encoderError.right(8192);
    });
    connect(&m_encoder, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_running)
            return;
        if (error == QProcess::FailedToStart) {
            m_encoderError = m_encoder.errorString();
            ++m_encodeAttempt;
            const auto generation = m_generation;
            QTimer::singleShot(0, this, [this, generation] {
                if (m_generation == generation)
                    encodeNext();
            });
        }
    });
    connect(&m_encoder, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (!m_running)
            return;
        if (m_cancelled) {
            finish(false, QStringLiteral("Export cancelled."));
            return;
        }
        if (exitStatus == QProcess::NormalExit && exitCode == 0
            && QFileInfo(m_stagedMoviePath).size() > 0) {
            QString error;
            const bool committed = commitMovie(&error);
            finish(committed, error);
            return;
        }
        ++m_encodeAttempt;
        const auto generation = m_generation;
        QTimer::singleShot(0, this, [this, generation] {
            if (m_generation == generation)
                encodeNext();
        });
    });
}

ExportService::~ExportService()
{
    m_running = false;
    m_frameTimer.stop();
    if (m_encoder.state() != QProcess::NotRunning) {
        m_encoder.kill();
        m_encoder.waitForFinished(1000);
    }
    removeStagedMovie();

}

int ExportService::clampFps(int fps) noexcept { return std::clamp(fps, 1, 120); }

int ExportService::frameCount(double durationSeconds, int fps) noexcept
{
    if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0)
        return 1;

    const double count = std::ceil(static_cast<float>(durationSeconds) * static_cast<float>(clampFps(fps)));

    if (!std::isfinite(count) || count > static_cast<double>(std::numeric_limits<int>::max()))
        return 0;
    return std::max(1, static_cast<int>(count));
}

QImage ExportService::outputPixels(const QImage& image, bool keepAlpha, const QColor& matteColor)
{
    if (image.isNull())
        return {};
    const QImage pma = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage output(pma.size(), keepAlpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    if (output.isNull())
        return {};
    for (int y = 0; y < pma.height(); ++y) {
        const auto* source = reinterpret_cast<const QRgb*>(pma.constScanLine(y));
        auto* target = reinterpret_cast<QRgb*>(output.scanLine(y));
        for (int x = 0; x < pma.width(); ++x) {
            const int alpha = qAlpha(source[x]);
            if (keepAlpha) {
                target[x] = qRgba(straight(qRed(source[x]), alpha), straight(qGreen(source[x]), alpha),
                                  straight(qBlue(source[x]), alpha), alpha);
            } else {
                const int inverse = 255 - alpha;
                target[x] = qRgb(std::min(255, qRed(source[x]) + (matteColor.red() * inverse + 127) / 255),
                                 std::min(255, qGreen(source[x]) + (matteColor.green() * inverse + 127) / 255),
                                 std::min(255, qBlue(source[x]) + (matteColor.blue() * inverse + 127) / 255));
            }
        }
    }
    return output;
}

bool ExportService::saveImage(const QString& path, ImageFormat format, const QImage& image,
                             bool keepAlpha, const QColor& matteColor, QString* error)
{
    if (error)
        error->clear();
    if (path.isEmpty())
        return failWith(error, QStringLiteral("Export path is empty."));
    if (image.isNull())
        return failWith(error, QStringLiteral("Nothing is available to export."));
    const QImage pixels = outputPixels(image, format == ImageFormat::Png && keepAlpha, matteColor);
    if (pixels.isNull())
        return failWith(error, QStringLiteral("Could not allocate export pixels."));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return failWith(error, file.errorString());
    QImageWriter writer(&file, format == ImageFormat::Png ? QByteArray("png") : QByteArray("jpg"));
    if (!writer.write(pixels))
        return failWith(error, writer.errorString());
    if (!file.commit())
        return failWith(error, file.errorString());
    return true;
}

QString ExportService::findFfmpeg()
{
    const QString name = QStringLiteral("ffmpeg")
#ifdef Q_OS_WIN
        + QStringLiteral(".exe")
#endif
        ;
    const QDir application(QCoreApplication::applicationDirPath());
    const QStringList candidates{application.filePath(name), application.filePath("tools/" + name), application.filePath("../tools/" + name)};
    for (const auto& path : candidates) {
        const QFileInfo info(path);
        if (info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(name);
}

QList<QStringList> ExportService::movieArguments(const QString& frameFolder, const QString& outputPath,
                                                MovieFormat format, bool keepAlpha, int fps)
{
    const QStringList base{"-hide_banner", "-loglevel", "error", "-y", "-framerate",
                           QString::number(clampFps(fps)), "-start_number", "1", "-i",
                           QDir(frameFolder).filePath("frame_%06d.png")};
    if (format == MovieFormat::Mp4) {
        const QStringList prefix = base + QStringList{"-vf", "crop=trunc(iw/2)*2:trunc(ih/2)*2"};
        const QStringList tail{"-pix_fmt", "yuv420p", "-movflags", "+faststart", outputPath};
        return {prefix + QStringList{"-c:v", "libx264", "-crf", "17"} + tail,
                prefix + QStringList{"-c:v", "h264_mf", "-b:v", "12M"} + tail,
                prefix + QStringList{"-c:v", "h264_nvenc", "-cq", "18"} + tail};
    }
    if (format == MovieFormat::Gif)
        return {base + QStringList{"-vf", "split[s0][s1];[s0]palettegen=max_colors=256[p];[s1][p]paletteuse=dither=sierra2_4a",
                                   "-loop", "0", outputPath}};
    if (keepAlpha)
        return {base + QStringList{"-c:v", "libvpx-vp9", "-crf", "17", "-b:v", "0", "-pix_fmt", "yuva420p",
                                   "-auto-alt-ref", "0", outputPath}};
    return {base + QStringList{"-c:v", "libvpx-vp9", "-crf", "24", "-b:v", "0", "-pix_fmt", "yuv420p", outputPath}};
}

bool ExportService::startFrames(const ExportRequest& request, RenderFrame render, QString* error)
{ return start(request, std::move(render), false, error); }

bool ExportService::startMovie(const ExportRequest& request, RenderFrame render, QString* error)
{ return start(request, std::move(render), true, error); }

bool ExportService::start(const ExportRequest& request, RenderFrame render, bool movie, QString* error)
{
    if (error)
        error->clear();
    if (isBusy())
        return failWith(error, QStringLiteral("Another export is already running."));
    if (!render || request.outputPath.isEmpty())
        return failWith(error, QStringLiteral("Export output and renderer are required."));
    if (request.motions.isEmpty())
        return failWith(error, QStringLiteral("No animation is available to export."));
    QList<int> counts;
    int total = 0;
    for (const auto& motion : request.motions) {
        const int count = frameCount(motion.durationSeconds, request.fps);
        if (count == 0 || total > std::numeric_limits<int>::max() - count)
            return failWith(error, QStringLiteral("The export contains too many frames."));
        counts.append(count);
        total += count;
    }
    QString folder, stagedMovie;
    if (movie) {
        const QFileInfo output(request.outputPath);
        if (!QFileInfo(output.absolutePath()).isDir())
            return failWith(error, QStringLiteral("The export directory does not exist."));
        if (output.exists() && !output.isFile())
            return failWith(error, QStringLiteral("The movie export path is not a file."));
        const QString suffix = request.movieFormat == MovieFormat::Mp4 ? ".mp4"
            : request.movieFormat == MovieFormat::Gif ? ".gif" : ".webm";

        QTemporaryFile encoded(QDir(output.absolutePath()).filePath(".spinelove-encode-XXXXXX" + suffix));
        if (!encoded.open())
            return failWith(error, encoded.errorString());
        QTemporaryDir temporary(QDir::tempPath() + "/spinelove_frames_XXXXXX");
        if (!temporary.isValid())
            return failWith(error, QStringLiteral("Could not create temporary export directory."));
        temporary.setAutoRemove(false);
        encoded.setAutoRemove(false);
        stagedMovie = encoded.fileName();
        folder = temporary.path();
    } else {
        if (!QDir().mkpath(request.outputPath))
            return failWith(error, QStringLiteral("Could not create export directory."));
        folder = QFileInfo(request.outputPath).absoluteFilePath();
    }
    m_request = request;
    ++m_generation;
    m_request.outputPath = QFileInfo(request.outputPath).absoluteFilePath();
    m_request.fps = clampFps(request.fps);
    if (movie) {
        m_request.imageFormat = ImageFormat::Png;
        m_request.keepAlpha = request.movieFormat == MovieFormat::Webm && request.keepAlpha;
    }
    m_render = std::move(render);
    m_frameCounts = counts;
    m_frameFolder = folder;
    m_stagedMoviePath = stagedMovie;
    m_keepStagedMovie = false;
    m_ownedTemporaryDirectory = movie;
    m_movie = movie;
    m_running = true;
    m_cancelled = false;
    m_totalFrames = total;
    m_completedFrames = m_motionIndex = m_frameInMotion = m_encodeAttempt = 0;
    m_frameSize = {};
    m_encoderError.clear();
    emit progressChanged(0, total, QStringLiteral("Rendering frames..."));
    m_frameTimer.start(0);
    return true;
}

void ExportService::renderNext()
{
    if (!m_running)
        return;
    if (m_cancelled) {
        finish(false, QStringLiteral("Export cancelled."));
        return;
    }
    QString error;
    const auto generation = m_generation;
    const RenderFrame renderer = m_render;
    QImage frame;
    {
        QScopedValueRollback<bool> rendering(m_rendering, true);
        frame = renderer(m_motionIndex, m_frameInMotion,
                         m_frameInMotion == 0 ? 0.0f : 1.0f / m_request.fps, &error);
    }

    if (!m_running || m_generation != generation)
        return;
    if (frame.isNull()) {
        finish(false, error.isEmpty() ? QStringLiteral("Could not render animation frame.") : error);
        return;
    }
    if (m_frameSize.isEmpty())
        m_frameSize = frame.size();
    if (frame.size() != m_frameSize) {
        finish(false, QStringLiteral("Export frame dimensions changed during rendering."));
        return;
    }
    const QString suffix = m_request.imageFormat == ImageFormat::Png ? ".png" : ".jpg";
    const QString path = QDir(m_frameFolder).filePath(
        QStringLiteral("frame_%1").arg(m_completedFrames + 1, 6, 10, QLatin1Char('0')) + suffix);
    if (!saveImage(path, m_request.imageFormat, frame, m_request.keepAlpha, m_request.matteColor, &error)) {
        finish(false, error);
        return;
    }
    ++m_completedFrames;
    ++m_frameInMotion;
    emit progressChanged(m_completedFrames, m_totalFrames,
                         QStringLiteral("Rendering (%1/%2) %3").arg(m_motionIndex + 1)
                             .arg(m_request.motions.size()).arg(m_request.motions[m_motionIndex].name));
    if (!m_running || m_generation != generation)
        return;
    if (m_completedFrames == m_totalFrames) {
        if (!m_movie) {
            finish(true, {});
            return;
        }
        m_ffmpeg = findFfmpeg();
        m_encodeArguments = movieArguments(m_frameFolder, m_stagedMoviePath,
                                           m_request.movieFormat, m_request.keepAlpha, m_request.fps);
        encodeNext();
        return;
    }
    if (m_frameInMotion >= m_frameCounts[m_motionIndex]) {
        ++m_motionIndex;
        m_frameInMotion = 0;
    }
    m_frameTimer.start(0);
}

void ExportService::encodeNext()
{
    if (!m_running)
        return;
    if (m_cancelled) {
        finish(false, QStringLiteral("Export cancelled."));
        return;
    }
    if (m_ffmpeg.isEmpty()) {
        finish(false, QStringLiteral("ffmpeg was not found. Put it beside the application, in its tools folder, or on PATH."));
        return;
    }
    if (m_encodeAttempt >= m_encodeArguments.size()) {
        finish(false, QStringLiteral("Video encoding failed.\n%1").arg(m_encoderError));
        return;
    }
    const auto generation = m_generation;
    emit progressChanged(m_completedFrames, m_totalFrames, QStringLiteral("Encoding video..."));
    if (!m_running || m_generation != generation)
        return;

    if (QFileInfo::exists(m_stagedMoviePath) && !QFile::remove(m_stagedMoviePath)) {
        finish(false, QStringLiteral("Could not prepare the temporary movie output."));
        return;
    }
    m_encoder.setProgram(m_ffmpeg);
    m_encoder.setArguments(m_encodeArguments[m_encodeAttempt]);

    m_encoder.start();
}

void ExportService::cancel()
{
    m_cancelled = true;
    if (!m_running)
        return;
    m_frameTimer.stop();
    if (m_encoder.state() != QProcess::NotRunning)
        m_encoder.kill();
    else
        finish(false, QStringLiteral("Export cancelled."));
}

void ExportService::finish(bool success, const QString& error)
{
    if (!m_running)
        return;
    m_frameTimer.stop();
    m_running = false;
    const QString recovery = !success && m_movie ? m_frameFolder : QString{};

    if (success && m_ownedTemporaryDirectory)
        QDir(m_frameFolder).removeRecursively();
    m_ownedTemporaryDirectory = false;
    removeStagedMovie();
    m_render = {};
    emit finished(success, error, recovery);
}

bool ExportService::commitMovie(QString* error)
{
    const auto path = [](const QString& value) {
#ifdef Q_OS_WIN
        return std::filesystem::path(value.toStdWString());
#else
        const QByteArray bytes = value.toUtf8();
        return std::filesystem::u8path(bytes.constData(), bytes.constData() + bytes.size());
#endif
    };
    std::error_code failure;
    std::filesystem::rename(path(m_stagedMoviePath), path(m_request.outputPath), failure);
    if (failure) {
        m_keepStagedMovie = true;
        const std::string nativeError = failure.message();
        return failWith(error, QStringLiteral("Could not replace the final movie: %1\nThe encoded movie was preserved in:\n%2")
            .arg(QString::fromLocal8Bit(nativeError.c_str()), m_stagedMoviePath));
    }
    m_stagedMoviePath.clear();
    return true;
}

void ExportService::removeStagedMovie()
{
    if (!m_keepStagedMovie && !m_stagedMoviePath.isEmpty())
        QFile::remove(m_stagedMoviePath);
    m_stagedMoviePath.clear();
}

}
