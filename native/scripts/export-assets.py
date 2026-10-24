#!/usr/bin/env python3
"""Deterministically export Mac measurement tables and BDF glyphs for C++."""
import argparse,json,re
from pathlib import Path
root=Path(__file__).resolve().parents[2]
tables={}
for filename in ['MeasuredChrome.swift','MeasuredWindowChrome.swift']:
 source=(root/'app/Sources/PlatinumKit/Core'/filename).read_text()
 for match in re.finditer(r'static let (\w+)(?:[^=\n]*)= \[',source):
  start=match.end()-1;depth=1;end=start+1
  while depth:
   depth+=(source[end]=='[')-(source[end]==']');end+=1
  literal=source[start:end]
  if re.search(r'"[\w-]+":',literal):value=json.loads('{'+literal[1:-1].rstrip().rstrip(',')+'}')
  else:value=json.loads(re.sub(r',\s*]',']',literal.replace('(','[').replace(')',']')))
  tables[match[1]]=value
source=(root/'app/Sources/PlatinumKit/Core/Chrome.swift').read_text()
for key,name in [('SCROLL_UP','scrollUp'),('SCROLL_THUMB','thumb')]:
 match=re.search(r'static let '+name+r' = \[(.*?)\n    \]',source,re.S);tables[key]=re.findall(r'"([^"]*)"',match[1])
fonts={}
for key,name in [('system','System-12'),('regular','Regular-12'),('small','Small-10')]:
 glyphs={}
 for block in (root/'platinum/fonts'/f'RetroBoot-{name}.bdf').read_text().split('STARTCHAR ')[1:]:
  code=int(re.search(r'ENCODING (\d+)',block)[1]);advance=int(re.search(r'DWIDTH (\d+)',block)[1]);box=list(map(int,re.search(r'BBX (\d+) (\d+) (-?\d+) (-?\d+)',block).groups()));rows=block.split('BITMAP\n')[1].split('ENDCHAR')[0].split();glyphs[code]=[advance,*box,rows]
 fonts[key]=glyphs
result=json.dumps({'measured':tables,'fonts':fonts},separators=(',',':'))+'\n';p=root/'native/resources/platinum.json'
parser=argparse.ArgumentParser();parser.add_argument('--check',action='store_true');args=parser.parse_args()
if args.check:
 if p.read_text()!=result:raise SystemExit('Native assets are stale; run native/scripts/export-assets.py')
else:p.write_text(result)
