#include "platinum.h"
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QPoint>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace Platinum {
static const QJsonObject &assets() {
  static const QJsonObject a = [] {
    QFile f(":/platinum.json");
    if (!f.open(QFile::ReadOnly))
      qFatal("Missing Platinum tables");
    return QJsonDocument::fromJson(f.readAll()).object();
  }();
  return a;
}
const QJsonObject &measured() {
  static const auto m = assets()["measured"].toObject();
  return m;
}
static const QJsonArray &table(const QString &key) {
  static const QHash<QString, QJsonArray> cache = [] {
    QHash<QString, QJsonArray> out;
    const auto &m = measured();
    for (auto it = m.begin(); it != m.end(); ++it)
      if (it.value().isArray())
        out.insert(it.key(), it.value().toArray());
    return out;
  }();
  static const QJsonArray empty;
  auto it = cache.constFind(key);
  return it != cache.constEnd() ? it.value() : empty;
}
QImage cachedIcon(const QString &resourcePath) {
  static QMutex mutex;
  static QHash<QString, QImage> cache;
  QMutexLocker lock(&mutex);
  auto it = cache.constFind(resourcePath);
  if (it != cache.constEnd())
    return it.value();
  QImage img(resourcePath);
  cache.insert(resourcePath, img);
  return img;
}
QRgb color(QChar ch) {
  const ushort u = ch.unicode();
  if (u == '.' || u == ' ')
    return 0;
  if (u == 'K')
    return 0xff000000;
  if (u == 'W')
    return 0xffffffff;
  if (u >= '0' && u <= '9')
    return 0xff000000 | uint(u - '0') * 0x111111;
  if (u >= 'a' && u <= 'f')
    return 0xff000000 | uint(u - 'a' + 10) * 0x111111;
  static const unsigned ramp[] = {0xeeeeee, 0xccccff, 0x9999ff, 0x6666cc,
                                  0x333399, 0x000088, 0x000055, 0};
  const int i = int(u) - 'A';
  return i >= 0 && i < 8 ? 0xff000000 | ramp[i] : 0;
}
Pixels::Pixels(int w, int h, QRgb background)
    : image(std::max(1, w), std::max(1, h), QImage::Format_ARGB32) {
  image.fill(background);
}
void Pixels::pixel(int x, int y, QRgb c) {
  if (qAlpha(c) && x >= 0 && y >= 0 && x < w() && y < h())
    image.setPixel(x, y, c);
}
void Pixels::fill(int x, int y, int width, int height, QRgb c) {
  if (!qAlpha(c) || width <= 0 || height <= 0)
    return;
  for (int j = std::max(0, y); j < std::min(h(), y + height); ++j) {
    auto row = reinterpret_cast<QRgb *>(image.scanLine(j));
    std::fill(row + std::clamp(x, 0, w()), row + std::clamp(x + width, 0, w()),
              c);
  }
}
void Pixels::frame(int x, int y, int width, int height, QRgb c) {
  line(x, y, width, c);
  line(x, y + height - 1, width, c);
  vertical(x, y, height, c);
  vertical(x + width - 1, y, height, c);
}
void Pixels::sprite(const QJsonArray &rows, int x, int y) {
  for (int j = 0; j < rows.size(); ++j) {
    const auto s = rows[j].toString();
    for (int i = 0; i < s.size(); ++i)
      pixel(x + i, y + j, color(s[i]));
  }
}
void Pixels::stretch(const QJsonArray &rows, int repeat, int x, int y,
                     int width, int height) {
  if (width < 0)
    width = w();
  if (height < 0)
    height = h();
  const int bottom = rows.size() - repeat - 1;
  for (int j = 0; j < height; ++j) {
    auto row = rows[j < repeat             ? j
                    : j >= height - bottom ? rows.size() - (height - j)
                                           : repeat]
                   .toArray();
    auto s = row[0].toString() +
             row[1].toString().repeated(
                 std::max(0, width - int(row[0].toString().size() +
                                         row[2].toString().size()))) +
             row[2].toString();
    sprite({s.left(width)}, x, y + j);
  }
}
void button(Pixels &p, const QString &state, bool isDefault) {
  int d = isDefault ? 3 : 0;
  if (isDefault)
    p.stretch(
        table(state == "disabled" ? "DEFAULT_DISABLED" : "BUTTON_DEFAULT"),
        state == "disabled" ? 13 : 7);
  p.stretch(table(state == "pressed"    ? "BUTTON_PRESSED"
                  : state == "disabled" ? "BUTTON_DISABLED"
                                        : "BUTTON"),
            state == "disabled" ? 2 : 4, d, d, p.w() - d * 2, p.h() - d * 2);
}
void toggle(Pixels &p, bool radio, bool on, const QString &state, bool mixed) {
  if (mixed)
    p.sprite(table(state == "pressed"    ? "CHECKBOX_MIXED_PRESSED"
                   : state == "disabled" ? "CHECKBOX_MIXED_DISABLED"
                                         : "CHECKBOX_MIXED"));
  else if (radio && state == "disabled")
    p.sprite(table(on ? "RADIO_ON_DISABLED" : "RADIO_OFF_DISABLED"));
  else
    p.sprite(measured()[radio ? "RADIO" : "CHECKBOX"]
                 .toObject()[(on ? QString("on") : QString("off")) +
                             (state == "normal" ? QString() : "-" + state)]
                 .toArray());
}
void popup(Pixels &p, const QString &state) {
  if (state == "pressed") {
    p.stretch(table("POPUP_PRESSED"), 10);
    return;
  }
  auto pre = state == "disabled" ? QString("POPUP_DIS_") : QString("POPUP_");
  QJsonArray rows;
  auto left = table(pre + "LEFT"), mid = table(pre + "FILL"),
       right = table(pre + "RIGHT");
  for (int i = 0; i < left.size(); ++i)
    rows.append(QJsonArray{left[i], mid[i], right[i]});
  p.stretch(rows, 10);
}
void titleBox(Pixels &p, const QString &kind, bool pressed, int x, int y) {
  if (pressed) {
    p.sprite(table("WINDOW_" + kind.toUpper() + "_PRESSED"), x, y);
    return;
  }
  auto c = [](char a) { return color(a); };
  p.fill(x, y, 13, 13, c('c'));
  p.line(x, y, 12, c('8'));
  p.vertical(x, y, 12, c('8'));
  p.line(x + 1, y + 12, 12, c('W'));
  p.vertical(x + 12, y + 1, 12, c('W'));
  p.frame(x + 1, y + 1, 11, 11, c('2'));
  p.line(x + 2, y + 2, 8, c('c'));
  p.vertical(x + 2, y + 2, 8, c('c'));
  p.line(x + 3, y + 10, 8, c('8'));
  p.vertical(x + 10, y + 3, 8, c('8'));
  p.pixel(x + 2, y + 2, c('W'));
  QString ramp = "99aabbccddeeW";
  for (int j = 0; j < 7; ++j)
    for (int i = 0; i < 7; ++i)
      p.pixel(x + 3 + i, y + 3 + j, color(ramp[i + j]));
  if (kind == "zoom") {
    p.vertical(x + 7, y + 2, 6, c('2'));
    p.line(x + 2, y + 7, 6, c('2'));
  }
  if (kind == "collapse") {
    p.line(x + 1, y + 5, 11, c('2'));
    p.line(x + 1, y + 7, 11, c('2'));
  }
}
void windowFrame(Pixels &p, bool active, bool collapsed, int titleWidth,
                 bool boxes, bool growable, bool modal) {
  int w = p.w() - 1, h = p.h() - 1, x1 = w - 1, y1 = h - 1;
  auto ink = active ? 0xff000000 : 0xff555555;
  p.frame(0, 0, w, h, ink);
  p.vertical(x1 + 1, 2, h - 1, ink);
  p.line(2, y1 + 1, w - 1, ink);
  p.fill(1, 1, w - 2, h - 2, active ? 0xffcccccc : face);
  if (active) {
    p.line(1, 1, w - 2, 0xffffffff);
    p.vertical(1, 1, h - 2, 0xffffffff);
    p.line(1, y1 - 1, w - 2, 0xff999999);
    p.vertical(x1 - 1, 1, h - 2, 0xff999999);
    p.pixel(x1 - 1, 1, 0xffcccccc);
    p.pixel(1, y1 - 1, 0xffcccccc);
  }
  if (!collapsed) {
    int x = 6, y = modal ? 6 : 22, cw = w - 12, ch = h - y - 6;
    if (active) {
      p.line(x - 2, y - 2, cw + 3, 0xff999999);
      p.vertical(x - 2, y - 2, ch + 3, 0xff999999);
      p.line(x - 1, y + ch + 1, cw + 3, 0xffffffff);
      p.vertical(x + cw + 1, y - 1, ch + 3, 0xffffffff);
    }
    p.frame(x - 1, y - 1, cw + 2, ch + 2, ink);
    p.fill(x, y, cw, ch, face);
    if (growable)
      p.sprite(table(active ? "WINDOW_GROW" : "WINDOW_GROW_INACTIVE"),
               x + cw - 15, y + ch - 15);
  }
  if (modal)
    return;
  if (active) {
    int tx = (w - titleWidth) / 2, left = boxes ? 22 : 9,
        right = boxes ? w - 37 : x1 - 8;
    auto stripes = [&](int a, int b) {
      if (b - a < 2)
        return;
      for (int i = 0; i < 6; ++i) {
        p.line(a - 1, 4 + 2 * i, b - a, 0xffffffff);
        p.line(a, 5 + 2 * i, b - a, 0xff777777);
      }
    };
    if (titleWidth) {
      stripes(left, tx - 3);
      stripes(tx + titleWidth + 4, right);
    } else
      stripes(left, right);
    if (boxes) {
      titleBox(p, "close", false, 4, 4);
      titleBox(p, "zoom", false, w - 33, 4);
      titleBox(p, "collapse", false, w - 17, 4);
    }
  }
}
void group(Pixels &p, int labelWidth) {
  int top = labelWidth ? 7 : 0;
  p.stretch(table("GROUP"), 2, 0, top, p.w(), p.h() - top);
  if (labelWidth)
    p.fill(8, top, labelWidth + 6, 2, face);
}
void movableDialog(Pixels &p, int titleWidth, bool active) {
  p.stretch(table(active ? "MOVABLE_DIALOG" : "MOVABLE_DIALOG_INACTIVE"), 24);
  if (!active) return;
  const int w = p.w() - 1, tx = (w - titleWidth) / 2;
  auto stripes = [&](int a, int b) {
    if (b - a < 2) return;
    for (int i = 0; i < 6; ++i) {
      p.line(a - 1, 4 + i * 2, b - a, 0xffffffff);
      p.line(a, 5 + i * 2, b - a, 0xff777777);
    }
  };
  if (titleWidth > 0) {
    stripes(6, tx - 3);
    stripes(tx + titleWidth + 4, w - 5);
  } else stripes(6, w - 5);
}
void menuFrame(Pixels &p) {
  p.stretch(table("MENU_PANEL"), 2);
}
void sunken(Pixels &p) {
  p.line(0, 0, p.w() - 1, 0xff888888);
  p.vertical(0, 0, p.h() - 1, 0xff888888);
  p.line(1, p.h() - 1, p.w() - 1, 0xffffffff);
  p.vertical(p.w() - 1, 1, p.h() - 1, 0xffffffff);
  p.frame(1, 1, p.w() - 2, p.h() - 2, 0xff000000);
}
void focus(Pixels &p) {
  p.stretch(table("FIELD_FOCUS"), 3);
  p.fill(2, 2, p.w() - 4, p.h() - 4, face);
}
void bevel(Pixels &p, const QString &state) {
  p.stretch(table(state == "pressed"    ? "SMALL_BEVEL_PRESSED"
                  : state == "disabled" ? "SMALL_BEVEL_DISABLED"
                                        : "SMALL_BEVEL_NORMAL"),
            2);
}
void progress(Pixels &p, double value, int phase) {
  sunken(p);
  int x = 2, y = 2, w = p.w() - 4, h = p.h() - 4;
  p.line(0, 0, p.w() - 1, color('a'));
  p.vertical(0, 0, p.h() - 1, color('a'));
  p.pixel(0, p.h() - 1, face);
  p.pixel(p.w() - 1, 0, face);
  if (value < 0) {
    const auto tile = table("INDETERMINATE_" + QString::number((phase % 4 + 4) % 4));
    const auto end = table("INDETERMINATE_END_" + QString::number((phase % 4 + 4) % 4));
    for (int j = 0; j < h; ++j) {
      const int row = std::min(9, j);
      const auto pattern = tile[row].toString();
      for (int i = 0; i < w; ++i)
        p.pixel(x + i, y + j, color(pattern[i % 16]));
      p.pixel(x + w - 1, y + j, color(end[row].toString()[0]));
    }
    return;
  }
  QString track = "8bbbbbbbbd", trackEnd = "bddddddddd", second = "DCBAAABCDE",
          body = "EDCBABCDEG";
  QStringList end = {"EEEK5", "DDGK5", "CEGK5", "BEGK5", "BEGK5",
                     "BEGK5", "CEGK5", "DEGK5", "EEGK5", "GGGK5"};
  int filled =
      value <= 0
          ? 0
          : std::max(5, std::min(w, int(std::round(w * std::min(1.0, value)))));
  for (int j = 0; j < h; ++j) {
    int row = std::min(9, j);
    p.line(x, y + j, w, color(track[row]));
    p.pixel(x + w - 1, y + j, color(trackEnd[row]));
    p.pixel(x, y + j, color('8'));
    if (filled) {
      p.pixel(x, y + j, color('D'));
      p.pixel(x + 1, y + j, color(second[row]));
      p.line(x + 2, y + j, std::max(0, filled - 2), color(body[row]));
      if (filled < w)
        p.sprite({end[row] + "8"}, x + filled - 3, y + j);
    }
  }
}
namespace {
struct DecodedGlyph {
  int advance = 0;
  QVector<QPoint> pixels;
};
struct FontTable {
  QHash<uint, DecodedGlyph> glyphs;
  DecodedGlyph fallback;
};
DecodedGlyph decodeGlyph(const QJsonArray &g) {
  DecodedGlyph out;
  out.advance = g[0].toInt();
  const int w = g[1].toInt(), h = g[2].toInt(), dx = g[3].toInt(), dy = g[4].toInt();
  const auto rows = g[5].toArray();
  for (int j = 0; j < h; ++j) {
    const auto str = rows[j].toString();
    const auto row = str.toULongLong(nullptr, 16);
    const int bits = str.size() * 4;
    for (int i = 0; i < w; ++i)
      if (row & (quint64(1) << (bits - i - 1)))
        out.pixels.append(QPoint(dx + i, -dy - h + j));
  }
  return out;
}
const FontTable &fontTable(const QString &font) {
  static const QHash<QString, FontTable> tables = [] {
    QHash<QString, FontTable> out;
    const auto fonts = assets()["fonts"].toObject();
    for (auto it = fonts.begin(); it != fonts.end(); ++it) {
      FontTable table;
      const auto f = it.value().toObject();
      for (auto git = f.begin(); git != f.end(); ++git) {
        bool ok = false;
        const uint code = git.key().toUInt(&ok);
        const auto arr = git.value().toArray();
        if (ok && !arr.isEmpty())
          table.glyphs.insert(code, decodeGlyph(arr));
      }
      table.fallback = table.glyphs.value(63);
      out.insert(it.key(), std::move(table));
    }
    return out;
  }();
  static const FontTable empty;
  auto it = tables.constFind(font);
  return it != tables.constEnd() ? it.value() : empty;
}
inline const DecodedGlyph &glyphRef(const FontTable &table, uint code) {
  auto it = table.glyphs.constFind(code);
  return it != table.glyphs.constEnd() ? it.value() : table.fallback;
}
} // namespace
void scrollBar(Pixels &p, int thumb, int pressed) {
  const bool active = thumb >= 0;
  const int arrowTop = std::max(0, p.h() - 31);
  p.frame(0, 0, 16, p.h(), 0xff000000);
  if (active) {
    for (int y = 1; y < arrowTop; ++y) {
      p.sprite({y == 1 ? "K7777777777777cK" : y == 2 ? "K788888888888bcK"
                                                             : "K78aaaaaaaaaabcK"}, 0, y);
    }
    if (arrowTop >= 18) {
      const int ty = std::clamp(thumb, 0, arrowTop - 17);
      p.sprite(table("SCROLLBAR_THUMB"), 0, ty);
      if (ty + 17 < arrowTop) p.sprite({"K7777777777777cK"}, 0, ty + 17);
      if (ty + 18 < arrowTop) p.sprite({"K788888888888bcK"}, 0, ty + 18);
    }
  } else {
    p.fill(1, 1, 14, std::max(0, arrowTop - 1), 0xffeeeeee);
    p.line(1, 0, 14, 0xff555555);
  }
  p.sprite(table(!active ? "SCROLLBAR_UP_DISABLED" : pressed == -1 ? "SCROLLBAR_UP_HELD" : "SCROLLBAR_UP"), 0, arrowTop);
  p.sprite(table(!active ? "SCROLLBAR_DOWN_DISABLED" : pressed == 1 ? "SCROLLBAR_DOWN_HELD" : "SCROLLBAR_DOWN"), 0, arrowTop + 15);
}
int textWidth(const QString &s, const QString &font) {
  const auto &ft = fontTable(font);
  int x = 0;
  for (uint ch : s.toUcs4())
    x += glyphRef(ft, ch).advance;
  return x;
}
QString elide(const QString &s, int width, const QString &font) {
  const auto &ft = fontTable(font);
  const auto ucs4 = s.toUcs4();
  QVector<int> prefix(ucs4.size() + 1, 0);
  for (int i = 0; i < ucs4.size(); ++i)
    prefix[i + 1] = prefix[i] + glyphRef(ft, ucs4[i]).advance;
  if (prefix.last() <= width)
    return s;
  const QString ellipsis = QString::fromUtf8("…");
  const int ellipsisWidth = textWidth(ellipsis, font);
  const int budget = width - ellipsisWidth;
  int lo = 0, hi = ucs4.size();
  while (lo < hi) {
    const int mid = lo + (hi - lo + 1) / 2;
    if (prefix[mid] <= budget)
      lo = mid;
    else
      hi = mid - 1;
  }
  return QString::fromUcs4(reinterpret_cast<const char32_t *>(ucs4.constData()), lo) + ellipsis;
}
void text(Pixels &p, const QString &s, int x, int baseline, QRgb c,
          const QString &font) {
  if (!qAlpha(c))
    return;
  const auto &ft = fontTable(font);
  const int pw = p.w(), ph = p.h();
  for (uint ch : s.toUcs4()) {
    const auto &g = glyphRef(ft, ch);
    for (const QPoint pt : g.pixels) {
      const int px = x + pt.x();
      const int py = baseline + pt.y();
      if (px >= 0 && py >= 0 && px < pw && py < ph)
        reinterpret_cast<QRgb *>(p.image.scanLine(py))[px] = c;
    }
    x += g.advance;
  }
}
} // namespace Platinum
