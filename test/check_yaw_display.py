#!/usr/bin/env python3
"""Verify optional yaw-error plots using a rendered report; fixtures only change browser memory."""
import argparse
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
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='track_cursor_pin_', ignore_cleanup_errors=True) as profile:
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
                await js("document.getElementById('reviewMode').click()")
                await js("window.yawFixture=trackInsight.analysis.paths.find(p=>p.tracking.some(r=>r[10]));window.savedTracking=yawFixture.tracking;window.savedMetrics=JSON.stringify(yawFixture.metrics);yawFixture.tracking=savedTracking.map(r=>{const a=r.slice();a[9]=null;return a;});trackInsight.select([yawFixture],yawFixture.interval.id)")
                async def state():
                    return await js("(()=>{const g=document.getElementById('errors');return {axes:Object.keys(g._fullLayout).filter(k=>/^yaxis[0-9]*$/.test(k)).length,hidden:document.getElementById('yawErrorOption').hidden,checked:document.getElementById('showYawError').checked,range:g._fullLayout.xaxis.range,cursor:trackInsight.cursor,lines:g.layout.shapes.map(s=>s.x0),labels:[...g.querySelectorAll('.annotation-text')].map(e=>e.textContent)}})()")
                current=await state()
                assert current['axes']==2 and current['hidden'] and not current['checked'],current
                assert not any('target_yaw' in t for t in current['labels']),current
                # A finite zero is valid; invalid samples cannot enable the option.
                await js("yawFixture.tracking.forEach(r=>{r[9]=0;});trackInsight.select([yawFixture],yawFixture.interval.id)")
                current=await state()
                assert not current['hidden'] and current['axes']==2,current
                await js("yawFixture.tracking.forEach(r=>{r[9]=Math.PI/6;});trackInsight.select([yawFixture],yawFixture.interval.id)")
                await js("trackInsight.setCursor((yawFixture.start+yawFixture.end)/2);Plotly.relayout(document.getElementById('velocity'),{'xaxis.range':[yawFixture.start+.5,yawFixture.end-.5]}).then(()=>null)")
                await asyncio.sleep(.2)
                before=await state()
                assert before['axes']==2,before
                await js("document.getElementById('showYawError').checked=true;document.getElementById('showYawError').onchange()")
                current=await state()
                assert current['axes']==3 and current['range']==before['range'] and current['cursor']==before['cursor'],current
                degree=await js("(()=>{const g=document.getElementById('errors');return {label:g._fullLayout.yaxis3.title.text,values:g.data.find(t=>t.yaxis==='y3').y.filter(v=>v!==null)}})()")
                assert degree['label']=='姿态 (°)' and degree['values'] and all(abs(v-30)<1e-9 for v in degree['values']),degree
                assert current['lines']==[current['cursor']],current
                # Hide the third axis, then zoom/reset: it must never reappear as an empty plot.
                await js("document.getElementById('showYawError').checked=false;document.getElementById('showYawError').onchange()")
                await js("Plotly.relayout(document.getElementById('errors'),{'xaxis2.range':[yawFixture.start+1,yawFixture.end-1]}).then(()=>null)")
                await asyncio.sleep(.2)
                current=await state()
                assert current['axes']==2,current
                assert await js("document.getElementById('velocity')._fullLayout.xaxis.range")==current['range']
                await js("document.getElementById('resetTime').click()")
                await asyncio.sleep(.2)
                current=await state()
                assert current['axes']==2 and current['range']==await js('[yawFixture.start,yawFixture.end]'),current
                for mode in ['reportMode','reviewMode']:
                    await js("document.getElementById('"+mode+"').click();trackInsight.select([yawFixture],yawFixture.interval.id)")
                    assert (await state())['axes']==2
                # Invalid / non-finite / missing values do not reserve an empty third subplot.
                for value in ['NaN','Infinity','null','undefined']:
                    await js("yawFixture.tracking.forEach(r=>{r[9]="+value+";});document.getElementById('showYawError').checked=true;trackInsight.select([yawFixture],yawFixture.interval.id)")
                    current=await state()
                    assert current['axes']==2 and current['hidden'],current
                await js("yawFixture.tracking.forEach(r=>{r[9]=1;r[10]=false;});trackInsight.select([yawFixture],yawFixture.interval.id)")
                current=await state()
                assert current['axes']==2 and current['hidden'],current
                assert await js('JSON.stringify(yawFixture.metrics)===savedMetrics')
                assert await js("Plotly.toImage(document.getElementById('errors'),{format:'png',width:600,height:400}).then(s=>s.startsWith('data:image/png;base64,'))")
                await js("yawFixture.tracking=savedTracking;trackInsight.select([yawFixture],yawFixture.interval.id)")
                print('PASS: missing/invalid yaw hidden; valid zero; opt-in degrees; two/three axes; linked zoom/reset/cursor; both modes; PNG; unchanged metrics')
                assert await js("document.getElementById('failureBanner').hidden"),await js("document.getElementById('failureBanner').textContent")
                assert not errors,errors
                await call('Browser.close')
                chrome.wait(timeout=10)
        finally:
            if chrome.poll() is None:
                chrome.terminate()
                chrome.wait(timeout=10)

asyncio.run(main())
