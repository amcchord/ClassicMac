// Integer-only port of PlatinumKit/Core/Chrome.swift. The Mac and desktop
// renderers share measured tables, fonts and independent framebuffer fixtures.
import { measured as M, fonts } from './platinum-assets.mjs';
export const face = 0xdddddd;
export function color(ch) {
  if (ch === '.' || ch === ' ') return null;
  if (ch === 'K') return 0;
  if (ch === 'W') return 0xffffff;
  if (/^[0-9a-f]$/.test(ch)) return parseInt(ch, 16) * 0x111111;
  return { A: 0xeeeeee, B: 0xccccff, C: 0x9999ff, D: 0x6666cc, E: 0x333399, F: 0x000088, G: 0x000055, H: 0 }[ch];
}
export class Pixels {
  constructor(w, h, background = face) {
    this.w = Math.max(1, Math.round(w)); this.h = Math.max(1, Math.round(h));
    this.data = new Uint8ClampedArray(this.w * this.h * 4);
    if (background !== null) this.fill(0, 0, this.w, this.h, background);
  }
  pixel(x, y, c) {
    if (c == null || x < 0 || y < 0 || x >= this.w || y >= this.h) return;
    const i = (y * this.w + x) * 4;
    this.data[i] = (c >> 16) & 255; this.data[i + 1] = (c >> 8) & 255; this.data[i + 2] = c & 255; this.data[i + 3] = 255;
  }
  fill(x, y, w, h, c) { for (let j = Math.max(0,y); j < Math.min(this.h,y+h); j++) for (let i = Math.max(0,x); i < Math.min(this.w,x+w); i++) this.pixel(i, j, c); }
  line(x, y, w, c) { this.fill(x, y, w, 1, c); }
  vertical(x, y, h, c) { this.fill(x, y, 1, h, c); }
  frame(x, y, w, h, c) { this.line(x,y,w,c); this.line(x,y+h-1,w,c); this.vertical(x,y,h,c); this.vertical(x+w-1,y,h,c); }
  sprite(rows, x = 0, y = 0) { rows.forEach((row,j) => [...row].forEach((ch,i) => this.pixel(x+i,y+j,color(ch)))); }
  stretch(rows, repeatAt, x = 0, y = 0, w = this.w, h = this.h) {
    const bottom = rows.length - repeatAt - 1;
    for (let j=0;j<h;j++) {
      const row=rows[j<repeatAt?j:j>=h-bottom?rows.length-(h-j):repeatAt];
      this.sprite([(row[0]+row[1].repeat(Math.max(0,w-row[0].length-row[2].length))+row[2]).slice(0,w)],x,y+j);
    }
  }
}
export function button(p, state = 'normal', isDefault = false) {
  const d = isDefault ? 3 : 0;
  if (isDefault) p.stretch(state==='disabled'?M.DEFAULT_DISABLED:M.BUTTON_DEFAULT,state==='disabled'?13:7);
  p.stretch(state==='pressed'?M.BUTTON_PRESSED:state==='disabled'?M.BUTTON_DISABLED:M.BUTTON,state==='disabled'?2:4,d,d,p.w-d*2,p.h-d*2);
}
export function toggle(p, radio, on, state = 'normal', mixed = false) {
  const suffix = state==='normal'?'':'-'+state;
  if (mixed) p.sprite(state==='pressed'?M.CHECKBOX_MIXED_PRESSED:state==='disabled'?M.CHECKBOX_MIXED_DISABLED:M.CHECKBOX_MIXED);
  else if (radio && state==='disabled') p.sprite(on?M.RADIO_ON_DISABLED:M.RADIO_OFF_DISABLED);
  else p.sprite((radio?M.RADIO:M.CHECKBOX)[(on?'on':'off')+suffix]);
}
export function popup(p, state = 'normal') {
  if (state==='pressed') { p.stretch(M.POPUP_PRESSED,10); return; }
  const prefix=state==='disabled'?'POPUP_DIS_':'POPUP_';
  p.stretch(M[prefix+'LEFT'].map((row,i)=>[row,M[prefix+'FILL'][i],M[prefix+'RIGHT'][i]]),10);
}
export function titleBox(p, kind, pressed = false, x = 0, y = 0) {
  if (pressed) { p.sprite(M['WINDOW_'+kind.toUpperCase()+'_PRESSED'],x,y); return; }
  const c=color;
  p.fill(x,y,13,13,c('c'));p.line(x,y,12,c('8'));p.vertical(x,y,12,c('8'));
  p.line(x+1,y+12,12,c('W'));p.vertical(x+12,y+1,12,c('W'));p.frame(x+1,y+1,11,11,c('2'));
  p.line(x+2,y+2,8,c('c'));p.vertical(x+2,y+2,8,c('c'));p.line(x+3,y+10,8,c('8'));p.vertical(x+10,y+3,8,c('8'));p.pixel(x+2,y+2,c('W'));
  const ramp='99aabbccddeeW';for(let j=0;j<7;j++)for(let i=0;i<7;i++)p.pixel(x+3+i,y+3+j,c(ramp[i+j]));
  if(kind==='zoom'){p.vertical(x+7,y+2,6,c('2'));p.line(x+2,y+7,6,c('2'));}
  if(kind==='collapse'){p.line(x+1,y+5,11,c('2'));p.line(x+1,y+7,11,c('2'));}
}
export function windowFrame(p, { active=true, collapsed=false, titleWidth=70, boxes=true, growable=true, modal=false } = {}) {
  const w=p.w-1,h=p.h-1,x1=w-1,y1=h-1;
  p.frame(0,0,w,h,active?0:0x555555);p.vertical(x1+1,2,h-1,active?0:0x555555);p.line(2,y1+1,w-1,active?0:0x555555);
  p.fill(1,1,w-2,h-2,active?0xcccccc:face);
  if(active){p.line(1,1,w-2,0xffffff);p.vertical(1,1,h-2,0xffffff);p.line(1,y1-1,w-2,0x999999);p.vertical(x1-1,1,h-2,0x999999);p.pixel(x1-1,1,0xcccccc);p.pixel(1,y1-1,0xcccccc);}
  if(!collapsed){
    const x=6,y=modal?6:22,cw=w-12,ch=h-y-6;
    if(active){p.line(x-2,y-2,cw+3,0x999999);p.vertical(x-2,y-2,ch+3,0x999999);p.line(x-1,y+ch+1,cw+3,0xffffff);p.vertical(x+cw+1,y-1,ch+3,0xffffff);}
    p.frame(x-1,y-1,cw+2,ch+2,active?0:0x555555);p.fill(x,y,cw,ch,face);
    if(growable)p.sprite(active?M.WINDOW_GROW:M.WINDOW_GROW_INACTIVE,x+cw-15,y+ch-15);
  }
  if(modal)return;
  if(active){
    const tx=Math.floor((w-titleWidth)/2),left=boxes?22:9,right=boxes?w-37:x1-8;
    const stripes=(a,b)=>{if(b-a<2)return;for(let i=0;i<6;i++){p.line(a-1,4+2*i,b-a,0xffffff);p.line(a,5+2*i,b-a,0x777777);}};
    if(titleWidth){stripes(left,tx-3);stripes(tx+titleWidth+4,right);}else stripes(left,right);
    if(boxes){titleBox(p,'close',false,4,4);titleBox(p,'zoom',false,w-33,4);titleBox(p,'collapse',false,w-17,4);}
  }
}
export function group(p, labelWidth=0) {
  const top=labelWidth?7:0;p.stretch(M.GROUP,2,0,top,p.w,p.h-top);if(labelWidth)p.fill(8,top,labelWidth+6,2,face);
}
export function sunken(p) {
  p.line(0,0,p.w-1,0x888888);p.vertical(0,0,p.h-1,0x888888);p.line(1,p.h-1,p.w-1,0xffffff);p.vertical(p.w-1,1,p.h-1,0xffffff);p.frame(1,1,p.w-2,p.h-2,0);
}
export function focus(p) { p.stretch(M.FIELD_FOCUS,3);p.fill(2,2,p.w-4,p.h-4,face); }
export function bevel(p,state='normal') {p.stretch(M[state==='pressed'?'SMALL_BEVEL_PRESSED':state==='disabled'?'SMALL_BEVEL_DISABLED':'SMALL_BEVEL_NORMAL'],2);}
export function progress(p,value=0,phase=0) {
  // Public rectangle includes the one-pixel sunken edge.
  sunken(p);const x=2,y=2,w=p.w-4,h=p.h-4;
  if(value==null){for(let j=0;j<h;j++)for(let i=0;i<w;i++)p.pixel(x+i,y+j,((i-j+phase)%16+16)%16<8?0x6666cc:0xccccff);return;}
  p.line(0,0,p.w-1,color('a'));p.vertical(0,0,p.h-1,color('a'));p.pixel(0,p.h-1,face);p.pixel(p.w-1,0,face);
  const track='8bbbbbbbbd',trackEnd='bddddddddd',second='DCBAAABCDE',body='EDCBABCDEG';
  const end=['EEEK5','DDGK5','CEGK5','BEGK5','BEGK5','BEGK5','CEGK5','DEGK5','EEGK5','GGGK5'];
  const filled=value<=0?0:Math.max(5,Math.min(w,Math.round(w*Math.min(1,value))));
  for(let j=0;j<h;j++){const row=Math.min(9,j);p.line(x,y+j,w,color(track[row]));p.pixel(x+w-1,y+j,color(trackEnd[row]));p.pixel(x,y+j,color('8'));if(filled){p.pixel(x,y+j,color('D'));p.pixel(x+1,y+j,color(second[row]));p.line(x+2,y+j,Math.max(0,filled-2),color(body[row]));if(filled<w)p.sprite([end[row]+'8'],x+filled-3,y+j);}}
}
export function textWidth(text,font='system') { return [...text].reduce((n,ch)=>n+(fonts[font][ch.codePointAt(0)]||fonts[font][65533]||fonts[font][63])[0],0); }
export function text(p,str,x=0,baseline=12,c=0,font='system') {
  for(const ch of str){
    const [advance,w,h,dx,dy,rows]=fonts[font][ch.codePointAt(0)]||fonts[font][65533]||fonts[font][63];
    for(let j=0;j<h;j++){const row=BigInt('0x'+rows[j]);const bits=rows[j].length*4;for(let i=0;i<w;i++)if(row&(1n<<BigInt(bits-i-1)))p.pixel(x+dx+i,baseline-dy-h+j,c);}
    x+=advance;
  }
}
