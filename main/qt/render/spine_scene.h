#pragma once
#include <QQuickRhiItem>
#include <memory>
#include "scene_snapshot.h"
#include <QPointer>
#include "../core/viewer_controller.h"

namespace slqt {
class SpineScene : public QQuickRhiItem {
    Q_OBJECT
    Q_PROPERTY(slqt::ViewerController* controller READ controller WRITE setController NOTIFY controllerChanged)
public:
    explicit SpineScene(QQuickItem* parent = nullptr);
    ViewerController* controller() const { return m_controller; }
    void setController(ViewerController* controller);
    quint64 sourceGeneration()const{return m_sourceGeneration;}
    std::shared_ptr<const SceneSnapshot> snapshot() const;
signals:
    void controllerChanged();
protected:
    QQuickRhiItemRenderer* createRenderer() override;
    void geometryChange(const QRectF& now, const QRectF& before) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void hoverMoveEvent(QHoverEvent*) override;
    void hoverLeaveEvent(QHoverEvent*) override;
private:
    void updateViewport();
    ViewerController* m_controller = nullptr;
    quint64 m_sourceGeneration=0;
};
}
