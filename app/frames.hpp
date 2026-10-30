#pragma once
// The camera picture on the Train page. It keeps the picture's proportions and says where it drew
// it (paintedRect), so that the skeleton drawn over it in QML lines up.
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

class CameraView : public QQuickPaintedItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QRectF paintedRect READ paintedRect NOTIFY paintedRectChanged)

 public:
  explicit CameraView(QQuickItem* parent = nullptr);
  void paint(QPainter* p) override;
  QRectF paintedRect() const { return rect_; }

 signals:
  void paintedRectChanged();

 private:
  QRectF rect_;
};
