#include "frames.hpp"

#include <QPainter>

#include "coach.hpp"

CameraView::CameraView(QQuickItem* parent) : QQuickPaintedItem(parent) {
  connect(Coach::instance(), &Coach::frameReady, this, [this] { update(); });
}

void CameraView::paint(QPainter* p) {
  QImage img = Coach::instance()->frame();
  QRectF r;
  if (!img.isNull()) {
    QSizeF s = img.size().scaled(size().toSize(), Qt::KeepAspectRatio);
    r = QRectF(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->drawImage(r, img);
  }
  if (r != rect_) {
    rect_ = r;
    emit paintedRectChanged();
  }
}
