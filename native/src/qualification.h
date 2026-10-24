#pragma once
#include <QString>
class App;
int selfTest(const QString &fixtures,const QString &output={});
void qualify(App *,const QString &recipe);
