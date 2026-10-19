#!/usr/bin/env python3
"""Exercise the native Mac display/input and Quake III on a disposable clone.

Requires an independently supplied SuperDeluxe disk; no guest OS/game content
is distributed with this test. Output contains local screenshots and metrics.
Use --baseline to measure a released engine without the new USB HID device.
"""
import argparse, hashlib, json, os, re, struct, subprocess, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'ati/tests'), str(ROOT/'display/tests')]
import guest_run as G
import deluxe_q3_timedemo as D
import cmdclient as C


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('qemu','bios','disk','ndrvloader','tools-cd','out'): ap.add_argument('--'+key,required=True)
    ap.add_argument('--baseline',action='store_true')
    ap.add_argument('--inspect',action='store_true')
    ap.add_argument('--profile',action='store_true')
    ap.add_argument('--audio',default='wav')
    ap.add_argument('--cpu',default='7400')
    a=ap.parse_args()
    for key in ('qemu','bios','disk','ndrvloader','tools_cd','out'): setattr(a,key,str(Path(getattr(a,key)).resolve()))
    out=Path(a.out);out.mkdir(exist_ok=False,parents=True)
    subprocess.run(['cp','-c',a.disk,str(out/'disk.img')],check=True)
    os.chdir(out.parent);name=out.name
    c=C.DisplayClient(str(Path(name)/'display.sock'))
    args=argparse.Namespace(out=name,qemu=a.qemu,bios=a.bios,disk=str(out/'disk.img'),card='ati',size='1024x768',depth=32,ram=512,name='Quake III regression',icount=False,display_option=['classicmac-display','retroboot-display'],ndrvloader=a.ndrvloader,tools_cd=a.tools_cd,extra=[])
    argv=G.build_argv(args)
    argv[argv.index('-cpu')+1]=a.cpu
    argv=[x.replace(str(out),name) for x in argv]
    argv[argv.index('-display')+1]='classicmac,socket='+name+'/display.sock'
    argv[argv.index('-audiodev')+1]=('wav,id=snd0,path='+name+'/audio.wav') if a.audio=='wav' else a.audio+',id=snd0'
    if not a.baseline: argv+=['-device','usb-mouse,id=classicmac-game-mouse']
    (out/'argv.json').write_text(json.dumps(argv,indent=2))
    p=subprocess.Popen(argv,stdout=(out/'qemu.log').open('wb'),stderr=subprocess.STDOUT,start_new_session=True)
    (out/'qemu.pid').write_text(str(p.pid))
    awake=subprocess.Popen(['caffeinate','-dimsu','-w',str(p.pid)])
    result={'baseline':a.baseline,'qemu':a.qemu,'started':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}
    q=None
    def mode():
        return next((e['relative'] for _,e in reversed(c.events) if e.get('event')=='pointer-mode'),None)
    def wait(predicate,timeout=60):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            c.pump(.05)
            if predicate():return
            if p.poll() is not None:raise RuntimeError('QEMU exited '+str(p.returncode))
        raise RuntimeError('Timed out waiting for guest state')
    def shot(label):
        c.pump(.2);return c.snapshot(str(out/(label+'.png')))
    def typing(text):
        special={' ':'spc','.':'dot','-':'minus','_':'shift-minus','\\':'backslash',';':'semicolon','/':'slash'}
        for char in text:
            chord=special.get(char,char.lower() if char.islower() or char.isdigit() else 'shift-'+char.lower())
            c.tap(*chord.split('-'),hold=.035)
    def ensure_console():
        for attempt in range(3):
            shot('console-state-'+str(attempt))
            # Quake's half-height console has a solid yellow separator.
            # This remains visible when the small version label defeats OCR.
            if all((lambda rgb: rgb[0]==rgb[1] and rgb[0]>=60 and rgb[2]==0)(c.pixel(x,238)) for x in (20,100,300,500)):
                return
            c.tap('grave_accent');c.pump(.35)
        raise RuntimeError('Quake console did not open')
    def command(text):
        ensure_console()
        typing('\\'+text);c.tap('ret');c.pump(.25)
    def stats():
        s=q.cmd('qom-get',path='/machine/peripheral/ati0',property='x-accel-stats')
        return {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',s)}
    def frame():
        s=c.surface;return C.xrgb_to_rgb(c.frame,s['width'],s['height'],s['stride'])
    try:
        c.accept(15);wait(lambda:c.surface is not None)
        q=G.QMP(name)
        t0=time.monotonic()
        def finder():
            if c.surface['width']!=1024:return False
            px=[c.pixel(x,y) for y in range(3,18,2) for x in range(35,350,2)]
            return sum(max(v)<100 for v in px)>=180 and sum(min(v)>170 for v in px)>=len(px)/2
        wait(finder,180);c.pump(2);result['boot_s']=round(time.monotonic()-t0,3);result['finder_relative']=mode();shot('finder')
        c.click(975,55);c.pump(.08);c.click(975,55);c.pump(1)
        for text in ('Games','Quake III','MacQuake3'):
            typing(text);c.tap('meta_l','o');c.pump(1.5)
        wait(lambda:c.surface['width']==640,120)
        t0=time.monotonic();measure=[];previous=None;changes=[]
        while time.monotonic()-t0<20:
            c.pump(.015);t=time.monotonic()-t0
            current=hashlib.sha256(c.frame).hexdigest()
            if current!=previous:changes.append(t);previous=current
            if not measure or t-measure[-1]['t']>=.25:
                measure.append({'t':t,**stats()})
            for second in range(20):
                if second<=t<second+.04 and not (out/f'first-{second}.png').exists():shot(f'first-{second}')
        result['first_intro_samples']=measure;result['first_frame_changes']=changes
        shot('menu')
        result['game_relative']=mode()
        if a.inspect:
            q.close();q=None;last_shot=0
            while True:
                c.pump(.05)
                if time.monotonic()-last_shot>1:
                    shot('live');last_shot=time.monotonic()
                    (out/'events.json').write_text(json.dumps(c.events,indent=2))
                controls=out/'commands.json'
                if controls.exists():
                    requests=json.loads(controls.read_text());controls.unlink()
                    for request in requests:
                        if 'tap' in request:c.tap(*request['tap'])
                        if 'command' in request:command(request['command'])
                        if 'type' in request:typing(request['type'])
                        if 'motion' in request:c.pointer_rel(*request['motion'])


        c.pointer_rel(100,70);c.pump(.5);shot('menu-motion')
        c.pointer_rel(220,75);c.pump(.2);c.button(0,True);c.pump(.12);c.button(0,False);c.pump(1);shot('menu-click')
        result['menu_click']=('UNNAMEDPLAYER' in D.ocr(str(out/'menu-click.png')).upper().replace(' ',''))
        command('map q3dm1');c.pump(6);shot('arena-before')
        before=frame();c.pump(.5);idle=frame();c.pointer_rel(120,0);c.pump(.5);after=frame();shot('arena-look')
        result['idle_changed_fraction']=sum(abs(x-y)>24 for x,y in zip(before,idle))/len(before)
        result['look_changed_fraction']=sum(abs(x-y)>24 for x,y in zip(idle,after))/len(after)
        c.button(0,True);c.pump(.8);c.button(0,False);c.pump(.2);shot('arena-fire')
        command('soundinfo');shot('sound-info')
        command('disconnect');c.pump(1)
        initial=stats();audio_file=out/'audio.wav';audio_start=audio_file.stat().st_size if audio_file.exists() else 0
        command('cinematic idlogo.RoQ')
        if a.profile: subprocess.Popen(['sample',str(p.pid),'3','-file',str(out/'movie-profile.txt')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        t0=time.monotonic();samples=[];last=None;frames=[];laststat=-1
        while time.monotonic()-t0<11:
            c.pump(.015);t=time.monotonic()-t0
            digest=hashlib.sha256(c.frame).hexdigest()
            if digest!=last:frames.append(round(t,4));last=digest
            if t-laststat>=.25:
                v=stats();samples.append({'t':round(t,4),'uploads':v['tex_uploads']-initial['tex_uploads'],'draws':v['draws3d']-initial['draws3d']});laststat=t
            for second in (1,3,5):
                if second<=t<second+.04 and not (out/f'intro-{second}.png').exists():shot(f'intro-{second}')
        result['intro_samples']=samples;result['display_change_times']=frames
        if audio_file.exists():
            raw=audio_file.read_bytes()[max(44,audio_start):];raw=raw[:len(raw)//4*4]
            pcm=struct.unpack('<%dh'%(len(raw)//2),raw)
            bins=[max(map(abs,pcm[i:i+882]),default=0) for i in range(0,len(pcm),882)]
            result['intro_audio']={'bytes':len(raw),'seconds':len(raw)/176400,'active_10ms_bins':sum(x>50 for x in bins),'total_10ms_bins':len(bins),'peak':max(bins,default=0)}
        command('quit');wait(finder,45);c.pump(1);shot('returned-finder');result['returned_finder_relative']=mode()
        c.pointer(975,55);c.pump(.3);shot('returned-pointer')
        result['pointer_mode_events']=[e for _,e in c.events if e.get('event')=='pointer-mode']
        result['status']='measured' if a.baseline else 'pass'
        if not a.baseline:
            first=[v for v in result['first_intro_samples'] if v['t']<=8]
            result['first_intro_uploads_8s']=first[-1]['tex_uploads']-first[0]['tex_uploads']
            replay=[v for v in samples if v['t']<=6]
            result['replay_uploads_6s']=replay[-1]['uploads']
            assert result['first_intro_uploads_8s']>=140, 'First-launch intro missed its frame cadence'
            assert result['replay_uploads_6s']>=120, 'Replayed intro missed its frame cadence'
            if 'intro_audio' in result:
                assert result['intro_audio']['active_10ms_bins']>=350, 'Intro soundtrack contains too little audio'
            assert result['finder_relative'] is False,result
            assert result['game_relative'] is True,result
            assert result['menu_click'], 'Menu click did not select Single Player'
            assert result['look_changed_fraction']>.04, 'Camera did not turn'
            assert result['returned_finder_relative'] is False, 'Finder did not restore seamless input'
        print(json.dumps({k:v for k,v in result.items() if k not in ('intro_samples','display_change_times','first_intro_samples','first_frame_changes')},indent=2),flush=True)
    except Exception as error:
        result['status']='fail';result['error']=str(error)
        if c.frame is not None:shot('failure')
        raise
    finally:
        (out/'result.json').write_text(json.dumps(result,indent=2))
        if q:
            try:q.cmd('quit')
            except Exception:pass
            q.close()
        try:p.wait(10)
        except subprocess.TimeoutExpired:p.terminate();p.wait(10)
        c.close();awake.terminate()

if __name__=='__main__':main()
