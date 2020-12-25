// See https://stackoverflow.com/a/43936590/2515783
#include "AspectRatioLabel.h"

AspectRatioLabel::AspectRatioLabel(QWidget *parent, Qt::WindowFlags f) : QLabel(parent, f) {}

AspectRatioLabel::~AspectRatioLabel() {}

void AspectRatioLabel::setPixmap(const QPixmap &pm) {
    pixmapWidth = pm.width();
    pixmapHeight = pm.height();

    updateMargins();
    QLabel::setPixmap(pm);
}

void AspectRatioLabel::resizeEvent(QResizeEvent *event) {
    updateMargins();
    QLabel::resizeEvent(event);
}

void AspectRatioLabel::updateMargins() {
    if (pixmapWidth <= 0 || pixmapHeight <= 0)
        return;

    int w = this->width();
    int h = this->height();

    if (w <= 0 || h <= 0)
        return;

    if (w * pixmapHeight > h * pixmapWidth) {
        int m = (w - (pixmapWidth * h / pixmapHeight)) / 2;
        setContentsMargins(m, 0, m, 0);
    } else {
        int m = (h - (pixmapHeight * w / pixmapWidth)) / 2;
        setContentsMargins(0, m, 0, m);
    }
}
