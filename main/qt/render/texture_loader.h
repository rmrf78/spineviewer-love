#pragma once
#include <QImage>
#include <QString>

namespace slqt {

QImage loadTextureImage(const QString& path,bool premultiplyAlpha,QString* error=nullptr);
}
