#include "engine.h"
#include <atomic>
#include <QRandomGenerator>
using Model::fail;
Engine::Engine(Model::Machine m,QObject *p):QObject(p),machine(m),vnc(this){
    connect(&process,&QProcess::readyReadStandardOutput,this,&Engine::read);connect(&process,&QProcess::readyReadStandardError,this,[&]{log=(log+QString::fromUtf8(process.readAllStandardError())).right(65536);});
    connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[&](int code,QProcess::ExitStatus){running=false;paused=false;vnc.close();emit changed();if(!initializing){bool again=(restarting&&reason=="guest-shutdown")||(!stopping&&(reason=="guest-reset"||reason=="host-qmp-system-reset"));emit exited(again,code&&!stopping?"The Mac stopped unexpectedly. "+log:QString());}});
}
Engine::~Engine(){// Application close is refused while a guest is mounted.
    // This path is reached only after a normal exit or explicit Force Off.
    if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(5000);}
}
void Engine::read(){buffer+=process.readAllStandardOutput();if(buffer.size()>1048576){process.kill();return;}for(;;){int at=buffer.indexOf('\n');if(at<0)break;auto line=buffer.left(at);buffer.remove(0,at+1);auto o=QJsonDocument::fromJson(line).object();if(o.contains("QMP"))emit greeting();if(o["event"].toString()=="SHUTDOWN")reason=o["data"].toObject()["reason"].toString();if(o.contains("id"))emit response(o["id"].toInt(),o);}}
void Engine::start(){
    machine=Model::readMachine(machine.folder);if(machine.family()=="quadra800"){auto pram=machine.folder+"/"+machine.config["pramImageName"].toString();if(!QFileInfo::exists(pram)&&!QFile::copy(Model::runtime()+"/pram-seed.img",pram))fail("Cannot prepare parameter memory.");Model::machineFile(machine.folder,machine.config["pramImageName"].toString());}
    for(auto key:{"cdImagePath","floppyImagePath"})if(!machine.config[key].toString().isEmpty()&&!QFileInfo(machine.config[key].toString()).isFile())fail("The attached media image is missing. Choose it again in Media.");
    initializing=true;auto guard=qScopeGuard([&]{initializing=false;});
    for(int attempt=0;attempt<4;++attempt){
        QTcpServer probe;if(!probe.listen(QHostAddress::LocalHost,0))fail("Cannot reserve the guest display port.");port=probe.serverPort();probe.close();password=QByteArray::number(QRandomGenerator::system()->generate64(),16).left(8);log.clear();buffer.clear();reason.clear();stopping=restarting=false;
        auto args=Model::argumentsFor(machine,port,audioUnavailable);process.setProgram(Model::binary(machine.family()=="quadra800"?"qemu-system-m68k":"qemu-system-ppc"));process.setArguments(args);process.setWorkingDirectory(Model::runtime()+"/bin");
#ifdef Q_OS_WIN
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a){a->flags|=0x08000000;});
#endif
        QEventLoop loop;QTimer timer;timer.setSingleShot(true);bool ready=false;auto g=connect(this,&Engine::greeting,&loop,[&]{ready=true;loop.quit();});auto f=connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&loop,&QEventLoop::quit);connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);process.start();if(!process.waitForStarted(5000)){disconnect(g);disconnect(f);fail("The emulation engine could not start: "+process.errorString());}timer.start(15000);loop.exec();disconnect(g);disconnect(f);
        if(!ready){process.kill();process.waitForFinished(5000);log=(log+QString::fromUtf8(process.readAllStandardError())).right(65536);if(log.contains("Address already in use")&&attempt<3)continue;if(!audioUnavailable&&machine.config["sound"].toBool()&&QRegularExpression("Could not initialize DirectSound|could not stat pidfile .*pulse.*pid|pa_context_connect.*failed",QRegularExpression::CaseInsensitiveOption).match(log).hasMatch()){audioUnavailable=true;continue;}fail("The emulation engine did not start. "+log);}
        running=true;try{command("qmp_capabilities");command("set_password",{{"protocol","vnc"},{"password",QString::fromLatin1(password)}});}catch(...){process.kill();process.waitForFinished(5000);throw;}vnc.open(port,password);emit changed();return;
    }fail("The emulation engine could not start after retrying.");
}
QJsonValue Engine::command(QString name,QJsonObject args){
    if(process.state()!=QProcess::Running)fail("The Mac is not running.");int id=++sequence;QJsonObject result;bool received=false;QEventLoop loop;QTimer timer;timer.setSingleShot(true);auto c=connect(this,&Engine::response,&loop,[&](int n,QJsonObject o){if(n==id){result=o;received=true;loop.quit();}});auto f=connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&loop,&QEventLoop::quit);connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);QJsonObject request={{"execute",name},{"arguments",args},{"id",id}};process.write(QJsonDocument(request).toJson(QJsonDocument::Compact)+'\n');timer.start(5000);loop.exec();disconnect(c);disconnect(f);if(!received)fail("The Mac did not respond. "+log);if(result.contains("error"))fail(result["error"].toObject()["desc"].toString());return result["return"];
}
QJsonValue Engine::hmp(const QString &s){return command("human-monitor-command",{{"command-line",s}});}
void Engine::pause(){command("stop");paused=true;emit changed();}
void Engine::resume(){command("cont");paused=false;emit changed();}
void Engine::shutdown(bool restart){restarting=restart;stopping=true;if(paused)resume();hmp("sendkey power");QTimer::singleShot(750,this,[&]{if(running)try{hmp("sendkey ret");}catch(const std::exception &){};});}
void Engine::forceOff(){restarting=false;stopping=true;try{command("quit");}catch(...){process.kill();}process.waitForFinished(5000);}
QList<QStringList> Engine::pastePlan(QString s){
    s.replace("\r\n","\n");s.replace('\r','\n');s=s.normalized(QString::NormalizationForm_C);if(s.isEmpty()||s.size()>8192)fail("Paste between 1 and 8,192 characters at a time.");QMap<QChar,QStringList> map;QString lower="abcdefghijklmnopqrstuvwxyz0123456789";for(auto ch:lower)map[ch]={QString(ch)};for(auto ch:QString("ABCDEFGHIJKLMNOPQRSTUVWXYZ"))map[ch]={"shift-"+QString(ch.toLower())};
    QStringList plain={"`","-","=","[","]","\\",";","'",",",".","/","1","2","3","4","5","6","7","8","9","0"},shift={"~","_","+","{","}","|",":","\"","<",">","?","!","@","#","$","%","^","&","*","(",")"},names={"grave_accent","minus","equal","bracket_left","bracket_right","backslash","semicolon","apostrophe","comma","dot","slash","1","2","3","4","5","6","7","8","9","0"};for(int i=0;i<plain.size();++i){map[plain[i][0]]={names[i]};map[shift[i][0]]={"shift-"+names[i]};}map[' ']={"spc"};map['\t']={"tab"};map['\n']={"ret"};
    QStringList accents={"e|áéíóú|ÁÉÍÓÚ|aeiou","u|äëïöüÿ|ÄËÏÖÜŸ|aeiouy","i|âêîôû|ÂÊÎÔÛ|aeiou","grave_accent|àèìòù|ÀÈÌÒÙ|aeiou","n|ãñõ|ÃÑÕ|ano"};for(auto a:accents){auto p=a.split('|');for(int i=0;i<p[1].size();++i){map[p[1][i]]={"alt-"+p[0],QString(p[3][i])};map[p[2][i]]={"alt-"+p[0],"shift-"+QString(p[3][i])};}}for(auto item:QStringList{"ç|alt-c","Ç|alt-shift-c","å|alt-a","Å|alt-shift-a","ø|alt-o","Ø|alt-shift-o","ß|alt-s"}){auto a=item.split('|');map[a[0][0]]={a[1]};}QList<QStringList> result;for(auto ch:s){if(!map.contains(ch))fail(QString("Unsupported character U+%1. Nothing has been pasted.").arg(uint(ch.unicode()),4,16,QChar('0')));result<<map[ch];}return result;
}
void Engine::paste(QString text,bool slow,std::atomic_bool &cancel,const std::function<void(int,int)> &progress){auto plan=pastePlan(text);int complete=0;for(auto sequence:plan){if(cancel||!running||paused)fail("Pasting stopped. Text already entered remains in the Mac.");for(auto key:sequence){auto response=hmp("sendkey "+key+" 1").toString();if(QRegularExpression("unknown key|error:|unknown command",QRegularExpression::CaseInsensitiveOption).match(response).hasMatch())fail("The Mac rejected a key chord.");QEventLoop loop;QTimer::singleShot(slow?125:50,&loop,&QEventLoop::quit);loop.exec();}progress(++complete,plan.size());}}
