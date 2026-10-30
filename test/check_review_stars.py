#!/usr/bin/env python3
"""Verify red/black review stars in an isolated browser profile using a rendered report."""
import argparse
import base64
import asyncio
import json
import subprocess
import tempfile
from pathlib import Path
from urllib.request import urlopen
import websockets


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--report', required=True)
    parser.add_argument('--screenshot')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='track_stars_', ignore_cleanup_errors=True) as profile:
        chrome = subprocess.Popen(['/usr/bin/google-chrome', '--headless', '--disable-gpu',
            '--disable-background-networking', '--no-first-run', '--remote-debugging-port=0',
            '--user-data-dir=' + profile, 'about:blank'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            portfile = Path(profile) / 'DevToolsActivePort'
            for _ in range(200):
                if portfile.exists(): break
                await asyncio.sleep(.05)
            port = portfile.read_text().splitlines()[0]
            target = next(t for t in json.load(urlopen('http://127.0.0.1:' + port + '/json')) if t['type'] == 'page')
            async with websockets.connect(target['webSocketDebuggerUrl'], max_size=64*1024*1024) as ws:
                sequence = 0
                errors = []
                async def call(method, params=None):
                    nonlocal sequence
                    sequence += 1
                    await ws.send(json.dumps({'id': sequence, 'method': method, 'params': params or {}}))
                    while True:
                        response = json.loads(await ws.recv())
                        if response.get('method') == 'Runtime.exceptionThrown': errors.append(response['params'])
                        if response.get('id') == sequence:
                            assert 'error' not in response, response
                            return response.get('result', {})
                async def js(expression):
                    result = await call('Runtime.evaluate', {'expression': expression, 'returnByValue': True, 'awaitPromise': True})
                    assert 'exceptionDetails' not in result, result
                    return result.get('result', {}).get('value')
                await call('Runtime.enable')
                await call('Emulation.setDeviceMetricsOverride', {'width':1920,'height':1080,'deviceScaleFactor':1,'mobile':False})
                await call('Page.navigate', {'url':Path(args.report).resolve().as_uri()})
                for _ in range(200):
                    if await js('!!window.trackInsight'):break
                    await asyncio.sleep(.05)
                keys=await js("[...document.getElementById('quickSelection').options].map(o=>o.value).filter(key=>trackInsight.analysis.paths.some(p=>p.interval.id===key))")
                assert len(keys)>=3,'Use a report with at least three selectable cases'
                first,second,third=keys[:3]
                original=await js('JSON.stringify({overview:trackInsight.analysis.overview,metrics:trackInsight.analysis.paths.map(p=>p.metrics)})')
                async def select(key):
                    await js("(()=>{const p=trackInsight.analysis.paths.find(p=>p.interval.id==="+json.dumps(key)+");return trackInsight.select([p],p.interval.id);})()")
                async def reload():
                    await js('window.pendingStarReload=true')
                    await call('Page.reload',{'ignoreCache':True})
                    for _ in range(200):
                        await asyncio.sleep(.05)
                        if await js("!window.pendingStarReload&&!!window.trackInsight&&!!document.getElementById('reviewRecordsDialog')"):return
                    raise AssertionError('Report did not reload')
                async def state():
                    return await js("(()=>{const options=id=>[...document.getElementById(id).options].map(o=>o.value).filter(Boolean);return {red:options('favoriteSelection'),black:options('blackFavoriteSelection'),redActive:document.getElementById('toggleFavorite').getAttribute('aria-pressed'),blackActive:document.getElementById('toggleBlackFavorite').getAttribute('aria-pressed'),selected:trackInsight.selected}})()")
                async def imported(record):
                    await js("(async()=>{const input=document.getElementById('reviewRecordsFile'),data=new DataTransfer();data.items.add(new File(["+json.dumps(json.dumps(record))+"],'records.json',{type:'application/json'}));input.files=data.files;await input.onchange();})()")
                    return await js("document.getElementById('reviewRecordsStatus').textContent")
                # An old orange favorite and a hidden case survive migration.
                await js("(()=>{const m=trackInsight.analysis.meta,i=JSON.stringify([m.input_sha256,m.start_ns,m.end_ns]);localStorage.setItem('trackInsight:favorites:v1:'+i,JSON.stringify(["+json.dumps(first)+"]));localStorage.setItem('trackInsight:hidden:v1:'+i,JSON.stringify(["+json.dumps(third)+"]));})()")
                await reload()
                current=await state()
                assert current['black']==[first] and current['red']==[],current
                assert third not in await js("[...document.getElementById('quickSelection').options].map(o=>o.value)")
                await select(first)
                await js("document.getElementById('toggleFavorite').click()")
                current=await state()
                assert current['red']==[first] and current['black']==[] and current['blackActive']=='false',current
                await select(second)
                for expected in [[second],[],[second]]:
                    await js("document.getElementById('toggleBlackFavorite').click()")
                    assert (await state())['black']==expected
                for picker,key in [('favoriteSelection',first),('blackFavoriteSelection',second)]:
                    await js("(()=>{const s=document.getElementById('"+picker+"');s.value="+json.dumps(key)+";s.dispatchEvent(new Event('change'));})()")
                    await asyncio.sleep(.15)
                    assert (await state())['selected']==key
                await reload()
                assert (await state())['red']==[first] and (await state())['black']==[second]
                # Capture the actual export Blob, without writing into the user's Downloads folder.
                record=await js("(async()=>{const create=URL.createObjectURL,click=HTMLAnchorElement.prototype.click;let blob;URL.createObjectURL=b=>{blob=b;return create(b);};HTMLAnchorElement.prototype.click=()=>{};try{document.getElementById('exportReviewRecords').click();return JSON.parse(await blob.text());}finally{URL.createObjectURL=create;HTMLAnchorElement.prototype.click=click;}})()")
                assert record['schema_version']==2 and record['favorites']=={'red':[first],'black':[second]},record
                assert record['hidden']==[third]
                baseline=await js('JSON.stringify(localStorage)')
                for bad in [dict(record,source={}),dict(record,favorites={'red':[first],'black':[first]}),dict(record,hidden=['unknown']),dict(record,favorites={'red':[first],'black':'bad'})]:
                    assert '导入失败' in await imported(bad)
                    assert await js('JSON.stringify(localStorage)')==baseline,'Rejected import changed saved records'
                # Imported classifications win a color conflict; version 1 remains readable.
                incoming=dict(record,favorites={'red':[second],'black':[first]},hidden=[])
                assert '导入完成' in await imported(incoming)
                current=await state()
                assert current['red']==[second] and current['black']==[first],current
                legacy=dict(record,schema_version=1,favorites=[second],hidden=[])
                assert '导入完成' in await imported(legacy)
                assert (await state())['red']==[] and (await state())['black']==[first,second]
                assert '导入完成' in await imported(record)
                await select(second)
                await js("document.getElementById('deleteSelection').click()")
                await asyncio.sleep(.15)
                assert (await state())['black']==[]
                await js("document.getElementById('reportMode').click()")
                await asyncio.sleep(.15)
                assert second in (await state())['black'],'Review hiding leaked into report mode'
                await js("document.getElementById('overviewNavigation').click()")
                assert await js("['favoritePicker','blackFavoritePicker'].every(id=>getComputedStyle(document.getElementById(id)).display==='none')")
                await js("document.getElementById('reviewMode').click();document.getElementById('restoreAllHidden').click()")
                await select(first)
                assert await js('JSON.stringify({overview:trackInsight.analysis.overview,metrics:trackInsight.analysis.paths.map(p=>p.metrics)})')==original,'Review classification changed analysis'
                labels=await js("['blackFavoriteSelection','favoriteSelection'].map(id=>document.getElementById(id).options[0].textContent)")
                assert labels==['选择黑色星标','选择红色星标'],labels
                geometry=await js("(()=>{const rect=s=>document.querySelector(s).getBoundingClientRect(),c=rect('.viewCenter'),t=rect('.viewToolbar'),b=rect('#blackFavoritePicker'),d=rect('#deleteSelection'),r=rect('#favoritePicker'),m=rect('.modeSwitch'),l=rect('#toggleBlackFavorite'),e=rect('#titleEndpoints'),s=rect('#toggleFavorite');return {center:c.x+c.width/2,expected:t.x+t.width/2,blackGap:b.x-d.right,redGap:m.x-r.right,mirror:(b.x+b.width/2)+(r.x+r.width/2)-(2*t.x+t.width),left:l.right<=e.x,right:s.x>=e.right}})()")
                assert abs(geometry['center']-geometry['expected'])<1 and geometry['blackGap']>=24 and geometry['redGap']>=30 and abs(geometry['mirror'])<1 and geometry['left'] and geometry['right'],geometry
                await asyncio.sleep(.2)
                if args.screenshot:
                    shot=await call('Page.captureScreenshot',{'format':'png'})
                    Path(args.screenshot).write_bytes(base64.b64decode(shot['data']))
                for mode,width in [('reviewMode',2560),('reviewMode',1366),('reportMode',1920)]:
                    await call('Emulation.setDeviceMetricsOverride',{'width':width,'height':1080,'deviceScaleFactor':1,'mobile':False})
                    await js("document.getElementById('"+mode+"').click()")
                    await select(first)
                    await asyncio.sleep(.3)
                    error=await js("(()=>{const t=document.querySelector('.viewToolbar').getBoundingClientRect(),b=document.getElementById('blackFavoritePicker').getBoundingClientRect(),r=document.getElementById('favoritePicker').getBoundingClientRect();return (b.x+b.width/2)+(r.x+r.width/2)-(2*t.x+t.width);})()")
                    assert abs(error)<1,(mode,width,error)
                # The report sidebar leaves less horizontal room; groups must not overlap.
                for mode in ['reviewMode','reportMode']:
                    await call('Emulation.setDeviceMetricsOverride',{'width':1366,'height':900,'deviceScaleFactor':1,'mobile':False})
                    await js("document.getElementById('"+mode+"').click()")
                    await select(first)
                    await asyncio.sleep(.2)
                    assert await js("(()=>{const boxes=['.viewNavigation','.viewCenter','.viewActions'].map(s=>document.querySelector(s).getBoundingClientRect());return boxes.every((a,i)=>boxes.slice(i+1).every(b=>a.right<=b.left+1||b.right<=a.left+1||a.bottom<=b.top+1||b.bottom<=a.top+1));})()"),'Toolbar groups overlap'
                assert await js("document.getElementById('failureBanner').hidden")
                assert not errors,errors
                print('PASS: legacy migration; red/black exclusivity and cancellation; both pickers; reload; v1/v2 import/export; rejected imports atomic; hide/restore; report isolation; centered toolbar; unchanged analysis')
                await call('Browser.close')
                chrome.wait(timeout=10)
        finally:
            if chrome.poll() is None:
                chrome.terminate()
                chrome.wait(timeout=10)

asyncio.run(main())
