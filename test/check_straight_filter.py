#!/usr/bin/env python3
"""Verify straight-path exclusion in both modes, overview and persisted star pickers."""
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
    parser.add_argument('--expected-count', type=int, required=True)
    parser.add_argument('--excluded-plans', nargs='*', default=[])
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
                a=await js("({paths:trackInsight.analysis.paths.map(p=>({id:p.interval.id,plan:p.interval.plan_id})),overview:trackInsight.analysis.overview,meta:trackInsight.analysis.meta})")
                assert len(a['paths'])==args.expected_count, a['paths']
                assert set(a['overview']['excluded_straight_plan_ids'])==set(args.excluded_plans),a['overview']
                excluded=set(a['overview']['excluded_straight_case_ids'])
                assert not excluded.intersection(p['id'] for p in a['paths'])
                identity=json.dumps([a['meta'][k] for k in ['input_sha256','start_ns','end_ns']],separators=(',',':'),ensure_ascii=False)
                saved={'red':list(excluded)+([a['paths'][0]['id']] if a['paths'] else []),'black':[]}
                await js('localStorage.setItem('+json.dumps('trackInsight:stars:v2:'+identity)+','+json.dumps(json.dumps(saved))+')')
                await js('window.reloadingStraightTest=true')
                await call('Page.reload',{'ignoreCache':True})
                for _ in range(200):
                    await asyncio.sleep(.05)
                    if await js("!window.reloadingStraightTest&&!!window.trackInsight&&!!document.getElementById('reviewRecordsDialog')"):break
                for mode in ('reportMode','reviewMode'):
                    await js("document.getElementById("+json.dumps(mode)+").click()")
                    await asyncio.sleep(.3)
                    picks=await js("['quickSelection','favoriteSelection','blackFavoriteSelection'].map(id=>[...document.getElementById(id).options].map(o=>o.value))")
                    for options in picks:assert not excluded.intersection(options),options
                    assert not set(args.excluded_plans).intersection(await js("[...document.getElementById('quickSelection').options].map(o=>o.textContent)"))
                    if not a['paths']:
                        assert not picks[0]
                        assert await js("document.getElementById('quickSelection').disabled")
                        assert await js("document.getElementById('velocity').data.length===0&&document.getElementById('errors').data.length===0")
                    else:
                        assert a['paths'][0]['id'] in picks[1],picks
                await js("document.getElementById('reportMode').click();document.getElementById('overviewNavigation').click()")
                rows=await js("[...document.querySelectorAll('#overviewCases .caseLink')].map(b=>b.dataset.case)")
                assert rows==a['overview']['case_ids'],(rows,a['overview']['case_ids'])
                for key in ('best_ids','worst_ids'):assert not excluded.intersection(a['overview'][key])
                assert await js("document.getElementById('reportOverview').textContent.includes('最大偏离 ≤ 2 cm')")
                assert not await js("!document.getElementById('failureBanner').hidden"),await js("document.getElementById('failureBanner').textContent")
                assert await js("trackInsight.analysis.tasks.filter(t=>t.path_filter_applied).every(t=>![...document.querySelectorAll('#navigation button')].some(b=>b.dataset.key===t.id)&&!Object.keys(trackInsight.analysis.navigation_metrics).some(k=>k.startsWith(t.id+'/')))")
                assert not errors,errors
                print('PASS: both modes, table, saved star lists and empty-state; retained',len(a['paths']),'cases')
        finally:
            chrome.terminate()
            try:chrome.wait(timeout=10)
            except subprocess.TimeoutExpired:chrome.kill();chrome.wait()

if __name__=='__main__':
    asyncio.run(main())
