#include "core/viewer_controller.h"
#include "render/spine_scene.h"
#include "render/legacy_image_provider.h"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QFontDatabase>
#include <QIcon>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTranslator>
#include <QTimer>
#include <QScreen>
#include <QSurfaceFormat>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QSettings>
#include <QDir>
#include <algorithm>
#include <functional>
#include "../window_resolution_presets.h"
#include <cstdio>

static QString assetPath(const QString& relative){
    const QDir app(QCoreApplication::applicationDirPath());
    const QString packaged=app.filePath("ttf/"+relative);
    if(QFileInfo::exists(packaged))return packaged;
    return app.filePath(relative);
}

class LegacyTranslations final : public QTranslator {
public:
    void setLanguage(const QString& code) {
        m_strings.clear();QFile file(":/lang/"+code+".txt");
        if(!file.open(QIODevice::ReadOnly))return;
        const auto lines=QString::fromUtf8(file.readAll()).split('\n');
        for(auto line:lines){if(line.endsWith('\r'))line.chop(1);const auto equal=line.indexOf('=');if(equal>0)m_strings.insert(line.left(equal),line.mid(equal+1));}
    }
    QString translate(const char*,const char* source,const char*,int) const override {return m_strings.value(QString::fromUtf8(source));}
    bool isEmpty() const override {return m_strings.isEmpty();}
private:QHash<QString,QString> m_strings;
};

int main(int argc,char** argv){
    auto format=QSurfaceFormat::defaultFormat();format.setAlphaBufferSize(8);QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc,argv);
    app.setApplicationName("SpineLoveEX");app.setOrganizationName("SpineLoveEX");
    app.setWindowIcon(QIcon(QStringLiteral(":/main/app.ico")));
    const auto args=app.arguments();
    QSettings preferences(QSettings::defaultFormat(),QSettings::UserScope,"SpineLoveEX","Viewer");
    const float desktopScale=app.primaryScreen()?float(app.primaryScreen()->geometry().width()*app.primaryScreen()->devicePixelRatio()/1920.0):1.f;
    preferences.setValue("uiScale",window_resolution_presets::UiScale(preferences.value("resolutionPreset",0).toInt(),desktopScale));
    if(args.contains("--opengl"))QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    const int fontId=QFontDatabase::addApplicationFont(assetPath("NotoSansSC-Regular.ttf"));
    if(fontId>=0){const auto names=QFontDatabase::applicationFontFamilies(fontId);if(!names.isEmpty())app.setFont(QFont(names.front()));}
    qmlRegisterType<slqt::SpineScene>("SpineLove",1,0,"SpineScene");
    qmlRegisterUncreatableType<slqt::ViewerController>("SpineLove",1,0,"ViewerController","Created by the application");
    slqt::ViewerController controller;
    LegacyTranslations translator;translator.setLanguage(controller.state().value("language").toString());app.installTranslator(&translator);
    QQmlApplicationEngine engine;engine.addImageProvider("legacy-texture",new slqt::LegacyImageProvider);
    engine.rootContext()->setContextProperty("backend",&controller);
    QObject::connect(&controller,&slqt::ViewerController::languageRequested,&engine,[&](const QString& language){translator.setLanguage(language);engine.retranslate();});
    QObject::connect(&engine,&QQmlApplicationEngine::objectCreationFailed,&app,[]{QCoreApplication::exit(2);},Qt::QueuedConnection);
    engine.load(QUrl("qrc:/Main.qml"));if(engine.rootObjects().isEmpty())return 2;
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    if(window&&window->screen()&&!args.contains("--fixed-size")){
        const auto desktop=window->screen()->geometry();const qreal dpr=window->devicePixelRatio();
        window->resize(qMax(qRound(640/dpr),desktop.width()*9/10),qMax(qRound(480/dpr),desktop.height()*9/10));
        window->setPosition(desktop.x()+(desktop.width()-window->width())/2,desktop.y()+(desktop.height()-window->height())/4);
    }
    controller.setWindow(window);
    if(window&&window->screen()&&!args.contains("--fixed-size")){
        const int presetIndex=preferences.value("resolutionPreset",0).toInt();
        const auto* preset=window_resolution_presets::Get(presetIndex);

        if(preset&&preset->width>0)controller.dispatch("settings.resolution",presetIndex);
    }
    const int viewport=args.indexOf("--viewport");
    if(window&&viewport>=0&&viewport+1<args.size()){
        const auto parts=args[viewport+1].split('x');if(parts.size()==2){const int w=parts[0].toInt(),h=parts[1].toInt();if(w>0&&h>0)window->resize(qRound(w/window->devicePixelRatio()),qRound(h/window->devicePixelRatio()));}
    }

    if(window)window->show();
    const int open=args.indexOf("--open");if(open>=0&&open+1<args.size())QTimer::singleShot(0,&controller,[&controller,args,open]{controller.openPaths({args[open+1]});});
    return app.exec();
}
