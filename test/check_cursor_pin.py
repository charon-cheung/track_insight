#!/usr/bin/env python3
"""Check shared-cursor pinning with real mouse/key input in a dedicated Chrome profile."""
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
                async def mouse(kind, point, button='none'):
                    await call('Input.dispatchMouseEvent', {'type': kind, 'x': point[0], 'y': point[1],
                        'button': button, 'buttons': (2 if button == 'right' else 1) if kind == 'mousePressed' else 0, 'clickCount': 1 if button != 'none' else 0})
                    await asyncio.sleep(.1)
                async def point(graph, fraction, axis=1):
                    return await js("(()=>{const g=document.getElementById(" + json.dumps(graph) + ");g.scrollIntoView({block:'center'});const b=g.getBoundingClientRect(),a=g._fullLayout,s=" + json.dumps('' if axis == 1 else str(axis)) + ";return [b.x+a['xaxis'+s]._offset+a['xaxis'+s]._length*" + str(fraction) + ",b.y+a['yaxis'+s]._offset+a['yaxis'+s]._length*.35];})()")
                async def click(graph, fraction, axis=1):
                    p = await point(graph, fraction, axis)
                    await mouse('mouseMoved', p)
                    await mouse('mousePressed', p, 'left')
                    await mouse('mouseReleased', p, 'left')
                async def escape(held=False):
                    await call('Input.dispatchKeyEvent', {'type': 'keyDown', 'key': 'Escape', 'code': 'Escape', 'windowsVirtualKeyCode': 27})
                    if held:
                        await asyncio.sleep(.3)
                        await call('Input.dispatchKeyEvent', {'type': 'keyDown', 'key': 'Escape', 'code': 'Escape', 'windowsVirtualKeyCode': 27, 'autoRepeat': True})
                    await call('Input.dispatchKeyEvent', {'type': 'keyUp', 'key': 'Escape', 'code': 'Escape', 'windowsVirtualKeyCode': 27})
                    await asyncio.sleep(.15)
                async def snapshot():
                    return await js("(()=>{const v=document.getElementById('velocity'),e=document.getElementById('errors');return {cursor:trackInsight.cursor,timeline:document.getElementById('timeline').value,lines:[v,e].map(g=>g.layout.shapes[0].x0),pose:document.getElementById('trajectory').data.slice(-2).map(t=>[t.x,t.y]),readout:document.getElementById('instantReadouts').textContent};})()")
                async def frozen():
                    before = await snapshot()
                    for graph in ['velocity', 'errors']:
                        await mouse('mouseMoved', await point(graph, .8))
                        assert await snapshot() == before, 'Hover moved the pinned shared cursor or readout'
                        assert await js("document.querySelectorAll('#'+" + json.dumps(graph) + "+' .hoverlayer .spikeline').length") == 0, 'Temporary hover spike moved while pinned'
                    return before
                await call('Runtime.enable')
                await call('Emulation.setDeviceMetricsOverride', {'width': 1920, 'height': 1080, 'deviceScaleFactor': 1, 'mobile': False})
                await call('Page.navigate', {'url': Path(args.report).resolve().as_uri()})
                for _ in range(200):
                    if await js('!!window.trackInsight'): break
                    await asyncio.sleep(.05)
                assert await js('!!window.trackInsight')
                await js("(async()=>{const p=trackInsight.analysis.paths[0];await trackInsight.select([p],p.interval.id);})()")
                await click('velocity', .3)
                await frozen()  # Fails on the original hover-only implementation.
                assert await js("!document.getElementById('releaseCursor').hidden")
                for graph in ['velocity', 'errors']:
                    axes=await js("Object.keys(document.getElementById('"+graph+"')._fullLayout).filter(k=>/^xaxis[0-9]*$/.test(k)).map(k=>Number(k.slice(5)||1))")
                    for axis in axes:
                        before = await js('trackInsight.cursor')
                        await click(graph, .4 + axis*.07, axis)
                        after = await frozen()
                        assert abs(after['cursor']-before) > .01
                        assert all(abs(t-after['cursor']) < 1e-8 for t in after['lines'])
                before = await js('trackInsight.cursor')
                await js("document.getElementById('stepForward').click()")
                assert abs(await js('trackInsight.cursor')-before-.05) < 1e-8
                await frozen()
                await js("document.getElementById('stepBack').click()")
                assert abs(await js('trackInsight.cursor')-before) < 1e-8
                await js("const slider=document.getElementById('timeline');slider.value='4500';slider.dispatchEvent(new Event('input'))")
                await frozen()
                # A real zoom drag changes the viewport, without moving or releasing the pin.
                before = await js("({cursor:trackInsight.cursor,range:document.getElementById('velocity').layout.xaxis.range.slice()})")
                start, end = await point('velocity', .2), await point('velocity', .7)
                await mouse('mousePressed', start, 'left')
                await mouse('mouseMoved', end)
                await mouse('mouseReleased', end, 'left')
                assert await js('trackInsight.cursor') == before['cursor']
                assert await js("document.getElementById('velocity').layout.xaxis.range") != before['range']
                await frozen()
                await js("document.getElementById('resetTime').click()")
                await escape()
                assert await js("document.getElementById('releaseCursor').hidden")
                before = await js('trackInsight.cursor')
                await mouse('mouseMoved', await point('velocity', .2))
                assert abs(await js('trackInsight.cursor')-before) > .01, 'Escape did not restore hover'
                tooltip = await js("[...document.querySelectorAll('#velocity .hoverlayer .legendtext')].map(e=>e.textContent)")
                assert tooltip and all('\u00a0'*2 in text and '/ctrl/' not in text for text in tooltip), tooltip
                await click('velocity', .3)
                await js("document.getElementById('releaseCursor').click()")
                assert await js("document.getElementById('releaseCursor').hidden")
                # Right click and a drag returning to its start must never become a pin.
                p = await point('velocity', .35)
                await mouse('mousePressed', p, 'right')
                await mouse('mouseReleased', p, 'right')
                assert await js("document.getElementById('releaseCursor').hidden")
                await escape()
                start, end = await point('velocity', .25), await point('velocity', .65)
                await mouse('mousePressed', start, 'left')
                await mouse('mouseMoved', end)
                await mouse('mouseMoved', start)
                await mouse('mouseReleased', start, 'left')
                assert await js("document.getElementById('releaseCursor').hidden"), 'Drag returning to its start pinned'
                await js("document.getElementById('resetTime').click()")
                toolbar = await js("(()=>{const b=document.querySelector('#velocity .modebar-btn').getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2];})()")
                await mouse('mousePressed', toolbar, 'left')
                await mouse('mouseReleased', toolbar, 'left')
                assert await js("document.getElementById('releaseCursor').hidden"), 'Toolbar click pinned'
                await click('velocity', .3)
                await js("document.getElementById('play').click()")
                assert await js("document.getElementById('releaseCursor').hidden")
                before = await js('trackInsight.cursor')
                await asyncio.sleep(.25)
                assert await js('trackInsight.cursor') > before
                await click('errors', .4)
                assert await js("document.getElementById('play').textContent") == '播放'
                before = await snapshot()
                await asyncio.sleep(.25)
                assert await snapshot() == before
                # Mode changes and maximization preserve the pin; Escape releases before exiting.
                await js("document.getElementById('reportMode').click()")
                await asyncio.sleep(.25)
                assert await js("!document.getElementById('releaseCursor').hidden")
                await frozen()
                await js("document.getElementById('maximizeVelocity').click()")
                await asyncio.sleep(.4)
                assert await js("!document.getElementById('releaseVelocityCursor').hidden")
                await click('velocity', .5)
                await escape(held=True)
                assert await js("document.getElementById('velocityView').open && document.getElementById('releaseVelocityCursor').hidden")
                await click('velocity', .6)
                await js("document.getElementById('releaseVelocityCursor').click()")
                assert await js("document.getElementById('velocityView').open && document.getElementById('releaseVelocityCursor').hidden")
                await escape()
                assert not await js("document.getElementById('velocityView').open")
                await click('errors', .4)
                await js("(async()=>{const p=trackInsight.analysis.paths.at(-1);await trackInsight.select([p],p.interval.id);})()")
                assert await js("document.getElementById('releaseCursor').hidden")
                assert not errors, errors
                await call('Browser.close')
                chrome.wait(timeout=10)
                print('PASS: pin/repin all visible subplots; synchronized freeze; step/seek/play; zoom; release; mode/maximize/Escape; case reset; no JS exceptions')
        finally:
            if chrome.poll() is None:
                chrome.terminate()
                chrome.wait(timeout=10)


asyncio.run(main())
