#!/usr/bin/env python3
"""Offline browser acceptance test. Requires a dedicated Chrome CDP profile and websockets."""
import argparse
import asyncio
import base64
import json
from pathlib import Path
from urllib.request import urlopen
import websockets

async def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--profile",required=True)
    parser.add_argument("--report",required=True)
    parser.add_argument("--screenshot",required=True)
    args=parser.parse_args()
    port=(Path(args.profile)/"DevToolsActivePort").read_text().splitlines()[0]
    targets=json.load(urlopen("http://127.0.0.1:"+port+"/json"))
    target=next(x for x in targets if x["type"]=="page")
    errors=[]; external=[]; sequence=0
    async with websockets.connect(target["webSocketDebuggerUrl"],max_size=50*1024*1024) as ws:
        async def call(method,params=None):
            nonlocal sequence
            sequence+=1;identifier=sequence
            await ws.send(json.dumps({"id":identifier,"method":method,"params":params or {}}))
            while True:
                item=json.loads(await ws.recv())
                if item.get("method")=="Runtime.exceptionThrown":errors.append(item["params"])
                if item.get("method")=="Network.requestWillBeSent":
                    url=item["params"]["request"]["url"]
                    if url.startswith(("http:","https:")):external.append(url)
                if item.get("id")==identifier:
                    if "error" in item:raise AssertionError(item["error"])
                    return item.get("result",{})
        async def evaluate(expression):
            result=await call("Runtime.evaluate",{"expression":expression,"returnByValue":True,"awaitPromise":True})
            if "exceptionDetails" in result:raise AssertionError(result["exceptionDetails"])
            return result.get("result",{}).get("value")
        async def check_steps():
            await evaluate("document.getElementById('rate').value='2×'")
            selected=await evaluate("trackInsight.selected")
            for button,delta in [('stepForward',.05),('stepBack',-.05)]:
                step=await evaluate("(()=>{const before=trackInsight.cursor;document.getElementById('play').click();document.getElementById('"+button+"').click();return {before,after:trackInsight.cursor,label:document.getElementById('play').textContent};})()")
                assert abs(step['after']-step['before']-delta)<1e-8,step
                assert step['label']=='播放',step
                await asyncio.sleep(.15)
                assert await evaluate("trackInsight.cursor")==step['after'],'Step did not pause playback'
                synced=await evaluate("(()=>{const t=trackInsight.cursor,p=trackInsight.analysis.paths.find(p=>p.interval.id===trackInsight.selected),r=p.tracking.filter(r=>r[0]<=t).at(-1),v=p.velocity.filter(r=>r[0]<=t).at(-1);return {lines:['velocity','errors'].map(id=>document.getElementById(id).layout.shapes[0].x0),position:document.getElementById('trajectory').data.at(-2).x.slice(0,1),expectedPosition:r&&r[10]&&t<=r[13]+1e-9?[r[1]]:[],readout:document.getElementById('instant-command-0').textContent,expectedReadout:v&&v[7]&&t<=v[9]+1e-9?v[1].toFixed(3):'—',timestamp:document.getElementById('cursorTimestamp').textContent};})()")
                assert all(t==step['after'] for t in synced['lines']),synced
                assert synced['position']==synced['expectedPosition'],synced
                assert synced['readout']==synced['expectedReadout'],synced
                assert synced['timestamp'].startswith('Timestamp: '),synced
                labels=await evaluate("['velocity','errors'].map(id=>document.getElementById(id).layout.annotations.find(a=>a.name==='cursorTimestamp'))")
                assert all(a['text']==synced['timestamp'].removeprefix('Timestamp: ') and a['x']==step['after'] for a in labels),labels
            for edge,button,offset in [('start','stepBack',.02),('end','stepForward',-.02)]:
                bounds=await evaluate("(()=>{const p=trackInsight.analysis.paths.find(p=>p.interval.id===trackInsight.selected),edge=p."+edge+";trackInsight.setCursor(edge+("+str(offset)+"));document.getElementById('"+button+"').click();document.getElementById('"+button+"').click();return {expected:edge,actual:trackInsight.cursor};})()")
                assert bounds['actual']==bounds['expected'],bounds
                assert await evaluate("trackInsight.selected")==selected,'Step changed selected path'
            await evaluate("(()=>{const p=trackInsight.analysis.paths.find(p=>p.interval.id===trackInsight.selected);trackInsight.setCursor(p.start+1);document.getElementById('rate').value='1×';})()")
        async def check_favorites():
            keys=await evaluate("[...document.getElementById('quickSelection').options].map(o=>o.value)")
            if len(keys)<2:return
            async def select_key(key):
                await evaluate("(()=>{const s=document.getElementById('quickSelection');s.value="+json.dumps(key)+";s.dispatchEvent(new Event('change'));})()")
                await asyncio.sleep(.2)
            state="(()=>{const s=document.getElementById('favoriteSelection');return {keys:[...s.options].map(o=>o.value).filter(Boolean),hidden:document.getElementById('favoritePicker').hidden,active:document.getElementById('toggleFavorite').getAttribute('aria-pressed'),selected:trackInsight.selected,quick:document.getElementById('quickSelection').value};})()"
            # This acceptance test uses a dedicated browser profile.
            for key in (await evaluate(state))['keys']:
                await select_key(key);await evaluate("document.getElementById('toggleFavorite').click()")
            assert (await evaluate(state))['hidden']
            for key in [keys[1],keys[0]]:
                await select_key(key);await evaluate("document.getElementById('toggleFavorite').click()")
                assert (await evaluate(state))['active']=='true'
            saved=await evaluate(state)
            assert saved['keys']==keys[:2] and not saved['hidden'],saved
            await call("Page.reload",{"ignoreCache":True});await asyncio.sleep(.3)
            for _ in range(100):
                if await evaluate("Boolean(window.trackInsight)&&document.getElementById('favoriteSelection').options.length>0"):break
                await asyncio.sleep(.2)
            else:raise AssertionError('Favorites did not initialize after reload')
            assert (await evaluate(state))['keys']==keys[:2],'Favorites lost on reload'
            await evaluate("(()=>{const s=document.getElementById('favoriteSelection');s.value="+json.dumps(keys[1])+";s.dispatchEvent(new Event('change'));})()")
            await asyncio.sleep(.2)
            selected=await evaluate(state)
            assert selected['selected']==selected['quick']==keys[1] and selected['active']=='true',selected
            await evaluate("document.getElementById('reportMode').click()")
            assert (await evaluate(state))['keys']==keys[:2],'Mode switch lost favorites'
            await evaluate("document.getElementById('toggleFavorite').click()")
            removed=await evaluate(state)
            assert removed['keys']==keys[:1] and removed['active']=='false' and not removed['hidden'],removed
            await select_key(keys[0]);await evaluate("document.getElementById('toggleFavorite').click()")
            assert (await evaluate(state))['hidden'],'Empty favorite list remains visible'
            await evaluate("document.getElementById('reviewMode').click()")
        await call("Runtime.enable")
        await call("Network.enable")
        await call("Network.emulateNetworkConditions",{"offline":True,"latency":0,"downloadThroughput":0,"uploadThroughput":0})
        await call("Emulation.setDeviceMetricsOverride",{"width":1600,"height":1100,"deviceScaleFactor":1,"mobile":False})
        await call("Page.navigate",{"url":Path(args.report).resolve().as_uri()})
        for _ in range(100):
            await asyncio.sleep(.2)
            if await evaluate("Boolean(window.trackInsight)"):break
        else:raise AssertionError(await evaluate("document.getElementById('failureBanner').textContent"))
        assert await evaluate("Boolean(document.getElementById('reviewMode'))"),"Review mode selector absent"
        assert await evaluate("document.body.classList.contains('review-mode')&&document.getElementById('reviewMode').getAttribute('aria-pressed')==='true'"),'Default view is not review mode'
        await check_favorites()
        await evaluate("document.getElementById('reportMode').click()")
        # Single-path tasks have one shared entry; multi-path and empty tasks retain their identity.
        navigation_cases=await evaluate("trackInsight.analysis.tasks.map(t=>({id:t.id,paths:trackInsight.analysis.paths.filter(p=>p.interval.navigation_id===t.id).map(p=>p.interval.id)}))")
        tested=set()
        for task in navigation_cases:
            kind=min(len(task['paths']),2)
            if kind in tested:continue
            tested.add(kind)
            key=task['paths'][0] if kind==1 else task['id']
            state=await evaluate("(async()=>{const key="+json.dumps(key)+",task=trackInsight.analysis.tasks.find(t=>t.id==="+json.dumps(task['id'])+");document.querySelector('#navigation button[data-key=\"'+key+'\"]').click();await new Promise(r=>setTimeout(r,250));return {selected:trackInsight.selected,title:document.getElementById('selectionTitle').textContent,info:document.getElementById('taskInfo').textContent,coverage:document.getElementById('coverageHeading').textContent,detail:document.querySelector('.card .detail').textContent,range:document.getElementById('velocity').layout.xaxis.range,taskRange:[task.start_ns,task.end_ns].map(ns=>Number(BigInt(ns)-BigInt(trackInsight.analysis.clock.origin_ns))/1e9),taskEntries:document.querySelectorAll('#navigation button[data-key=\"'+task.id+'\"]').length,quick:document.getElementById('quickSelection').value,log:document.getElementById('logDetail').textContent};})()")
            assert state['selected']==key and state['quick']==key,state
            assert task['id'] in state['info'],state
            if kind==1:
                assert state['taskEntries']==0,state
                assert state['coverage']=='路径区间覆盖率',state
                assert '有效 ' in state['detail'] and ' / 区间 ' in state['detail'],state
                expected=await evaluate("(()=>{const p=trackInsight.analysis.paths.find(p=>p.interval.id==="+json.dumps(key)+");return {range:[p.start,p.end],value:p.metrics.lateral.rmse.toFixed(3),coverage:(p.metrics.lateral.coverage*100).toFixed(1)};})()")
                assert state['range']==expected['range'],state
                assert expected['coverage']+'%' in state['detail'],state
                assert await evaluate("document.querySelector('.card .value').firstChild.textContent")==expected['value']
            else:
                assert state['range']==state['taskRange'],state
                assert state['coverage']=='导航全程覆盖率',state
                if kind==0:
                    assert '无可评价路径' in state['info'],state
                    assert await evaluate("document.getElementById('association').textContent")=='无可评价路径'
                    assert '选择事件' in state['log'],state
                else:
                    await evaluate("(()=>{const b=document.querySelector('#navigation button[data-key=\""+key+"\"]');b.closest('details').open=true;b.closest('details').open=false;const q=document.getElementById('quickSelection');q.value="+json.dumps(task['paths'][0])+";q.dispatchEvent(new Event('change'));})()")
                    await asyncio.sleep(.3)
                    assert await evaluate("trackInsight.selected")==task['paths'][0]
                    assert await evaluate("document.querySelector('#navigation button.selected').closest('details').open")
        initial=await evaluate("({title:document.title,paths:trackInsight.analysis.plans.length,time:trackInsight.time(10),traces:document.getElementById('velocity').data.length})")
        await call("Emulation.setTimezoneOverride",{"timezoneId":"America/Los_Angeles"})
        assert await evaluate("trackInsight.time(10)")==initial["time"],"Viewer timezone changed labels"
        await evaluate("trackInsight.select([trackInsight.analysis.paths[Math.min(8,trackInsight.analysis.paths.length-1)]],'acceptance')")
        await evaluate("trackInsight.setCursor(trackInsight.analysis.paths[Math.min(8,trackInsight.analysis.paths.length-1)].start+1)")
        assert await evaluate("document.getElementById('trajectory').data.at(-2).x.length>0"),"Cursor position absent"
        await evaluate("document.getElementById('multiPlans').click()")
        await asyncio.sleep(.3)
        assert await evaluate("document.getElementById('trajectory').data.length")>=initial["paths"]
        await evaluate("document.getElementById('play').click()")
        before=await evaluate("trackInsight.cursor")
        await asyncio.sleep(.4)
        after=await evaluate("trackInsight.cursor")
        assert after>before,"Playback did not advance"
        await evaluate("document.getElementById('play').click()")
        await evaluate("Plotly.relayout(document.getElementById('velocity'),{'xaxis.range[0]':10,'xaxis.range[1]':20}).then(()=>true)")
        await asyncio.sleep(.2)
        assert await evaluate("document.getElementById('errors').layout.xaxis.range")==[10,20],"Time range did not link"
        await evaluate("document.getElementById('resetTime').click()")
        await evaluate("document.getElementById('multiPlans').click()")
        await asyncio.sleep(.3)
        # Mode changes must preserve selection, cursor and zoom, using one set of plots.
        await evaluate("trackInsight.select([trackInsight.analysis.paths[Math.min(8,trackInsight.analysis.paths.length-1)]],trackInsight.analysis.paths[Math.min(8,trackInsight.analysis.paths.length-1)].interval.id)")
        await evaluate("trackInsight.setCursor(trackInsight.analysis.paths[Math.min(8,trackInsight.analysis.paths.length-1)].start+1)")
        await evaluate("(()=>{const p=trackInsight.analysis.paths[Math.min(8,trackInsight.analysis.paths.length-1)];return Plotly.relayout(document.getElementById('velocity'),{'xaxis.range[0]':p.start+.5,'xaxis.range[1]':Math.min(p.end,p.start+8)}).then(()=>true);})()")
        await asyncio.sleep(.2)
        await check_steps()
        state_expression="({selected:trackInsight.selected,cursor:trackInsight.cursor,range:document.getElementById('velocity').layout.xaxis.range})"
        saved=await evaluate(state_expression)
        await evaluate("document.getElementById('reviewMode').click()")
        for width,height in [(1366,768),(1920,1080)]:
            await call("Emulation.setDeviceMetricsOverride",{"width":width,"height":height,"deviceScaleFactor":1,"mobile":False})
            await asyncio.sleep(.6)
            boxes=await evaluate("(()=>{const box=id=>{const r=document.getElementById(id).getBoundingClientRect();return {x:r.x,y:r.y,right:r.right,bottom:r.bottom,width:r.width,height:r.height};};return {plots:['trajectory','velocity','errors'].map(box),dock:box('playbackDock'),pageWidth:document.documentElement.scrollWidth,pageHeight:document.documentElement.scrollHeight,sidebar:getComputedStyle(document.getElementById('sidebar')).display};})()")
            assert boxes['sidebar']=='none',boxes
            assert boxes['pageWidth']<=width+1 and boxes['pageHeight']<=height+1,boxes
            plots=boxes['plots'];assert all(p['width']>250 and p['height']>200 for p in plots),boxes
            assert plots[0]['right']<=plots[1]['x'] and plots[1]['right']<=plots[2]['x'],boxes
            assert max(p['bottom'] for p in plots)<=boxes['dock']['y']+1,boxes
            assert boxes['dock']['bottom']<=height,boxes
            assert await evaluate(state_expression)==saved,'Mode or resize changed selection/cursor/zoom'
        assert await evaluate("getComputedStyle(document.getElementById('toggleSidebar')).display")=='none','Review sidebar toggle still visible'
        await evaluate("document.getElementById('reportMode').click()")
        await asyncio.sleep(.3)
        assert await evaluate(state_expression)==saved,'Returning to report changed state'
        assert await evaluate("getComputedStyle(document.getElementById('sidebar')).display")!='none','Report sidebar missing'
        assert await evaluate("document.getElementById('downloadMetrics').closest('aside')!==null"),'CSV export not restored'
        await evaluate("document.getElementById('toggleSidebar').click()")
        assert await evaluate("getComputedStyle(document.getElementById('sidebar')).display")=='none'
        await evaluate("document.getElementById('toggleSidebar').click()")
        assert await evaluate("getComputedStyle(document.getElementById('sidebar')).display")!='none'
        assert await evaluate("document.getElementById('playbackDock').parentElement.id")=='trajectoryPanel'
        await evaluate("document.getElementById('reviewMode').click()")
        await evaluate("document.getElementById('resetTime').click()")
        await evaluate("(()=>{const slider=document.getElementById('timeline');slider.value='4000';slider.dispatchEvent(new Event('input',{bubbles:true}));})()")
        await asyncio.sleep(.2)
        synced=await evaluate("(()=>{const p=trackInsight.analysis.paths.find(p=>p.interval.id===trackInsight.selected),t=trackInsight.cursor,r=p.tracking.find(r=>r[0]<=t&&r[13]>t),v=p.velocity.find(r=>r[0]<=t&&r[9]>t);return {cursor:t,expected:p.start+.4*(p.end-p.start),lines:['velocity','errors'].map(id=>document.getElementById(id).layout.shapes[0].x0),position:document.getElementById('trajectory').data.at(-2).x,rawPosition:r&&r[10]?r.slice(1,2):[],readout:document.getElementById('instant-command-0').textContent,expectedReadout:v&&v[7]?v[1].toFixed(3):'—',timestamp:document.getElementById('cursorTimestamp').textContent};})()")
        assert abs(synced['cursor']-synced['expected'])<1e-8,synced
        assert all(t==synced['cursor'] for t in synced['lines']),synced
        assert synced['position'][:1]==synced['rawPosition'],synced
        assert synced['readout']==synced['expectedReadout'],synced
        assert synced['timestamp'].startswith('Timestamp: '),synced
        await evaluate("document.getElementById('play').click()")
        before=await evaluate("trackInsight.cursor");await asyncio.sleep(.4)
        assert await evaluate("trackInsight.cursor")>before,'Review playback did not advance'
        assert await evaluate("['velocity','errors'].every(id=>{const a=document.getElementById(id).layout.annotations.find(a=>a.name==='cursorTimestamp');return a.text===document.getElementById('cursorTimestamp').textContent.replace('Timestamp: ','')&&a.x===trackInsight.cursor;})"),'Playback timestamp did not synchronize'
        await evaluate("document.getElementById('play').click()")
        await check_steps()
        await call("Emulation.setDeviceMetricsOverride",{"width":1366,"height":768,"deviceScaleFactor":1,"mobile":False})
        await asyncio.sleep(.5)
        result=await call("Page.captureScreenshot",{"format":"png","captureBeyondViewport":False})
        Path(args.screenshot).write_bytes(base64.b64decode(result["data"]))
        assert not external,external
        assert not errors,errors
        print(json.dumps({"status":"passed","initial":initial,"offline":True,"timezone_independent":True,
                          "playback":True,"favorites_add_remove_reload_navigate":True,"step_50ms_both_modes":True,"step_pauses_and_clamps":True,"range_link":True,"review_mode":True,"shared_cursor":True,
                          "navigation_cases":sorted(tested),"state_preserved":True,"viewports":["1366x768","1920x1080"],"errors":len(errors)},ensure_ascii=False))
asyncio.run(main())
