#!/usr/bin/env python3
"""Exercise the archive comparison offline in an isolated Chrome profile.

Default: synthetic edge cases. --catalog runs.json checks a real catalog without
changing the archive. --output DIR retains the preview and its screenshot.
"""
import argparse
import asyncio
import base64
import json
import subprocess
import tempfile
from pathlib import Path
from urllib.request import urlopen
import websockets

ROOT = Path(__file__).resolve().parents[1]


def fixture():
    runs = []
    for i in range(1, 25):
        stamp = str(1793000000000000000 + i * 1000000000)
        identity = 'run_' + stamp + '_' + format(i, '012x')
        runs.append(dict(id=identity, name='R'+str(i), start_ns=stamp,
            start_label='2026-10-26 00:00:'+str(i).zfill(2), end_label='2026-10-26 00:01:00',
            report='runs/'+identity+'/report.html', paths=2, tasks=2, worst=[], note='',
            status='complete', metric_version='v1', analysis_quality='inferred',
            overview=dict(case_ids=['E1', 'E2'], ranked_cases=2, averages=dict(lateral=dict(
                case_mean_rmse=i/1000, pooled_rmse=i/500, case_count=2))),
            pid_parameters=dict(schema_version=2, source_kind='navigation_log', status='available', values={
                'PidTrackingController.kp_x': 1 if i == 23 else 2,
                'PidTrackingController.enabled': True if i == 23 else 'true',
                'PidTrackingController.plugin': 'hidden', 'controller_frequency': 20,
                'failure_tolerance': 0, 'publish_zero_velocity': True,
                'PidTrackingController.fixed': .4,
                'PidTrackingController.removed' if i == 23 else 'PidTrackingController.added': 3})))
    runs[4]['overview']['averages']['lateral']['case_mean_rmse'] = 0
    runs[5]['overview']['averages']['lateral']['case_mean_rmse'] = None
    runs[6]['overview']['averages']['lateral']['case_count'] = 0
    runs[7]['analysis_quality'] = 'confirmed_available'
    runs[9]['metric_version'] = 'v2'
    runs[11]['pid_parameters']['source_kind'] = 'config_file'
    runs[12]['pid_parameters'] = dict(status='unknown', reason='快照未确认')
    runs[18]['name'] = 'long-round-'+'x'*180+'</script><img src=x onerror="window.injected=true">__TRACK_ARCHIVE_COMPARISON_SCRIPT__'
    runs[23]['pid_parameters']['values']['PidTrackingController.text'] = '<img id="injected" src="https://invalid.test/x">'
    return dict(schema_version=1, generator='Track Insight archive', runs=runs[::-1])


def page(catalog, path):
    html = (ROOT/'resources/archive/index.html').read_text()
    for slot, filename in [('STYLE', 'comparison.css'), ('SCRIPT', 'comparison.js')]:
        html = html.replace('__TRACK_ARCHIVE_COMPARISON_'+slot+'__', (ROOT/'resources/archive'/filename).read_text())
    payload = json.dumps(catalog, ensure_ascii=False)
    for char, escaped in [('<', r'\u003c'), ('>', r'\u003e'), ('&', r'\u0026')]:
        payload = payload.replace(char, escaped)
    path.write_text(html.replace('__TRACK_ARCHIVE_DATA__', payload))


CHECKS = r'''
const assert=(ok,why)=>{if(!ok)throw Error(why);},$=id=>document.getElementById(id);
const runs=JSON.parse($('archiveData').textContent).runs,by=n=>runs.find(r=>r.start_label.endsWith(':'+String(n).padStart(2,'0')));
const change=(id,value)=>{$(id).value=value;$(id).dispatchEvent(new Event('change'));};
const pair=(a,b)=>{change('archiveCompareBaseline',by(a).id);change('archiveCompareTarget',by(b).id);};
const points=()=>[...document.querySelectorAll('[data-trend-run]')];
const rows=()=>[...$('archiveParameterRows').querySelectorAll('tr[data-parameter]')];
const row=key=>rows().find(r=>r.dataset.parameter==='PidTrackingController.'+key);
const cards=()=>[...$('archiveCompareSummary').querySelectorAll('strong')].map(e=>e.textContent);
assert($('error').hidden&&!document.querySelector('.archiveCompareError'),'Page failed to initialize');
assert(points().length===20&&points()[0].dataset.trendRun===by(5).id,'Default recent 20 chronology');
assert($('archiveCompareBaseline').value===by(23).id&&$('archiveCompareTarget').value===by(24).id,'Default pair');
assert(cards().join('|')==='0.0230 m|0.0240 m|+0.0010 m','Stored mean/delta');
assert(row('kp_x').cells[3].textContent==='已修改'&&getComputedStyle(row('kp_x')).backgroundColor==='rgb(224, 224, 224)','Modified parameter highlight');
assert(row('enabled').cells[3].textContent==='已修改','Typed value difference');
assert(row('added').cells[3].textContent==='新增'&&row('removed').cells[3].textContent==='已删除','Added/removed parameters');
assert(row('fixed').cells[3].textContent==='未修改','Unchanged parameter');
assert(!rows().some(r=>r.cells[0].textContent.startsWith('PidTrackingController.')||['plugin','controller_frequency','failure_tolerance','publish_zero_velocity'].includes(r.cells[0].textContent)),'Display filtering');
assert(!window.injected&&!document.querySelector('#injected')&&!document.querySelector('#roundComparison img'),'Hostile text executed');
assert(row('text').cells[2].textContent.includes('<img'),'Hostile parameter must remain literal');
$('archiveOnlyChanges').click();assert(!row('fixed')&&rows().length===5,'Changed-only filter');$('archiveOnlyChanges').click();
change('archiveTrendMetric','pooled_rmse');assert(cards().join('|')==='0.0460 m|0.0480 m|+0.0020 m','Stored pooled values');
change('archiveTrendMetric','case_mean_rmse');change('archiveTrendRange','all');assert(points().length===24,'All rounds');
const point=n=>points().find(p=>p.dataset.trendRun===by(n).id);
assert(!!point(5).querySelector('circle')&&!point(6).querySelector('circle')&&!point(7).querySelector('circle'),'Zero vs missing/no valid cases');
const segment=n=>document.querySelector('[data-trend-segment="'+by(n).id+'"]');
for(const n of [6,7,8,9,10,11])assert(!segment(n),'Missing/version/quality gap '+n);
assert(segment(12),'Comparable adjacent rounds should connect');
change('archiveTrendParameter','PidTrackingController.kp_x');point(12).focus();
assert($('archiveTrendPoint').textContent.includes('测试时参数未知'),'Legacy snapshot leaked as runtime evidence');
point(24).focus();assert($('archiveTrendPoint').textContent.includes('kp_x：2'),'Selected parameter readout');
pair(5,6);assert(cards()[0]==='0.0000 m'&&cards()[1]==='— m'&&cards()[2]==='不可直接比较','Missing metric delta');
pair(8,9);assert(cards()[2]==='不可直接比较','Evidence mismatch delta');
pair(9,10);assert(cards()[2]==='不可直接比较','Metric version mismatch delta');
pair(12,24);assert($('archiveOnlyChanges').disabled&&$('archiveParameterStatus').textContent.includes('测试时参数未知'),'Unknown snapshot message');
assert(rows().every(r=>r.cells[1].textContent==='未知'&&r.cells[3].textContent==='不可比较'&&!r.classList.contains('archiveParameterChanged')),'Unknown cannot imply parameter addition');
pair(13,12);assert(rows().length===0&&$('archiveParameterStatus').textContent.includes('快照未确认'),'Both unknown');
pair(24,24);assert(cards()[2]==='0.0000 m'&&rows().every(r=>r.cells[3].textContent==='未修改'),'Same round');
pair(23,24);point(22).dispatchEvent(new MouseEvent('click',{bubbles:true}));assert($('archiveCompareTarget').value===by(22).id,'Point click');
point(21).focus();
assert($('archiveCompareSummary').querySelector('a').getAttribute('href')===by(23).report,'Round report link');
assert(JSON.stringify(JSON.parse($('archiveData').textContent).runs)===JSON.stringify(runs),'Archive data mutation');
'''


async def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='track_compare_', ignore_cleanup_errors=True) as tmp:
        output = args.output or Path(tmp)
        output.mkdir(parents=True, exist_ok=True)
        file = output/'index.html'
        catalog = json.loads(args.catalog.read_text()) if args.catalog else fixture()
        page(catalog, file)
        profile = Path(tmp)/'chrome'
        chrome = subprocess.Popen(['/usr/bin/google-chrome', '--headless', '--disable-gpu',
            '--disable-background-networking', '--no-first-run', '--remote-debugging-port=0',
            '--user-data-dir='+str(profile), 'about:blank'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            for _ in range(200):
                if (profile/'DevToolsActivePort').exists(): break
                await asyncio.sleep(.05)
            port = (profile/'DevToolsActivePort').read_text().splitlines()[0]
            target = next(t for t in json.load(urlopen('http://127.0.0.1:'+port+'/json')) if t['type']=='page')
            async with websockets.connect(target['webSocketDebuggerUrl'], max_size=64*1024*1024) as ws:
                sequence, errors, requests = 0, [], []
                async def call(method, params=None):
                    nonlocal sequence
                    sequence += 1
                    await ws.send(json.dumps(dict(id=sequence, method=method, params=params or {})))
                    while True:
                        response = json.loads(await ws.recv())
                        if response.get('method')=='Runtime.exceptionThrown': errors.append(response['params'])
                        if response.get('method')=='Network.requestWillBeSent': requests.append(response['params']['request']['url'])
                        if response.get('id')==sequence:
                            assert 'error' not in response, response
                            return response.get('result', {})
                async def js(expression):
                    result = await call('Runtime.evaluate', dict(expression=expression, returnByValue=True, awaitPromise=True))
                    assert 'exceptionDetails' not in result, result
                    return result.get('result', {}).get('value')
                async def load():
                    await call('Page.navigate', dict(url=file.resolve().as_uri()))
                    for _ in range(200):
                        if await js("document.readyState==='complete'&&!!document.getElementById('archiveCompareTarget')"): break
                        await asyncio.sleep(.05)
                    assert await js("!!document.getElementById('archiveCompareTarget')&&!document.querySelector('.archiveCompareError')")
                await call('Runtime.enable');await call('Network.enable')
                await call('Emulation.setFocusEmulationEnabled', dict(enabled=True))
                await call('Emulation.setDeviceMetricsOverride', dict(width=1600, height=1200, deviceScaleFactor=1, mobile=False))
                await load()
                if not args.catalog:
                    await js('(()=>{'+CHECKS+'})()')
                    for key, code in [('Enter', 13), (' ', 32)]:
                        await call('Input.dispatchKeyEvent', dict(type='keyDown', key=key, windowsVirtualKeyCode=code))
                        await call('Input.dispatchKeyEvent', dict(type='keyUp', key=key, windowsVirtualKeyCode=code))
                        assert await js("document.activeElement.dataset.trendRun===document.getElementById('archiveCompareTarget').value&&document.activeElement.getAttribute('aria-pressed')==='true'"), 'Keyboard selection lost focus'
                else:
                    count = len(catalog['runs'])
                    assert await js("document.querySelectorAll('#rounds tr').length")==count
                    assert await js("document.querySelectorAll('[data-trend-run]').length")==min(20, count)
                    assert await js("!document.getElementById('error').hidden||!!document.querySelector('.archiveCompareError')") is False
                    await js("document.getElementById('archiveOnlyChanges').click()")
                issues = []
                if not args.catalog:
                    position = await js("(()=>{const p=[...document.querySelectorAll('[data-trend-run]')][19];p.scrollIntoView({block:'center'});const r=p.querySelector('circle').getBoundingClientRect(),next=[...document.querySelectorAll('[data-trend-run]')][20].querySelector('circle').getBoundingClientRect();return {x:r.x+r.width/2+2,y:r.y+r.height/2+2*(next.y-r.y)/(next.x-r.x),id:p.dataset.trendRun};})()")
                    for kind in ['mouseMoved', 'mousePressed', 'mouseReleased']:
                        await call('Input.dispatchMouseEvent', dict(type=kind,x=position['x'],y=position['y'],button='left',clickCount=1))
                    if await js("document.getElementById('archiveCompareTarget').value")!=position['id']:
                        issues.append('Real pointer click missed trend point')
                    await js("(()=>{const s=document.getElementById('archiveCompareTarget');s.value=JSON.parse(document.getElementById('archiveData').textContent).runs.find(r=>r.name.includes('__TRACK')).id;s.dispatchEvent(new Event('change'));})()")
                for width in [1920, 1000, 600]:
                    await call('Emulation.setDeviceMetricsOverride', dict(width=width, height=1100, deviceScaleFactor=1, mobile=False))
                    assert await js('document.documentElement.scrollWidth<=innerWidth'), 'Horizontal page overflow at '+str(width)
                    if not await js("(()=>{const box=document.getElementById('roundComparison').getBoundingClientRect();return [...document.querySelectorAll('#archiveCompareSummary .card,#roundComparison select')].every(e=>{const r=e.getBoundingClientRect();return r.left>=box.left&&r.right<=box.right;});})()"):
                        issues.append('Clipped comparison at '+str(width))
                assert not issues, issues
                await call('Emulation.setDeviceMetricsOverride', dict(width=1600, height=1200, deviceScaleFactor=1, mobile=False))
                await js("document.getElementById('roundComparison').scrollIntoView()")
                shot = await call('Page.captureScreenshot', dict(format='png'))
                (output/'comparison.png').write_bytes(base64.b64decode(shot['data']))
                if not args.catalog:
                    original = json.loads(json.dumps(catalog))
                    for count in [0, 1]:
                        catalog['runs'] = original['runs'][:count]
                        page(catalog, file);await load()
                        assert await js("document.querySelectorAll('[data-trend-run]').length")==count
                        if not count: assert await js("[...document.querySelectorAll('#roundComparison select, #roundComparison input')].every(e=>e.disabled)")
                        else: assert await js("document.querySelector('#archiveCompareSummary .card:last-child strong').textContent==='0.0000 m'")
                    page(original, file)
                assert not errors, errors
                assert not [url for url in requests if url.startswith(('http:', 'https:'))], requests
                print('PASS: '+('real catalog ('+str(len(catalog['runs']))+' rounds)' if args.catalog else '24-round fixture, boundaries, keyboard, empty/single rounds')+'; offline loading; desktop/mobile layout')
                if args.output: print('Preview: '+str(file)+'; screenshot: '+str(output/'comparison.png'))
        finally:
            chrome.terminate();chrome.wait(timeout=10)


if __name__ == '__main__':
    asyncio.run(main())
