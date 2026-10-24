#!/usr/bin/env python3
"""Exercise the first host resize after launching a 3D application.

This catches readiness being observed only while a resize is pending. Output
contains a disposable sparse disk clone, screenshots and measured real mode /
3D counters. The original independently supplied guest disk is never modified.
"""
import argparse,hashlib,json,os,re,subprocess,sys,time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'ati/tests'), str(ROOT/'display/tests')]
import guest_run as G, cmdclient as C
parser = argparse.ArgumentParser(description="Resize the actual ATI desktop while Quake III renders. Requires an independently supplied SuperDeluxe fixture; no guest content is distributed.")
for name in ('qemu', 'bios', 'disk', 'ndrvloader', 'tools-cd', 'out'):
 parser.add_argument('--'+name, required=True)
args = parser.parse_args()
for name in ('qemu', 'bios', 'disk', 'ndrvloader', 'tools_cd', 'out'):
 setattr(args, name, str(Path(getattr(args, name)).resolve()))
out = Path(args.out)
out.mkdir(parents=True, exist_ok=False)
copy = ['cp', '-c'] if sys.platform == 'darwin' else ['cp', '--reflink=auto', '--sparse=always']
subprocess.run(copy + [args.disk, str(out/'disk.img')], check=True)
os.chdir(out.parent)
c = C.DisplayClient(str(Path(out.name)/'display.sock'))
a = argparse.Namespace(out=out.name, qemu=args.qemu, bios=args.bios,
 disk=str(out/'disk.img'), card='ati', size='1024x768', depth=32, ram=512,
 name='ATI window resize regression', icount=False,
 display_option=['classicmac-display', 'retroboot-display'],
 ndrvloader=args.ndrvloader, tools_cd=args.tools_cd,
 extra=['-device', 'classicmac-display-control,id=hostresize', '-device', 'usb-mouse,id=classicmac-game-mouse'])
argv=G.build_argv(a);argv[argv.index('-display')+1]='classicmac,socket='+str(Path(out.name)/'display.sock')
proc=subprocess.Popen(argv,stdout=(out/'qemu.log').open('wb'),stderr=subprocess.STDOUT);q=None;result={'passed':False,'modes':[], 'qemuSha256':hashlib.sha256(Path(args.qemu).read_bytes()).hexdigest(), 'firmwareSha256':hashlib.sha256(Path(args.ndrvloader).read_bytes()).hexdigest()}
def wait(pred,timeout=90):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  c.pump(.05)
  if pred():return
  if proc.poll() is not None:raise RuntimeError('QEMU exited '+str(proc.returncode))
 raise RuntimeError('Timed out waiting for guest')
def shot(name):c.pump(.3);c.snapshot(str(out/(name+'.png')))
def typing(text):
 special={' ':'spc','.':'dot','-':'minus','_':'shift-minus','\\':'backslash',';':'semicolon','/':'slash'}
 for char in text:c.tap(*special.get(char,char.lower() if char.islower() or char.isdigit() else 'shift-'+char.lower()).split('-'),hold=.035)
def console_open():
 if not c.surface:return False
 width,height=c.surface['width'],c.surface['height']
 for y in range(100,min(height,1000)):
  run=0
  for x in range(width):
   r,g,b=c.pixel(x,y)
   run=run+1 if r==g and r>=60 and b==0 else 0
   if run>=500:return True
 return False
def ensure_console():
 for attempt in range(3):
  if console_open():return
  c.tap('grave_accent');c.pump(.5)
 raise RuntimeError('Console did not open')
def command(text):ensure_console();typing('\\'+text);c.tap('ret');c.pump(.5)
def stats():return {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',q.cmd('qom-get',path='/machine/peripheral/ati0',property='x-accel-stats'))}
def finder():
 if not c.surface or c.surface['width']!=1024:return False
 px=[c.pixel(x,y) for y in range(3,18,2) for x in range(35,350,2)]
 return sum(max(v)<100 for v in px)>=180 and sum(min(v)>170 for v in px)>=len(px)/2
try:
 c.accept(15);q=G.QMP(out.name);wait(finder,200);c.pump(3);shot('finder')
 c.click(975,55);c.pump(.08);c.click(975,55);c.pump(1)
 for text in ('Games','Quake III','MacQuake3'):typing(text);c.tap('meta_l','o');c.pump(1.5)
 wait(lambda:stats().get('draws3d',0)>100,120);c.pump(15);shot('game-menu');ensure_console()
 command('r_fullscreen 0');command('vid_restart');c.pump(5);shot('windowed-console')
 command('map q3dm1');c.pump(12);shot('arena-before')
 for width,height in [(1144,797),(896,656),(1600,1200),(1024,768)]:
  before=stats();c.resize(width,height);wait(lambda:c.surface['width']==width and c.surface['height']==height,40);c.pump(4);after=stats();shot(f'mode-{width}x{height}')
  record={'width':width,'height':height,'draws3d':after.get('draws3d',0)-before.get('draws3d',0),'tex_uploads':after.get('tex_uploads',0)-before.get('tex_uploads',0)};result['modes'].append(record);print(record,flush=True)
  assert record['draws3d']>0,'3D rendering stopped during resize'
 command('quit');wait(finder,60);shot('returned-finder');result['passed']=True
 # The production shutdown chord is sent through QMP so power has its official qcode.
 keys=[{'type':'qcode','data':x} for x in ['ctrl','alt','meta_l','power']]
 q.cmd('send-key',keys=keys,**{'hold-time':500})
 deadline=time.monotonic()+60
 while proc.poll() is None and time.monotonic()<deadline:
  try:c.pump(.1)
  except ConnectionError:break
 proc.wait(20);result['cleanShutdown']=proc.returncode==0
 assert result['cleanShutdown'], 'Engine did not exit cleanly'
except Exception as e:
 result['passed']=False;result['error']=str(e)
 if proc.poll() is None and q:
  try:result['mailbox']=q.cmd('qom-get',path='/machine/peripheral/hostresize',property='x-resize-status')
  except Exception:pass
 if c.frame:shot('failure')
 raise
finally:
 (out/'result.json').write_text(json.dumps(result,indent=2))
 if proc.poll() is None:
  if q:q.cmd('quit')
  proc.wait(10)
 if q:
  try:q.close()
  except BrokenPipeError:pass
 c.close()
