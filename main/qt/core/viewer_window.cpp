#include "viewer_controller.h"
#include "window_geometry.h"
#include "../../window_resolution_presets.h"
#include <QQuickWindow>
#include <QScreen>
#include <QGuiApplication>
#include <QCursor>
#include <QDateTime>
#include <QRandomGenerator>
#include <QQuickItem>
#include <cmath>
#include <algorithm>
#if defined(Q_OS_WIN)
#define NOMINMAX
#include <Windows.h>
#endif

namespace slqt {
namespace {
QSize legacyFrameAllowance(QQuickWindow* window,bool nativeFrame,bool resizeEnabled){
#if defined(Q_OS_WIN)
    if(QGuiApplication::platformName()=="windows"){
        DWORD style=WS_POPUP|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX;
        if(nativeFrame)style|=WS_CAPTION;
        if(resizeEnabled)style|=WS_THICKFRAME;
        RECT r{};const auto dpi=GetDpiForWindow(reinterpret_cast<HWND>(window->winId()));
        AdjustWindowRectExForDpi(&r,style,FALSE,WS_EX_ACCEPTFILES,dpi?dpi:96);
        return {r.right-r.left,r.bottom-r.top};
    }
#endif
    return {0,0};
}
void resizeLikeLegacy(QQuickWindow* window,QSize physical,bool nativeFrame,bool resizeEnabled){
    const qreal dpr=window->devicePixelRatio();
    const QRect work=window->screen()->availableGeometry();
    const QSize frame=legacyFrameAllowance(window,nativeFrame,resizeEnabled);
    const QSize client=legacyClientSize(physical,frame,
        QSize(qRound(work.width()*dpr),qRound(work.height()*dpr)),!nativeFrame);
    const QSize size(qRound(client.width()/dpr),qRound(client.height()/dpr));

    const auto margins=window->frameMargins();
    const QSize outer=size+QSize(margins.left()+margins.right(),margins.top()+margins.bottom());
    window->setGeometry(QRect(work.topLeft()+QPoint((work.width()-outer.width())/2+margins.left(),
        (work.height()-outer.height())/2+margins.top()),size));
}
#if defined(Q_OS_WIN)
bool writeNativeStyle(HWND hwnd,int index,LONG_PTR value){
    SetLastError(ERROR_SUCCESS);
    const auto previous=SetWindowLongPtrW(hwnd,index,value);
    return previous!=0||GetLastError()==ERROR_SUCCESS;
}
bool setNativeResizeFrame(QQuickWindow* window,bool enabled){
    const auto hwnd=reinterpret_cast<HWND>(window->winId());
    const LONG_PTR style=GetWindowLongPtrW(hwnd,GWL_STYLE);
    if(!writeNativeStyle(hwnd,GWL_STYLE,enabled?(style|WS_THICKFRAME):(style&~WS_THICKFRAME)))return false;
    if(SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED))return true;
    writeNativeStyle(hwnd,GWL_STYLE,style);
    SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    return false;
}
bool setNativeColorKey(QQuickWindow* window,bool enabled,bool topmost){
    const auto hwnd=reinterpret_cast<HWND>(window->winId());
    const LONG_PTR style=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
    COLORREF oldKey=0;BYTE oldAlpha=255;DWORD oldFlags=0;
    const bool hadAttributes=GetLayeredWindowAttributes(hwnd,&oldKey,&oldAlpha,&oldFlags)!=FALSE;
    const bool changed=writeNativeStyle(hwnd,GWL_EXSTYLE,enabled?(style|WS_EX_LAYERED):(style&~WS_EX_LAYERED));
    const bool attributed=changed&&(!enabled||SetLayeredWindowAttributes(hwnd,RGB(0,0,0),255,LWA_COLORKEY));
    if(attributed&&SetWindowPos(hwnd,topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED))return true;
    writeNativeStyle(hwnd,GWL_EXSTYLE,style);
    if(hadAttributes)SetLayeredWindowAttributes(hwnd,oldKey,oldAlpha,oldFlags);
    SetWindowPos(hwnd,(style&WS_EX_TOPMOST)?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    return false;
}
void captureNativeWindowPolicy(QQuickWindow* window){
    const auto hwnd=reinterpret_cast<HWND>(window->winId());
    COLORREF key=0;BYTE alpha=0;DWORD flags=0;
    window->setProperty("_slNativeResizeEffective",(GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_THICKFRAME)!=0);
    window->setProperty("_slNativeTopmostEffective",(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0);
    window->setProperty("_slNativeColorKeyEffective",GetLayeredWindowAttributes(hwnd,&key,&alpha,&flags)&&(flags&LWA_COLORKEY));
}
#endif
}
bool ViewerController::windowCommand(const QString& c,const QVariant& v){
    if(!m_window)return false;
    if(c=="pet.enter"){enterDesktopPet();return true;}
    if(c=="pet.exit"){exitDesktopPet();return true;}
    if(c=="pet.random"){m_petRandom=v.toBool();m_nextPetMotion=0;}
    else if(c=="pet.next"){
        if(live2dMode())m_live2d->command("animation.step",1);
        else runtime()->StepToNextMotion();
    }
    else if(c=="window.resize"){if(m_resizeEnabled&&!m_petMode)m_window->startSystemResize(Qt::Edges(v.toInt()));}
    else if(c=="window.toggleResize"){
#if defined(Q_OS_WIN)
        if(!m_petMode){
            const auto hwnd=reinterpret_cast<HWND>(m_window->winId());
            if(m_resizeEnabled)m_window->setProperty("_slNativeResizeBeforeDisable",(GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_THICKFRAME)!=0);
            const bool nativeEnabled=!m_resizeEnabled&&m_window->property("_slNativeResizeBeforeDisable").toBool();
            if(!setNativeResizeFrame(m_window,nativeEnabled)){fail(tr("Could not change the native window resize policy."));return true;}
        }
#endif
        m_resizeEnabled=!m_resizeEnabled;
    }
    else if(c=="window.invertWheel"){m_invertWheel=!m_invertWheel;m_wheelRemainder=0;}
    else if(c=="window.toggleChrome"){
        m_nativeFrame=!m_nativeFrame;m_window->setFlag(Qt::FramelessWindowHint,!m_nativeFrame);m_window->show();
#if defined(Q_OS_WIN)
        if(!m_resizeEnabled&&!m_petMode){
            m_window->setProperty("_slNativeResizeBeforeDisable",(GetWindowLongPtrW(reinterpret_cast<HWND>(m_window->winId()),GWL_STYLE)&WS_THICKFRAME)!=0);
            if(!setNativeResizeFrame(m_window,false))fail(tr("Could not retain the disabled window resize policy."));
        }

        if(m_clickThrough&&!m_petMode&&!setNativeColorKey(m_window,true,true))fail(tr("Could not retain native black color-key transparency."));
#endif
    }
    else if(c=="window.toggleClickThrough"){
#if defined(Q_OS_WIN)

        const bool enabled=!m_clickThrough;
        if(!setNativeColorKey(m_window,enabled,enabled)){fail(tr("Could not change native black color-key transparency."));return true;}
        m_clickThrough=enabled;
#endif
    }
    else if(c=="window.matchCanvas"){
        const float scale=runtime()->SkeletonScale();if(std::isfinite(scale)&&scale>.0001f){runtime()->SetBaseSize(m_viewport.width()/scale,m_viewport.height()/scale);fit();}
    }
    else if(c=="window.restoreCanvas"){runtime()->ClearBaseSize();fit();}
    else if(c=="settings.resolution"){
        const auto* preset=window_resolution_presets::Get(v.toInt());if(!preset)return true;
        if(m_window->visibility()==QWindow::FullScreen)windowCommand("window.fullscreen",{});
        if(m_defaultWindowSize.isEmpty())m_defaultWindowSize=m_window->size();
        m_state["resolutionPreset"]=v.toInt();m_settings.setValue("resolutionPreset",v.toInt());

        const qreal windowDpr=m_window->devicePixelRatio();
        QSize size=preset->width>0?QSize(qRound(preset->width/windowDpr),qRound(preset->height/windowDpr)):m_defaultWindowSize;
        const QRect available=m_window->screen()->availableGeometry();
        const double factor=std::min(1.0,std::min(double(available.width())/size.width(),double(available.height())/size.height()));
        size=QSize(std::max(1,int(std::floor(size.width()*factor))),std::max(1,int(std::floor(size.height()*factor))));
        if(preset->width>0)resizeLikeLegacy(m_window,QSize(preset->width,preset->height),m_nativeFrame,m_resizeEnabled);
        else{
            m_window->resize(size);
            m_window->setPosition(available.topLeft()+QPoint((available.width()-size.width())/2,(available.height()-size.height())/2));
        }

        const auto* primary=QGuiApplication::primaryScreen();
        const float defaultScale=primary?float(primary->geometry().width()*primary->devicePixelRatio()/1920.0):1.0f;
        const float uiScale=window_resolution_presets::UiScale(v.toInt(),defaultScale);
        m_state["baseFontPixels"]=16.0f*uiScale;m_state["titleScale"]=uiScale;m_state["uiScale"]=uiScale;
        m_settings.setValue("uiScale",uiScale);m_settings.setValue("baseFontPixels",16.0f*uiScale);m_settings.setValue("titleScale",uiScale);
    }
    else if(c=="window.fullscreen"){
        if(m_window->visibility()==QWindow::FullScreen){
            if(m_fullscreenReturnVisibility==int(QWindow::Maximized))m_window->showMaximized();
            else {m_window->showNormal();if(m_fullscreenReturnGeometry.isValid())m_window->setGeometry(m_fullscreenReturnGeometry);}
        }else{m_fullscreenReturnVisibility=int(m_window->visibility());m_fullscreenReturnGeometry=m_window->geometry();m_window->showFullScreen();}
#if defined(Q_OS_WIN)
        if(!m_resizeEnabled&&m_window->visibility()!=QWindow::FullScreen&&!m_petMode)setNativeResizeFrame(m_window,false);
        if(m_clickThrough&&!m_petMode&&!setNativeColorKey(m_window,true,true))fail(tr("Could not retain native black color-key transparency."));
#endif
    }
    else return false;
#if defined(Q_OS_WIN)
    captureNativeWindowPolicy(m_window);
#endif
    refresh();record();return true;
}
void ViewerController::enterDesktopPet(){
    if(m_petMode||!m_window||!m_state.value("loaded").toBool())return;
    m_petReturnGeometry=m_window->geometry();m_petReturnFlags=m_window->flags();m_petReturnMinimum=m_window->minimumSize();

    const QPoint clientOrigin=m_window->mapToGlobal(QPoint(0,0));
    auto* desktop=m_window->findChild<QObject*>("desktopViewer");
    const qreal panelWidth=desktop?desktop->property("canvasLeft").toReal():m_window->width()-m_viewport.width()/m_dpr;
    const int panel=std::clamp(qRound(panelWidth),0,qMax(0,m_window->width()-1));
    const QRect pet(clientOrigin+QPoint(panel,0),QSize(qMax(1,m_window->width()-panel),m_window->height()));
    m_window->setProperty("_slPetReturnVisibility",int(m_window->visibility()));
    m_window->setProperty("_slPetReturnColor",m_window->color());
    m_window->setProperty("_slPetPersistentGraphics",m_window->isPersistentGraphics());
    m_window->setProperty("_slPetPersistentSceneGraph",m_window->isPersistentSceneGraph());
#if defined(Q_OS_WIN)
    m_window->setProperty("_slPetReturnTopmost",(GetWindowLongPtrW(reinterpret_cast<HWND>(m_window->winId()),GWL_EXSTYLE)&WS_EX_TOPMOST)!=0);
#endif
    m_window->setPersistentGraphics(true);m_window->setPersistentSceneGraph(true);
    m_live2d->command("queue.stop");m_live2d->command("pet.reset");m_petClock.restart();m_petMode=true;m_nextPetMotion=0;m_dragged=false;m_pointerMode=0;
    m_state["petMode"]=true;emit stateChanged();
    m_window->setMinimumSize(QSize(1,1));m_window->setColor(Qt::transparent);
    m_window->setWindowState(Qt::WindowNoState);

    m_window->setProperty("_slPetReturnNormalGeometry",m_window->geometry());
    m_window->setFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);
    m_window->setGeometry(pet);m_window->show();m_clock.restart();refresh();record();
#if defined(Q_OS_WIN)
    captureNativeWindowPolicy(m_window);
#endif
}
void ViewerController::exitDesktopPet(){
    if(!m_petMode||!m_window)return;
    m_petMode=false;m_pointerMode=0;m_dragged=true;m_live2d->command("live2d.endDrag");m_live2d->command("pet.reset");
    if(auto* grabber=m_window->mouseGrabberItem())grabber->ungrabMouse();
    m_state["petMode"]=false;emit stateChanged();
    m_window->setWindowState(Qt::WindowNoState);
    m_window->setFlags(m_petReturnFlags);m_window->setColor(m_window->property("_slPetReturnColor").value<QColor>());m_window->setMinimumSize(m_petReturnMinimum);
    const auto returnVisibility=QWindow::Visibility(m_window->property("_slPetReturnVisibility").toInt());
    const QRect normalGeometry=m_window->property("_slPetReturnNormalGeometry").toRect();
    m_window->setGeometry((returnVisibility==QWindow::FullScreen||returnVisibility==QWindow::Maximized)&&normalGeometry.isValid()
        ?normalGeometry:m_petReturnGeometry);
    if(returnVisibility==QWindow::FullScreen)m_window->showFullScreen();
    else if(returnVisibility==QWindow::Maximized)m_window->showMaximized();
    else m_window->showNormal();
#if defined(Q_OS_WIN)
    const auto hwnd=reinterpret_cast<HWND>(m_window->winId());

    if(m_clickThrough){
        if(!setNativeColorKey(m_window,true,m_window->property("_slPetReturnTopmost").toBool()))fail(tr("Could not restore native black color-key transparency."));
    }else SetWindowPos(hwnd,m_window->property("_slPetReturnTopmost").toBool()?HWND_TOPMOST:HWND_NOTOPMOST,
        0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    if(!m_resizeEnabled&&returnVisibility!=QWindow::FullScreen)setNativeResizeFrame(m_window,false);
    captureNativeWindowPolicy(m_window);
#endif
    m_window->setPersistentGraphics(m_window->property("_slPetPersistentGraphics").toBool());
    m_window->setPersistentSceneGraph(m_window->property("_slPetPersistentSceneGraph").toBool());
    m_window->requestActivate();m_clock.restart();refresh();record();
}
void ViewerController::updateDesktopPet(){
    if(!m_petMode||!m_window)return;
    if(live2dMode()&&m_viewport.width()>0&&m_viewport.height()>0){
        const QPoint global=QCursor::pos();
        const QPoint local=m_window->mapFromGlobal(global);
        const QPointF p=QPointF(local)*m_dpr;
        const double x=p.x()/m_viewport.width()*2-1,y=1-p.y()/m_viewport.height()*2;
        m_live2d->command("live2d.drag",QVariantMap{{"x",std::clamp(x,-1.0,1.0)},{"y",std::clamp(y,-1.0,1.0)}});
        QPointF physicalScreen=QPointF(global)*m_dpr;
#if defined(Q_OS_WIN)
        POINT nativeCursor{};if(GetCursorPos(&nativeCursor))physicalScreen=QPointF(nativeCursor.x,nativeCursor.y);
#endif
        m_live2d->command("pet.pointer",QVariantMap{{"x",x},{"y",y},
            {"screenX",physicalScreen.x()},{"screenY",physicalScreen.y()},{"nowMs",m_petClock.elapsed()},{"dragging",m_pointerMode!=0}});
    }
    const qint64 now=m_petClock.elapsed();
    if(m_petRandom){
        if(m_nextPetMotion==0)m_nextPetMotion=now+QRandomGenerator::global()->bounded(10000,25000);
        else if(now>=m_nextPetMotion){
            const int count=int(m_state.value("animations").toList().size());
            if(count>1){const int selected=QRandomGenerator::global()->bounded(count);if(live2dMode())m_live2d->command("animation.play",selected);else runtime()->PlayMotionByIndex(size_t(selected));}
            m_nextPetMotion=now+QRandomGenerator::global()->bounded(10000,25000);
        }
    }
}
}
