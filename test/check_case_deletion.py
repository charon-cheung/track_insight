#!/usr/bin/env python3
"""Verify deletion in both modes, ranking, averages and persistence."""
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
                original=await js('({overview:trackInsight.analysis.overview,paths:trackInsight.analysis.paths.map(p=>({interval:p.interval,metrics:p.metrics}))})')
                paths={p['interval']['id']:p for p in original['paths']}
                case_ids=original['overview']['case_ids']
                limits=original['overview']['worst_case_limits']
                assert len(case_ids)>=3,'Use a report with at least three cases'
                fields=['lateral','path_distance','yaw_error','vx_residual','vy_residual','omega_residual','speed_vector_residual']
                deleted=set()
                async def verify():
                    keep=[k for k in case_ids if k not in deleted]
                    actual=await js("({rows:[...document.querySelectorAll('#overviewCases tr[data-case]')].map(r=>r.dataset.case),best:[...document.querySelectorAll('#overviewCases .overviewBest')].map(r=>r.dataset.case),worst:[...document.querySelectorAll('#overviewCases .overviewWorst')].map(r=>r.dataset.case),means:Object.fromEntries([...document.querySelectorAll('#overviewAverages tr')].map(r=>[r.dataset.metric,[...r.cells].slice(1).map(c=>c.textContent)]))})")
                    assert set(actual['rows'])==set(keep),(actual['rows'],keep)
                    def valid(k,f):
                        m=paths[k]['metrics'].get(f,{})
                        return isinstance(m.get('rmse'),(int,float)) and m['rmse']>=0 and m.get('valid_s',0)>0
                    ranked=[k for k in keep if valid(k,'lateral')]
                    for name,fn in [('best',min),('worst',max)]:
                        expected=[] if not ranked else [k for k in ranked if paths[k]['metrics']['lateral']['rmse']==fn(paths[x]['metrics']['lateral']['rmse'] for x in ranked)]
                        if name=='worst':
                            def passes(k):
                                m=paths[k]['metrics']['lateral']
                                return all(isinstance(m.get(f),(int,float)) and 0<=m[f]<=limit for f,limit in [('p95',limits['p95_m']),('max',limits['max_m'])])
                            expected=[k for k in expected if not passes(k)]
                        assert set(actual[name])==set(expected),(name,actual[name],expected)
                    for f in fields:
                        ms=[paths[k]['metrics'][f] for k in keep if valid(k,f)]
                        mean=sum(m['rmse'] for m in ms)/len(ms) if ms else None
                        weight=sum(m['valid_s'] for m in ms)
                        pooled=(sum(m['rmse']**2*m['valid_s'] for m in ms)/weight)**.5 if weight else None
                        cells=actual['means'][f]
                        for value,text in zip((mean,pooled),cells):
                            if value is None:assert text.startswith('—'),(f,cells)
                            else:assert abs(float(text.split()[0])-value)<=.00005001,(f,cells,value)
                        assert cells[2]==str(len(ms))+' / '+str(len(keep)),(f,cells)
                    for mode in ('reportMode','reviewMode'):
                        await js("document.getElementById('"+mode+"').click()")
                        visible=await js("[...document.getElementById('quickSelection').options,...document.getElementById('favoriteSelection').options,...document.getElementById('blackFavoriteSelection').options].map(o=>o.value)")
                        assert not deleted.intersection(visible),visible
                    assert await js("!!document.getElementById('pidParameters')"),'Overview update removed PID parameters'
                    assert await js('trackInsight.analysis.paths.map(p=>({interval:p.interval,metrics:p.metrics}))')==original['paths'],'Deleted case changed original measurements'
                async def delete(key):
                    await js("(async()=>{document.getElementById('reviewMode').click();const p=trackInsight.analysis.paths.find(p=>p.interval.id==="+json.dumps(key)+");await trackInsight.select([p],p.interval.id);document.getElementById('deleteSelection').click();})()")
                    await asyncio.sleep(.15)
                    deleted.add(key)
                    await verify()
                async def reload():
                    await js('window.pendingDeletionReload=true')
                    await call('Page.reload',{'ignoreCache':True})
                    for _ in range(200):
                        await asyncio.sleep(.05)
                        if await js("!window.pendingDeletionReload&&!!window.trackInsight&&!!document.getElementById('overviewAverages')"):return
                    raise AssertionError('Report did not reload')
                await verify()
                await delete(max(case_ids,key=lambda k:paths[k]['metrics']['lateral'].get('rmse') or 0))
                await delete(next(k for k in original['overview']['best_ids'] if k not in deleted))
                await reload()
                await verify()
                # CSV must agree with the filtered table.
                csv=await js("(async()=>{document.getElementById('reportMode').click();document.getElementById('overviewNavigation').click();const create=URL.createObjectURL;let blob;URL.createObjectURL=b=>{blob=b;return create(b)};document.getElementById('downloadMetrics').click();URL.createObjectURL=create;return await blob.text();})()")
                for k in deleted:assert '\"'+paths[k]['interval']['plan_id']+'\"' not in csv
                # Persisted deletion of every case must produce an empty, non-NaN summary.
                deleted=set(case_ids)
                await js("(()=>{const m=trackInsight.analysis.meta,k='trackInsight:hidden:v1:'+JSON.stringify([m.input_sha256,m.start_ns,m.end_ns]);localStorage.setItem(k,JSON.stringify("+json.dumps(case_ids)+"));})()")
                await reload()
                await verify()
                assert not await js("document.getElementById('overviewAverages').textContent.includes('NaN')")
                await js("document.getElementById('restoreAllHidden').click()")
                deleted.clear()
                await verify()
                # Synthetic invalid/tied metrics are checked against C++ in check_overview_contract.py.
                # A navigation with multiple paths: remove one child, then the whole group.
                await call('Page.enable')
                await call('Page.addScriptToEvaluateOnNewDocument',{'source':"const parse=JSON.parse;JSON.parse=function(...args){const a=parse(...args);if(a?.paths&&a?.tasks&&a?.overview){const ids=new Set(a.overview.case_ids),p=a.paths.filter(p=>ids.has(p.interval.id));if(p.length>1)p[1].interval.navigation_id=p[0].interval.navigation_id;}return a;};"})
                await js('localStorage.clear()')
                await reload()
                original=await js('({overview:trackInsight.analysis.overview,paths:trackInsight.analysis.paths.map(p=>({interval:p.interval,metrics:p.metrics}))})')
                paths={p['interval']['id']:p for p in original['paths']}
                deleted.clear()
                parent=paths[case_ids[0]]['interval']['navigation_id']
                await delete(case_ids[0])
                assert parent not in await js("[...document.getElementById('quickSelection').options].map(o=>o.value)")
                assert case_ids[1] in await js("[...document.getElementById('quickSelection').options].map(o=>o.value)")
                await js("document.getElementById('restoreAllHidden').click()")
                deleted.clear()
                await js("(()=>{const s=document.getElementById('quickSelection');s.value="+json.dumps(parent)+";s.dispatchEvent(new Event('change'));document.getElementById('deleteSelection').click();})()")
                deleted.update(k for k in case_ids if paths[k]['interval']['navigation_id']==parent)
                await verify()
                await reload()
                await verify()
                assert not errors,errors
                print('PASS: delete best/worst; both modes and star pickers; weighted averages; reload; CSV; empty and restore; grouped navigation; original metrics/PID retained')
                await call('Browser.close')
                chrome.wait(timeout=10)
        finally:
            if chrome.poll() is None:
                chrome.terminate()
                chrome.wait(timeout=10)

if __name__ == '__main__':
    asyncio.run(main())
