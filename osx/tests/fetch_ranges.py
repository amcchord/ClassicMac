import concurrent.futures
import pathlib, shutil, sys, time, urllib.request
target=pathlib.Path(sys.argv[1])
target.parent.mkdir(parents=True,exist_ok=True)
size=1929055939
url=('https://ia801909.us.archive.org/18/items/mac-osx-tiger-10.4-ppc-installed-qcow2-image/'
     'osx-tiger_10.4.11_installed.qcow2.7z')
chunk=64*1024*1024
def download(offset):
    end=min(offset+chunk,size)-1
    path=target.with_suffix(f'.part-{offset}')
    expected=end-offset+1
    for attempt in range(3):
        try:
            request=urllib.request.Request(url,headers={'Range':f'bytes={offset}-{end}'})
            with urllib.request.urlopen(request,timeout=45) as src, path.open('wb') as out:
                if src.getcode()!=206 or src.headers['Content-Range']!=f'bytes {offset}-{end}/{size}':
                    raise OSError('Unexpected archive response range')
                shutil.copyfileobj(src,out,1024*1024)
            if path.stat().st_size!=expected: raise OSError('Truncated archive range')
            print(f'Tiger fixture verified bytes {offset}-{end}',flush=True)
            return path
        except OSError:
            if attempt==2: raise
            time.sleep(1+attempt)
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool, target.open('wb') as out:
    for path in pool.map(download,range(0,size,chunk)):
        with path.open('rb') as src: shutil.copyfileobj(src,out,1024*1024)
        path.unlink()
