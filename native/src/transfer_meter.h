#pragma once
#include <QString>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <deque>
namespace Transfer {
struct Sample {
  double t;
  qint64 bytes;
};
class Meter {
public:
  explicit Meter(qint64 total = 0, qint64 initial = 0, double windowSec = 4.0)
      : totalBytes(std::max<qint64>(0, total)),
        lastDone(std::max<qint64>(0, initial)),
        windowSeconds(windowSec > 0.1 ? windowSec : 4.0) {}

  inline void reset(qint64 total, qint64 initial = 0) {
    totalBytes = std::max<qint64>(0, total);
    lastDone = std::max<qint64>(0, initial);
    samples.clear();
  }

  inline void update(qint64 done, double nowSec, double &speedBps,
                     double &etaSec) {
    done = std::max<qint64>(0, done);
    if (done < lastDone)
      samples.clear();
    lastDone = done;

    if (std::isfinite(nowSec)) {
      if (samples.empty() || nowSec > samples.back().t)
        samples.push_back({nowSec, done});
      else
        samples.back().bytes = done;
      while (samples.size() > 2 &&
             (nowSec - samples[1].t) >= windowSeconds)
        samples.pop_front();
      while (samples.size() > 1 &&
             (nowSec - samples.front().t) > windowSeconds * 1.5)
        samples.pop_front();
    }

    speedBps = 0.0;
    etaSec = -1.0;
    if (samples.size() >= 2) {
      const double dt = samples.back().t - samples.front().t;
      const qint64 db = samples.back().bytes - samples.front().bytes;
      if (dt > 0.01 && db > 0) {
        speedBps = double(db) / dt;
        if (totalBytes > done && speedBps > 0.0)
          etaSec = double(totalBytes - done) / speedBps;
        else if (totalBytes > 0 && done >= totalBytes)
          etaSec = 0.0;
      }
    }
  }

  static inline QString formatSpeed(double bps) {
    if (!std::isfinite(bps) || bps <= 0.0)
      return QStringLiteral("0 B/s");
    if (bps < 1024.0)
      return QStringLiteral("%1 B/s").arg(qRound64(bps));
    if (bps < 1024.0 * 1024.0)
      return QStringLiteral("%1 KB/s").arg(bps / 1024.0, 0, 'f', 1);
    if (bps < 1024.0 * 1024.0 * 1024.0)
      return QStringLiteral("%1 MB/s")
          .arg(bps / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB/s")
        .arg(bps / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
  }

  static inline QString formatEta(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0)
      return {};
    const qint64 s =
        seconds > 0.0 && seconds < 1.0 ? 1 : qRound64(seconds);
    if (s < 60)
      return QStringLiteral("%1s").arg(s);
    if (s < 3600) {
      const qint64 m = s / 60;
      const qint64 rem = s % 60;
      return rem ? QStringLiteral("%1m %2s").arg(m).arg(rem)
                 : QStringLiteral("%1m").arg(m);
    }
    const qint64 h = s / 3600;
    const qint64 m = (s % 3600) / 60;
    return m ? QStringLiteral("%1h %2m").arg(h).arg(m)
             : QStringLiteral("%1h").arg(h);
  }

private:
  qint64 totalBytes = 0;
  qint64 lastDone = 0;
  double windowSeconds = 4.0;
  std::deque<Sample> samples;
};
} // namespace Transfer
