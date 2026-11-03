#!/usr/bin/env python3
"""Verify automatic worst-case stars and persistence of manual overrides."""
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
                await call('Page.enable')
                await call('Emulation.setDeviceMetricsOverride', {'width':1920,'height':1080,'deviceScaleFactor':1,'mobile':False})
                # Make a known failing candidate even when the real round meets the limits.
                await call('Page.addScriptToEvaluateOnNewDocument',{'source':"document.addEventListener('trackinsight:ready',()=>{const a=trackInsight.analysis,s=a.overview;if(!s.worst_ids.length)s.worst_ids=s.suppressed_worst_ids||[];s.suppressed_worst_ids=[];for(const p of a.paths)if(s.worst_ids.includes(p.interval.id)){p.metrics.lateral.p95=.1;p.metrics.lateral.max=.1;}});"})
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
                worst=await js('trackInsight.analysis.overview.worst_ids')
                assert worst,'Use a report with an evaluated worst case'
                assert set((await state())['black'])==set(worst)
                await select(worst[0])
                assert (await state())['blackActive']=='true'
                assert not await js("document.getElementById('caseIssuePanel').hidden")
                await js("document.getElementById('toggleBlackFavorite').click()")
                await reload()
                assert worst[0] not in (await state())['black'],'Reload undid manual cancellation'
                await select(worst[0])
                await js("document.getElementById('toggleFavorite').click()")
                await reload()
                assert worst[0] in (await state())['red'],'Reload undid red classification'
                # Upgrade an existing saved classification without the automatic marker.
                await js("(()=>{const m=trackInsight.analysis.meta,k='trackInsight:stars:v2:'+JSON.stringify([m.input_sha256,m.start_ns,m.end_ns]);localStorage.setItem(k,JSON.stringify({red:trackInsight.analysis.overview.worst_ids,black:[]}));})()")
                await reload()
                assert set((await state())['red'])==set(worst) and not (await state())['black']
                # Exercise ties with an existing manually starred case and no migration marker.
                await js("(()=>{const m=trackInsight.analysis.meta,k='trackInsight:stars:v2:'+JSON.stringify([m.input_sha256,m.start_ns,m.end_ns]);localStorage.setItem(k,JSON.stringify({red:[],black:["+json.dumps(third)+"]}));})()")
                tied=[first,second]
                await call('Page.enable')
                script=await call('Page.addScriptToEvaluateOnNewDocument',{'source':"document.addEventListener('trackinsight:ready',()=>{trackInsight.analysis.overview.worst_ids="+json.dumps(tied)+";for(const p of trackInsight.analysis.paths)if(trackInsight.analysis.overview.worst_ids.includes(p.interval.id)){p.metrics.lateral.p95=.1;p.metrics.lateral.max=.1;}});"})
                await reload()
                assert set((await state())['black'])==set(tied+[third]),(await state(),await js('trackInsight.analysis.overview.worst_ids'),errors)
                await call('Page.removeScriptToEvaluateOnNewDocument',{'identifier':script['identifier']})
                await reload()
                assert await js('JSON.stringify({overview:trackInsight.analysis.overview,metrics:trackInsight.analysis.paths.map(p=>p.metrics)})')==original
                # Thresholds remove only the worst label and automatic black star.
                await js("""(()=>{const a=trackInsight.analysis,id=a.overview.worst_ids[0];
                  window.thresholdCase=id;const m=a.paths.find(p=>p.interval.id===id).metrics.lateral;
                  m.p95=.05;m.max=.055;
                  document.dispatchEvent(new Event('trackinsight:cases'));
                })()""")
                threshold_case=await js('thresholdCase')
                assert await js('trackInsight.reportOverview.worst_ids.length')==0
                assert await js("document.querySelectorAll('#overviewCases .overviewWorst').length")==0
                assert threshold_case not in (await state())['black']
                await select(threshold_case)
                await js("document.getElementById('toggleBlackFavorite').click();document.dispatchEvent(new Event('trackinsight:cases'))")
                assert threshold_case in (await state())['black'],'Manual black star was removed'
                for field,limit in [('p95',.05),('max',.055)]:
                    await js("(()=>{const m=trackInsight.analysis.paths.find(p=>p.interval.id===thresholdCase).metrics.lateral;m.p95=.05;m.max=.055;m["+json.dumps(field)+"]="+str(limit+1e-8)+";document.dispatchEvent(new Event('trackinsight:cases'));})()")
                    assert await js('trackInsight.reportOverview.worst_ids')==[threshold_case]
                assert not errors,errors
                print('PASS: automatic worst stars; cancellation/reload; red override; migration; tied cases; existing stars preserved; unchanged analysis')
                await call('Browser.close')
                chrome.wait(timeout=10)
        finally:
            if chrome.poll() is None:
                chrome.terminate()
                chrome.wait(timeout=10)

if __name__ == '__main__':
    asyncio.run(main())
