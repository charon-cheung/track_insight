#!/usr/bin/env python3
"""Archive CLI and file:// browser acceptance using an existing analysis and dedicated Chrome profile."""
import argparse
import asyncio
import base64
import json
from pathlib import Path
import subprocess
import tempfile
from urllib.request import urlopen
import websockets

async def browser(profile,home,screenshot):
    port=(profile/'DevToolsActivePort').read_text().splitlines()[0]
    target=next(t for t in json.load(urlopen('http://127.0.0.1:'+port+'/json')) if t['type']=='page')
    sequence=0;errors=[];external=[]
    async with websockets.connect(target['webSocketDebuggerUrl'],max_size=64*1024*1024) as ws:
        async def call(method,params=None):
            nonlocal sequence
            sequence+=1;identifier=sequence
            await ws.send(json.dumps({'id':identifier,'method':method,'params':params or {}}))
            while True:
                item=json.loads(await ws.recv())
                if item.get('method')=='Runtime.exceptionThrown':errors.append(item['params'])
                if item.get('method')=='Network.requestWillBeSent':
                    url=item['params']['request']['url']
                    if url.startswith(('http:','https:')):external.append(url)
                if item.get('id')==identifier:
                    assert 'error' not in item,item
                    return item.get('result',{})
        async def evaluate(expression):
            result=await call('Runtime.evaluate',{'expression':expression,'returnByValue':True,'awaitPromise':True})
            assert 'exceptionDetails' not in result,result
            return result.get('result',{}).get('value')
        async def ready(expression):
            for _ in range(100):
                if await evaluate(expression):return
                await asyncio.sleep(.1)
            raise AssertionError('page not ready: '+expression)
        await call('Page.enable');await call('Runtime.enable');await call('Network.enable')
        await call('Page.navigate',{'url':home.as_uri()})
        await ready("document.getElementById('rounds')?.children.length===2")
        assert await evaluate("document.getElementById('error').hidden")
        assert await evaluate("document.getElementById('roundCount').textContent")=='2'
        catalog=json.loads((home.parent/'runs.json').read_text())
        latest=catalog['runs'][0]
        actual=await evaluate("[...document.querySelectorAll('#rounds tr')].map(r=>({id:r.dataset.run,name:r.cells[0].querySelector('a').textContent,mean:r.cells[3].textContent,coverage:r.cells[5].firstChild.textContent}))")
        assert [r['id'] for r in actual]==[r['id'] for r in catalog['runs']],actual
        assert actual[0]['name']==latest['name'],actual
        mean=latest['overview']['averages']['lateral']['case_mean_rmse']
        assert actual[0]['mean']==('—' if mean is None else f'{mean:.3f}'),actual
        for width,height in [(1366,768),(1920,1080),(600,800)]:
            await call('Emulation.setDeviceMetricsOverride',{'width':width,'height':height,'deviceScaleFactor':1,'mobile':False})
            assert await evaluate('document.documentElement.scrollWidth')<=width+1
        await call('Emulation.setDeviceMetricsOverride',{'width':1366,'height':768,'deviceScaleFactor':1,'mobile':False})
        shot=await call('Page.captureScreenshot',{'format':'png'})
        screenshot.write_bytes(base64.b64decode(shot['data']))
        # Selecting a round actually navigates to its standalone report, then back to the archive.
        for row in catalog['runs']:
            await evaluate("document.getElementById('roundSelection').value="+json.dumps(row['id'])+";document.getElementById('roundSelection').dispatchEvent(new Event('change'))")
            await ready("!!window.trackInsight&&!!document.getElementById('archiveHome')")
            assert await evaluate('trackInsight.analysis.meta.input_sha256')==row['source_sha256']
            assert await evaluate("document.body.classList.contains('review-mode')")
            assert await evaluate("document.getElementById('archiveHome').href")==home.as_uri()
            assert await evaluate("document.getElementById('archiveHome').title")==row['name']
            assert await evaluate("typeof window.injected")=='undefined'
            await evaluate("document.getElementById('archiveHome').click()")
            await ready("document.getElementById('rounds')?.children.length===2")
        assert not errors,errors
        assert not external,external
        print('Browser passed: two rounds, statistics, literal text, responsive layout, navigation/return, offline.')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--executable',required=True)
    parser.add_argument('--analysis',required=True,type=Path)
    parser.add_argument('--work',required=True,type=Path)
    parser.add_argument('--profile',type=Path)
    args=parser.parse_args();args.work.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='archive_acceptance_',dir=args.work) as temp:
        root=Path(temp);archive=root/'rounds';source=args.analysis.resolve()
        def run(*options,code=0):
            result=subprocess.run([args.executable,*map(str,options)],text=True,capture_output=True,timeout=120)
            assert result.returncode==code,(result.returncode,result.stdout,result.stderr)
            return result
        def render(source,*options,code=0):
            return run('render','--analysis',source,'--archive',archive,*options,code=code)
        render(source,'--name','第一轮','--note','保留备注')
        original=(archive/'runs.json').read_text()
        render(source,code=1);assert (archive/'runs.json').read_text()==original
        run('render','--analysis',source,'--archive',archive,'--output',root/'wrong',code=1)
        run('render','--analysis',source,'--output',root/'wrong','--name','bad',code=1)
        assert not (root/'wrong').exists()
        a=json.loads(source.read_text());a['meta']['input_sha256']='b'*64
        # Deliberately synthetic second identity; the actual report data is never changed on disk.
        second=root/'second.json';second.write_text(json.dumps(a))
        unsafe='测试 __TRACK_DATA__ __TRACK_SCRIPT__ <script>window.injected=1</script> & "轮次"'
        render(second,'--name',unsafe,'--note','<备注>')
        catalog=json.loads((archive/'runs.json').read_text());assert len(catalog['runs'])==2
        other=next(r for r in catalog['runs'] if r['source_sha256']=='b'*64)
        other_report=(archive/other['report']).read_bytes()
        render(source,'--replace')
        c=json.loads((archive/'runs.json').read_text());assert len(c['runs'])==2
        first=next(r for r in c['runs'] if r['source_sha256']!='b'*64)
        assert first['name']=='第一轮' and first['note']=='保留备注'
        assert (archive/other['report']).read_bytes()==other_report
        if args.profile:asyncio.run(browser(args.profile.resolve(),(archive/'index.html').resolve(),args.work/'archive_acceptance.png'))
        standalone=root/'standalone'
        run('render','--analysis',source,'--output',standalone)
        assert 'id="archiveHome"' not in (standalone/'report.html').read_text()
        assert json.loads((standalone/'metrics.json').read_text())==json.loads((archive/first['report']).with_name('metrics.json').read_text())
        print('CLI passed: append, duplicate rejection, isolated replace, labels, invalid options, legacy output and equal metrics.')

if __name__=='__main__':main()
