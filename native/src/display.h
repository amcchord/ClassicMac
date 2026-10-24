#pragma once
#include <QRectF>
#include <QSize>
#include <algorithm>
#include <cmath>

namespace NativeDisplay {
// Windows chrome follows the exact OS DPI. Guest pixels use the closest
// integral device-pixel zoom; the driver changes resolution to fill the view.
inline int preferredZoom(qreal dpr) { return std::max(1, qRound(dpr)); }
inline QSize requestedSize(QSize viewport, qreal dpr, int alignment, int zoomOverride = 0) {
  int zoom = zoomOverride > 0 ? zoomOverride : preferredZoom(dpr);
  while (zoom > 1 && (qFloor(viewport.width() * dpr) < 512 * zoom ||
                       qFloor(viewport.height() * dpr) < 384 * zoom))
    --zoom;
  int w = std::clamp(qFloor(viewport.width() * dpr) / zoom, 512, 3840);
  int h = std::clamp(qFloor(viewport.height() * dpr) / zoom, 384, 2160);
  return {w / alignment * alignment, h};
}
inline QRectF imageRect(QSize viewport, QSize source, qreal dpr,
                       bool fitWindow = false, int zoomOverride = 0) {
  if (source.isEmpty() || viewport.isEmpty() || dpr <= 0)
    return {};
  // A fractional child origin can leave only floor(size * DPR) visible
  // device pixels. Rounding up clips the last guest row/column at 125/175%.
  QSize pixels(qFloor(viewport.width() * dpr),
               qFloor(viewport.height() * dpr));
  const qreal fit = std::min(qreal(pixels.width()) / source.width(),
                            qreal(pixels.height()) / source.height());
  const qreal zoom = fitWindow || fit < 1 ? fit :
                     zoomOverride > 0 ? std::min(fit, qreal(zoomOverride)) : std::floor(fit);
  QSize target(qRound(source.width() * zoom), qRound(source.height() * zoom));
  return {std::floor((pixels.width() - target.width()) / 2.0) / dpr,
          std::floor((pixels.height() - target.height()) / 2.0) / dpr,
          target.width() / dpr, target.height() / dpr};
}
} // namespace NativeDisplay
