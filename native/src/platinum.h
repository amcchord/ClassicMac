#pragma once
#include <QImage>
#include <QJsonObject>
#include <QString>

namespace Platinum {
constexpr QRgb face = 0xffdddddd;
struct Pixels {
    QImage image;
    Pixels(int w, int h, QRgb background = face);
    int w() const { return image.width(); }
    int h() const { return image.height(); }
    void pixel(int x, int y, QRgb c);
    void fill(int x, int y, int w, int h, QRgb c);
    void line(int x,int y,int w,QRgb c) { fill(x,y,w,1,c); }
    void vertical(int x,int y,int h,QRgb c) { fill(x,y,1,h,c); }
    void frame(int x,int y,int w,int h,QRgb c);
    void sprite(const QJsonArray &rows,int x=0,int y=0);
    void stretch(const QJsonArray &rows,int repeat,int x=0,int y=0,int width=-1,int height=-1);
};
QRgb color(QChar ch);
const QJsonObject &measured();
void button(Pixels &,const QString &state="normal",bool isDefault=false);
void toggle(Pixels &,bool radio,bool on,const QString &state="normal",bool mixed=false);
void popup(Pixels &,const QString &state="normal");
void titleBox(Pixels &,const QString &kind,bool pressed=false,int x=0,int y=0);
void windowFrame(Pixels &,bool active=true,bool collapsed=false,int titleWidth=70,bool boxes=true,bool growable=true,bool modal=false);
void group(Pixels &,int labelWidth=0);
void sunken(Pixels &);
void focus(Pixels &);
void bevel(Pixels &,const QString &state="normal");
void progress(Pixels &,double value=0,int phase=0);
int textWidth(const QString &,const QString &font="system");
QString elide(const QString &,int width,const QString &font="system");
void text(Pixels &,const QString &,int x=0,int baseline=12,QRgb c=0xff000000,const QString &font="system");
}
