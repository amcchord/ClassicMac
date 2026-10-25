#include "widgets.h"
#include <QStyledItemDelegate>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
using namespace Platinum;
static void draw(QWidget *w, const QImage &image) {
  QPainter p(w);
  p.setRenderHint(QPainter::SmoothPixmapTransform, false);
  p.drawImage(0, 0, image);
}
static QStringList lines(const QString &s, int width, const QString &font) {
  QStringList result;
  for (auto paragraph : s.split('\n')) {
    QString row;
    for (auto word : paragraph.split(' ')) {
      if (!row.isEmpty() && textWidth(row + " " + word, font) > width) {
        result << row;
        row.clear();
      }
      row += (row.isEmpty() ? "" : " ") + word;
    }
    result << row;
  }
  return result;
}
PLabel::PLabel(QString s, QWidget *p) : QWidget(p), value(s) {
  setAccessibleName(s);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}
QSize PLabel::sizeHint() const {
  return {textWidth(value, font), heightForWidth(width() > 0 ? width() : 400)};
}
int PLabel::heightForWidth(int w) const {
  return (wrap ? lines(value, w, font).size() : value.count('\n') + 1) *
         (font == "small" ? 13 : 16);
}
void PLabel::paintEvent(QPaintEvent *) {
  Pixels p(width(), height());
  auto rows = wrap ? lines(value, width(), font) : value.split('\n');
  int y = font == "small" ? 10 : 12;
  for (auto &row : rows) {
    text(p, elide(row, width(), font), 0, y, isEnabled() ? ink : 0xff888888,
         font);
    y += font == "small" ? 13 : 16;
  }
  draw(this, p.image);
}
PButton::PButton(const QString &s, QWidget *p, bool def)
    : QAbstractButton(p), defaultButton(def) {
  setText(s);
  setAccessibleName(s);
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  connect(this, &QAbstractButton::pressed, this, qOverload<>(&QWidget::update));
  connect(this, &QAbstractButton::released, this,
          qOverload<>(&QWidget::update));
  connect(this, &QAbstractButton::toggled, this, qOverload<>(&QWidget::update));
}
QSize PButton::sizeHint() const {
  if (!widgetKind.isEmpty())
    return {13, 13};
  if (toggleButton)
    return {18 + textWidth(text()), 18};
  return {std::max(59, textWidth(text()) + 24) + (defaultButton ? 6 : 0),
          defaultButton ? 26 : 20};
}
void PButton::paintEvent(QPaintEvent *) {
  Pixels p(width(), height());
  QString state = !isEnabled() ? "disabled" : isDown() ? "pressed" : "normal";
  if (!widgetKind.isEmpty()) {
    titleBox(p, widgetKind, isDown());
  } else if (toggleButton) {
    Platinum::toggle(p, radio, isChecked(), state);
    Platinum::text(p, text(), 18, 12, isEnabled() ? 0xff000000 : 0xff888888);
  } else {
    Platinum::button(p, state, defaultButton);
    Platinum::text(p, text(), (width() - textWidth(text())) / 2,
                   (height() - 16) / 2 + 12,
                   !isEnabled() ? 0xff888888
                   : isDown()   ? 0xffffffff
                                : 0xff000000);
  }
  if (hasFocus() && widgetKind.isEmpty() && !defaultButton) {
    p.frame(0, 0, width(), height(), 0xff6666cc);
  }
  draw(this, p.image);
}
PEdit::PEdit(const QString &s, QWidget *p) : QLineEdit(s, p) {
  setFixedHeight(22);
  setMaxLength(100);
  setFrame(false);
  setContextMenuPolicy(Qt::NoContextMenu);
  connect(this, &QLineEdit::textChanged, this, qOverload<>(&QWidget::update));
  connect(this, &QLineEdit::cursorPositionChanged, this,
          qOverload<>(&QWidget::update));
  connect(this, &QLineEdit::selectionChanged, this,
          qOverload<>(&QWidget::update));
}
void PEdit::paintEvent(QPaintEvent *) {
  Pixels p(width(), height());
  sunken(p);
  p.fill(2, 2, width() - 4, height() - 4,
         isEnabled() ? 0xffffffff : 0xffeeeeee);
  int cur = textWidth(text().left(cursorPosition()), "regular");
  scroll =
      std::clamp(scroll, std::max(0, cur - width() + 12), std::max(0, cur));
  int x = 5 - scroll;
  const int first = selectionStart(), last = first + int(selectedText().size());
  for (int i = 0; i < text().size(); ++i) {
    QString ch = text().mid(i, 1);
    int w = textWidth(ch, "regular");
    if (first >= 0 && i >= first && i < last)
      p.fill(x, 3, w, 16, 0xffccccff);
    Platinum::text(p, ch, x, 16, isEnabled() ? 0xff000000 : 0xff888888,
                   "regular");
    x += w;
  }
  if (hasFocus()) {
    p.vertical(5 + cur - scroll, 4, 14, 0xff000000);
    p.frame(0, 0, width(), height(), 0xff6666cc);
  }
  draw(this, p.image);
}
int PEdit::position(int x) const {
  int pos = 0, advance = 5 - scroll;
  while (pos < text().size()) {
    int w = textWidth(text().mid(pos, 1), "regular");
    if (x < advance + w / 2)
      break;
    advance += w;
    ++pos;
  }
  return pos;
}
void PEdit::mousePressEvent(QMouseEvent *e) {
  setFocus();
  anchor = position(e->position().x());
  setCursorPosition(anchor);
  update();
}
void PEdit::mouseMoveEvent(QMouseEvent *e) {
  if (e->buttons() & Qt::LeftButton)
    setSelection(anchor, position(e->position().x()) - anchor);
}
class BitmapDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter *q, const QStyleOptionViewItem &o,
             const QModelIndex &i) const override {
    Pixels p(o.rect.width(), o.rect.height(),
             o.state & QStyle::State_Selected ? 0xff6666cc : face);
    text(p, elide(i.data().toString(), p.w() - 16), 8, 14,
         o.state & QStyle::State_Selected ? 0xffffffff : 0xff000000);
    q->save();
    q->setRenderHint(QPainter::SmoothPixmapTransform, false);
    q->drawImage(o.rect.topLeft(), p.image);
    q->restore();
  }
  QSize sizeHint(const QStyleOptionViewItem &,
                 const QModelIndex &i) const override {
    return {textWidth(i.data().toString()) + 25, 20};
  }
};
PCombo::PCombo(QWidget *p) : QComboBox(p) {
  setFixedHeight(20);
  setItemDelegate(new BitmapDelegate(this));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}
QSize PCombo::sizeHint() const {
  int w = 70;
  for (int i = 0; i < count(); ++i)
    w = std::max(w, textWidth(itemText(i)) + 36);
  return {w, 20};
}
void PCombo::paintEvent(QPaintEvent *) {
  Pixels p(width(), height());
  popup(p, isEnabled() ? "normal" : "disabled");
  text(p, elide(currentText(), width() - 36), 9, 14,
       isEnabled() ? 0xff000000 : 0xff888888);
  if (hasFocus())
    p.frame(1, 1, width() - 2, height() - 2, 0xff6666cc);
  draw(this, p.image);
}
PGroup::PGroup(QString s, QWidget *p) : QWidget(p), title(s) {}
void PGroup::paintEvent(QPaintEvent *) {
  Pixels p(width(), height());
  group(p, textWidth(title));
  text(p, title, 11, 12);
  draw(this, p.image);
}
void PProgress::paintEvent(QPaintEvent *) {
  Pixels p(width(), height());
  progress(p, value);
  draw(this, p.image);
}
class FrameBody : public QWidget {
  PWindow *owner;

public:
  explicit FrameBody(PWindow *window) : QWidget(window), owner(window) {}
  void paintEvent(QPaintEvent *) override {
    if (!owner->growable || owner->shaded)
      return;
    Pixels pixels(17, 17);
    pixels.sprite(measured()[owner->isActiveWindow() ? "WINDOW_GROW"
                                                     : "WINDOW_GROW_INACTIVE"]
                      .toArray());
    QPainter(this).drawImage(width() - 15, height() - 15, pixels.image);
  }
};
PWindow::PWindow(QString s, QSize size, QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint), title(s) {
  setWindowTitle(s);
  setAttribute(Qt::WA_OpaquePaintEvent);
  body = new FrameBody(this);
  body->setAutoFillBackground(true);
  auto palette = body->palette();
  palette.setColor(QPalette::Window, QColor(0xdd, 0xdd, 0xdd));
  body->setPalette(palette);
  resize(size);
  setMinimumSize(300, 150);
  setFocusPolicy(Qt::StrongFocus);
  setSizeIncrement(1, 1);
}
void PWindow::paintEvent(QPaintEvent *) {
  Pixels p(width(), height());
  bool active = isActiveWindow();
  windowFrame(p, active, shaded, textWidth(title), true, growable);
  text(p, title, (width() - 1 - textWidth(title)) / 2, 16,
       active ? 0xff000000 : 0xff888888);
  if (!held.isEmpty())
    titleBox(p, held, true,
             held == "close" ? 4 : width() - 1 - (held == "zoom" ? 33 : 17), 4);
  draw(this, p.image);
}
void PWindow::resizeEvent(QResizeEvent *) {
  body->setGeometry(6, 22, width() - 13, std::max(0, height() - 29));
}
void PWindow::fitToScreen() {
  if (isFullScreen() || isMaximized() || shaded || !screen())
    return;
  auto available = screen()->availableGeometry().adjusted(4, 4, -4, -4);
  if (available.isEmpty())
    return;
  resize(size().boundedTo(available.size()).expandedTo(minimumSize()));
  move(std::clamp(pos().x(), available.left(),
                  std::max(available.left(), available.right() - width() + 1)),
       std::clamp(pos().y(), available.top(),
                  std::max(available.top(), available.bottom() - height() + 1)));
}
void PWindow::showEvent(QShowEvent *e) {
  QWidget::showEvent(e);
  if (!screenConnected && windowHandle()) {
    screenConnected = true;
    connect(windowHandle(), &QWindow::screenChanged, this, [this] {
      followWorkArea();
      QTimer::singleShot(0, this, &PWindow::fitToScreen);
    });
    followWorkArea();
  }
  QTimer::singleShot(0, this, &PWindow::fitToScreen);
}
void PWindow::followWorkArea() {
  disconnect(workAreaConnection);
  if (screen())
    workAreaConnection = connect(screen(), &QScreen::availableGeometryChanged, this, [this] {
      QTimer::singleShot(0, this, &PWindow::fitToScreen);
    });
}
bool PWindow::event(QEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  if (e->type() == QEvent::DevicePixelRatioChange)
    QTimer::singleShot(0, this, &PWindow::fitToScreen);
#endif
  return QWidget::event(e);
}
#ifdef Q_OS_WIN
bool PWindow::nativeEvent(const QByteArray &type, void *message, qintptr *result) {
  auto msg = static_cast<MSG *>(message);
  const qreal dpr = devicePixelRatioF();
  const int zoom = qRound(dpr);
  if (msg->message == WM_SIZING && zoom > 1 && qAbs(dpr - zoom) < 0.001) {
    auto r = reinterpret_cast<RECT *>(msg->lParam);
    RECT outer{}, client{};
    GetWindowRect(msg->hwnd, &outer);
    GetClientRect(msg->hwnd, &client);
    const int borderW = outer.right - outer.left - client.right;
    const int borderH = outer.bottom - outer.top - client.bottom;
    const int width = qRound(qreal(r->right - r->left - borderW) / zoom) * zoom + borderW;
    const int height = qRound(qreal(r->bottom - r->top - borderH) / zoom) * zoom + borderH;
    const bool left = msg->wParam == WMSZ_LEFT || msg->wParam == WMSZ_TOPLEFT || msg->wParam == WMSZ_BOTTOMLEFT;
    const bool top = msg->wParam == WMSZ_TOP || msg->wParam == WMSZ_TOPLEFT || msg->wParam == WMSZ_TOPRIGHT;
    if (left) r->left = r->right - width;
    else r->right = r->left + width;
    if (top) r->top = r->bottom - height;
    else r->bottom = r->top + height;
    *result = TRUE;
    return true;
  }
  return QWidget::nativeEvent(type, message, result);
}
#endif
void PWindow::changeEvent(QEvent *e) {
  if (e->type() == QEvent::ActivationChange) {
    update();
    body->update();
  }
  QWidget::changeEvent(e);
}
QString PWindow::hit(QPoint p) const {
  if (p.y() >= 4 && p.y() < 17) {
    if (p.x() >= 4 && p.x() < 17)
      return "close";
    if (p.x() >= width() - 34 && p.x() < width() - 21)
      return "zoom";
    if (p.x() >= width() - 18 && p.x() < width() - 5)
      return "collapse";
  }
  return {};
}
void PWindow::shade() {
  if (!allowShade)
    return;
  if (shaded) {
    shaded = false;
    body->show();
    setMinimumSize(expandedMinimum);
    resize(expanded);
    QTimer::singleShot(0, this, &PWindow::fitToScreen);
  } else {
    if (isMaximized() || isFullScreen())
      showNormal();
    expanded = size();
    expandedMinimum = minimumSize();
    shaded = true;
    body->hide();
    setMinimumHeight(23);
    resize(width(), 23);
  }
  update();
}
void PWindow::mousePressEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton)
    return;
  held = hit(e->position().toPoint());
  drag = e->globalPosition().toPoint();
  origin = pos();
  startSize = size();
  resizing = growable && !shaded && e->position().x() > width() - 23 &&
             e->position().y() > height() - 23;
  if (held.isEmpty() && windowHandle()) {
    bool moved =
        resizing
            ? windowHandle()->startSystemResize(Qt::BottomEdge | Qt::RightEdge)
            : e->position().y() < 21 && windowHandle()->startSystemMove();
    if (moved) {
      drag = {};
      return;
    }
  }
  if (!held.isEmpty())
    update();
  else if (e->position().y() < 21 || resizing)
    grabMouse();
  else
    drag = {};
}
void PWindow::mouseMoveEvent(QMouseEvent *e) {
  if (drag.isNull() || !held.isEmpty() || !(e->buttons() & Qt::LeftButton))
    return;
  auto delta = e->globalPosition().toPoint() - drag;
  if (resizing)
    resize(startSize + QSize(delta.x(), delta.y()));
  else
    move(origin + delta);
}
void PWindow::mouseReleaseEvent(QMouseEvent *e) {
  releaseMouse();
  auto a = held;
  held.clear();
  drag = {};
  if (a == hit(e->position().toPoint())) {
    if (a == "close")
      close();
    else if (a == "collapse")
      shade();
    else if (a == "zoom") {
      if (isMaximized())
        showNormal();
      else
        showMaximized();
    }
  }
  update();
}
void PWindow::mouseDoubleClickEvent(QMouseEvent *e) {
  if (e->position().y() < 21 && hit(e->position().toPoint()).isEmpty())
    shade();
}
void PWindow::closeEvent(QCloseEvent *e) {
  if (canClose && !canClose()) {
    e->ignore();
    return;
  }
  e->accept();
  emit closed();
}
void PWindow::keyPressEvent(QKeyEvent *e) {
  if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
    for (auto b : body->findChildren<PButton *>())
      if (b->defaultButton && b->isVisible() && b->isEnabled()) {
        b->animateClick();
        return;
      }
  }
  QWidget::keyPressEvent(e);
}
PDialog::PDialog(QString s, QSize size, QWidget *p) : PWindow(s, size, p) {
  allowShade = false;
  growable = false;
  setWindowModality(Qt::WindowModal);
  connect(this, &PWindow::closed, &loop, &QEventLoop::quit);
}
int PDialog::exec() {
  if (auto parent = qobject_cast<PWindow *>(parentWidget());
      parent && parent->shaded)
    parent->shade();
  if (parentWidget())
    move(parentWidget()->geometry().center() -
         QPoint(width() / 2, height() / 2));
  show();
  raise();
  activateWindow();
  loop.exec();
  hide();
  return result;
}
void PDialog::done(int code) {
  result = code;
  close();
}
PLabel *label(QString s, QWidget *p, QString font, bool wrap) {
  auto w = new PLabel(s, p);
  w->font = font;
  w->wrap = wrap;
  w->setSizePolicy(QSizePolicy::Preferred,
                   wrap ? QSizePolicy::Minimum : QSizePolicy::Fixed);
  return w;
}
PButton *button(QString s, std::function<void()> action, QWidget *p, bool def) {
  auto b = new PButton(s, p, def);
  QObject::connect(b, &PButton::clicked, b, [action] {
    try {
      action();
    } catch (const std::exception &e) {
      problem(QApplication::activeWindow(), QString::fromUtf8(e.what()));
    }
  });
  return b;
}
QHBoxLayout *buttonRow(QVBoxLayout *v, const QList<QWidget *> &widgets) {
  auto h = new QHBoxLayout;
  h->setSpacing(12);
  h->addStretch();
  for (auto w : widgets)
    h->addWidget(w);
  auto footer = qobject_cast<QVBoxLayout *>(v->property("dialogFooter").value<QObject *>());
  (footer ? footer : v)->addLayout(h);
  return h;
}
QVBoxLayout *dialogLayout(PWindow *d) {
  auto outer = new QVBoxLayout(d->body);
  outer->setContentsMargins(12, 12, 12, 12);
  outer->setSpacing(12);
  auto area = new QScrollArea(d->body);
  area->setFrameShape(QFrame::NoFrame);
  area->setWidgetResizable(true);
  area->setAutoFillBackground(false);
  area->viewport()->setAutoFillBackground(false);
  auto content = new QWidget;
  auto v = new QVBoxLayout(content);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(12);
  v->setProperty("dialogFooter", QVariant::fromValue<QObject *>(outer));
  area->setWidget(content);
  outer->addWidget(area, 1);
  return v;
}
bool question(QWidget *p, const QString &title, const QString &message,
              QString accept) {
  PDialog d(title, {450, 210}, p);
  auto v = dialogLayout(&d);
  v->addWidget(label(message, &d, "regular", true), 1);
  buttonRow(v, {button(
                    "Cancel", [&] { d.done(0); }, &d),
                button(accept, [&] { d.done(1); }, &d, true)});
  return d.exec() == 1;
}
void problem(QWidget *p, const QString &message) {
  PDialog d("ClassicMac", {480, 240}, p);
  auto v = dialogLayout(&d);
  auto area = new QScrollArea(&d);
  area->setWidgetResizable(true);
  area->setFrameShape(QFrame::NoFrame);
  area->setWidget(label(message, &d, "regular", true));
  v->addWidget(area, 1);
  buttonRow(v, {button("OK", [&] { d.done(); }, &d, true)});
  d.exec();
}
class PStyle : public QProxyStyle {
public:
  PStyle() : QProxyStyle("Fusion") {}
  void drawControl(ControlElement ce, const QStyleOption *o, QPainter *q,
                   const QWidget *w = nullptr) const override {
    if (ce == CE_SizeGrip) {
      Pixels p(o->rect.width(), o->rect.height());
      p.sprite(measured()[o->state & State_Active ? "WINDOW_GROW"
                                                : "WINDOW_GROW_INACTIVE"].toArray());
      q->save();
      q->setRenderHint(QPainter::SmoothPixmapTransform, false);
      q->drawImage(o->rect.topLeft(), p.image);
      q->restore();
      return;
    }
    if (ce == CE_ShapedFrame && (qobject_cast<const QListWidget *>(w) ||
                                qobject_cast<const QPlainTextEdit *>(w))) {
      Pixels p(o->rect.width(), o->rect.height(), 0);
      sunken(p);
      q->save();
      q->setRenderHint(QPainter::SmoothPixmapTransform, false);
      q->drawImage(o->rect.topLeft(), p.image);
      q->restore();
      return;
    }
    QProxyStyle::drawControl(ce, o, q, w);
  }
  int pixelMetric(PixelMetric m, const QStyleOption *o = nullptr,
                  const QWidget *w = nullptr) const override {
    if (m == PM_ScrollBarExtent)
      return 16;
    if (m == PM_ScrollBarSliderMin)
      return 17;
    if (m == PM_DefaultFrameWidth)
      return 1;
    return QProxyStyle::pixelMetric(m, o, w);
  }
  QRect subControlRect(ComplexControl cc, const QStyleOptionComplex *o,
                       SubControl sc,
                       const QWidget *widget = nullptr) const override {
    if (cc != CC_ScrollBar)
      return QProxyStyle::subControlRect(cc, o, sc, widget);
    auto s = qstyleoption_cast<const QStyleOptionSlider *>(o);
    if (!s)
      return {};
    bool vertical = s->orientation == Qt::Vertical;
    int length = vertical ? o->rect.height() : o->rect.width();
    int pos = 16 + (s->maximum == s->minimum
                        ? 0
                        : QStyle::sliderPositionFromValue(
                              s->minimum, s->maximum, s->sliderPosition,
                              std::max(0, length - 49), s->upsideDown));
    auto rect = [&](int at, int n) {
      return vertical ? QRect(o->rect.x(), o->rect.y() + at, 16, n)
                      : QRect(o->rect.x() + at, o->rect.y(), n, 16);
    };
    switch (sc) {
    case SC_ScrollBarSubLine:
      return rect(0, 16);
    case SC_ScrollBarAddLine:
      return rect(length - 16, 16);
    case SC_ScrollBarGroove:
      return rect(16, length - 32);
    case SC_ScrollBarSlider:
      return rect(pos, 17);
    case SC_ScrollBarSubPage:
      return rect(16, std::max(0, pos - 16));
    case SC_ScrollBarAddPage:
      return rect(pos + 17, std::max(0, length - pos - 33));
    default:
      return {};
    }
  }
  SubControl hitTestComplexControl(ComplexControl cc,
                                   const QStyleOptionComplex *o,
                                   const QPoint &point,
                                   const QWidget *w = nullptr) const override {
    if (cc != CC_ScrollBar)
      return QProxyStyle::hitTestComplexControl(cc, o, point, w);
    for (auto sc :
         {SC_ScrollBarSubLine, SC_ScrollBarAddLine, SC_ScrollBarSlider,
          SC_ScrollBarSubPage, SC_ScrollBarAddPage})
      if (subControlRect(cc, o, sc, w).contains(point))
        return sc;
    return SC_None;
  }
  void drawComplexControl(ComplexControl cc, const QStyleOptionComplex *o,
                          QPainter *q,
                          const QWidget *w = nullptr) const override {
    if (cc != CC_ScrollBar) {
      QProxyStyle::drawComplexControl(cc, o, q, w);
      return;
    }
    auto s = qstyleoption_cast<const QStyleOptionSlider *>(o);
    if (!s)
      return;
    bool vertical = s->orientation == Qt::Vertical;
    Pixels p(16, vertical ? o->rect.height() : o->rect.width());
    auto slider = subControlRect(cc, o, SC_ScrollBarSlider, w);
    int pos = vertical ? slider.y() - o->rect.y() : slider.x() - o->rect.x();
    int pressed = (o->state & State_Sunken)
                      ? (o->activeSubControls & SC_ScrollBarSubLine   ? -1
                         : o->activeSubControls & SC_ScrollBarAddLine ? 1
                                                                      : 0)
                      : 0;
    scrollBar(p, s->maximum > s->minimum ? pos : -1, pressed);
    q->save();
    q->setRenderHint(QPainter::SmoothPixmapTransform, false);
    q->drawImage(o->rect.topLeft(),
                 vertical ? p.image
                          : p.image.transformed(QTransform().rotate(-90)));
    q->restore();
  }
  void drawPrimitive(PrimitiveElement pe, const QStyleOption *o, QPainter *p,
                     const QWidget *w = nullptr) const override {
    if (pe == PE_PanelScrollAreaCorner) {
      p->fillRect(o->rect, QColor(face));
      return;
    }
    if (pe == PE_PanelMenu || pe == PE_PanelItemViewItem ||
        pe == PE_FrameFocusRect) {
      p->fillRect(o->rect,
                  QColor(pe == PE_FrameFocusRect ? 0xccccff : 0xdddddd));
      return;
    }
    QProxyStyle::drawPrimitive(pe, o, p, w);
  }
};
void installPlatinumStyle() {
  QFontDatabase::addApplicationFont(":/RetroBoot-Regular.ttf");
  QFontDatabase::addApplicationFont(":/RetroBoot-System.ttf");
  qApp->setStyle(new PStyle);
  QPalette p;
  p.setColor(QPalette::Window, QColor(0xdddddd));
  p.setColor(QPalette::Base, Qt::white);
  p.setColor(QPalette::Text, Qt::black);
  p.setColor(QPalette::Highlight, QColor(0xccccff));
  p.setColor(QPalette::HighlightedText, Qt::black);
  qApp->setPalette(p);
}
