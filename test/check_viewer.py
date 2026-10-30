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
        async def check_velocity_maximize():
            snapshot="({selected:trackInsight.selected,cursor:trackInsight.cursor,range:document.getElementById('velocity').layout.xaxis.range.slice(),visible:document.getElementById('velocity').data.map(t=>t.visible)})"
            for mode,width,height in [('reviewMode',1366,768),('reportMode',1920,1080)]:
                await call('Emulation.setDeviceMetricsOverride',{'width':width,'height':height,'deviceScaleFactor':1,'mobile':False})
                await evaluate("(async()=>{document.getElementById('"+mode+"').click();const p=trackInsight.analysis.paths[0];await trackInsight.select([p],p.interval.id);trackInsight.setCursor((p.start+p.end)/2);await Plotly.relayout(document.getElementById('velocity'),{'xaxis.range':[p.start+.1,p.end-.1]});document.querySelector('[data-velocity-group=feedback]').click();document.getElementById('maximizeVelocity').scrollIntoView({block:'center'});})()")
                await asyncio.sleep(.25)
                before=await evaluate(snapshot)
                scroll=await evaluate('window.scrollY')
                await evaluate("document.getElementById('maximizeVelocity').click()")
                await asyncio.sleep(.3)
                state=await evaluate("(()=>{const d=document.getElementById('velocityView'),g=document.getElementById('velocity').getBoundingClientRect();document.getElementById('quickSelection').focus();return {modal:d.matches(':modal'),onlyVelocity:d.querySelectorAll('.panel').length===1&&d.contains(document.getElementById('velocity')),focusInside:d.contains(document.activeElement),width:g.width,height:g.height,ticks:document.getElementById('velocity').layout.xaxis.tickvals.length}})()")
                assert state['modal'] and state['onlyVelocity'] and state['focusInside'],state
                assert state['width']>width*.94 and state['height']>height*.8 and state['ticks']==6,state
                assert await evaluate(snapshot)==before,'Maximize reset plot state'
                # Zoom and curve choices made in the focused view must survive returning.
                await evaluate("(async()=>{document.querySelector('[data-velocity-group=feedback]').click();const g=document.getElementById('velocity'),r=g.layout.xaxis.range;await Plotly.relayout(g,{'xaxis.range':[r[0]+.1,r[1]-.1]});})()")
                expected=await evaluate(snapshot)
                if mode=='reviewMode':
                    screenshot=await call('Page.captureScreenshot',{'format':'png'})
                    Path(args.screenshot).with_name(Path(args.screenshot).stem+'_maximized.png').write_bytes(base64.b64decode(screenshot['data']))
                    await evaluate("document.getElementById('maximizeVelocity').click()")
                else:
                    await call('Input.dispatchKeyEvent',{'type':'keyDown','key':'Escape','code':'Escape','windowsVirtualKeyCode':27})
                    await call('Input.dispatchKeyEvent',{'type':'keyUp','key':'Escape','code':'Escape','windowsVirtualKeyCode':27})
                await asyncio.sleep(.3)
                assert not await evaluate("document.getElementById('velocityView').open")
                assert await evaluate("document.getElementById('velocityPanel').parentElement.id")=='chartDeck'
                assert await evaluate(snapshot)==expected,'Returning reset plot state'
                assert abs(await evaluate('window.scrollY')-scroll)<=2,'Returning lost scroll position'
            await evaluate("document.getElementById('reviewMode').click();document.getElementById('resetTime').click()")
            await call('Emulation.setDeviceMetricsOverride',{'width':1600,'height':1100,'deviceScaleFactor':1,'mobile':False})
        async def check_velocity_options():
            saved=await evaluate("({key:trackInsight.selected,cursor:trackInsight.cursor})")
            for key in ['command','feedback','tracking_raw','downstream']:
                state=await evaluate("(async()=>{const key="+json.dumps(key)+",g=document.getElementById('velocity'),input=document.querySelector('[data-velocity-group=\"'+key+'\"]'),before={cursor:trackInsight.cursor,range:g.layout.xaxis.range.slice()};if(input.disabled)return {disabled:true};if(!input.checked)input.click();input.click();await new Promise(r=>setTimeout(r,100));return {before,after:{cursor:trackInsight.cursor,range:g.layout.xaxis.range.slice()},checked:input.checked,visible:g.data.filter(t=>t.legendgroup===key).map(t=>t.visible)};})()")
                if state.get('disabled'):continue
                assert not state['checked'] and all(v=='legendonly' for v in state['visible']),state
                assert state['before']==state['after'],state
            await evaluate("(async()=>{const p=trackInsight.analysis.paths.at(-1);await trackInsight.select([p],p.interval.id);document.getElementById('reportMode').click();await trackInsight.select([p],p.interval.id);})()")
            state=await evaluate("({open:document.getElementById('velocityOptions').open,groups:[...document.querySelectorAll('[data-velocity-group]')].map(i=>({key:i.dataset.velocityGroup,checked:i.checked,disabled:i.disabled,visible:document.getElementById('velocity').data.filter(t=>t.legendgroup===i.dataset.velocityGroup).map(t=>t.visible)}))})")
            assert state['open'],state
            for item in state['groups']:
                if item['disabled']:continue
                assert not item['checked'] and all(v=='legendonly' for v in item['visible']),item
            # Plotly legend actions use restyle; these must update the corresponding checkboxes too.
            await evaluate("Plotly.restyle(document.getElementById('velocity'),{visible:true}).then(()=>true)")
            assert await evaluate("[...document.querySelectorAll('[data-velocity-group]')].every(i=>i.disabled||i.checked)")
            await evaluate("document.getElementById('reviewMode').click()")
            assert not await evaluate("document.getElementById('velocityOptions').open")
            await evaluate("(async()=>{const p=trackInsight.analysis.paths.find(p=>p.interval.id==="+json.dumps(saved['key'])+");await trackInsight.select([p],p.interval.id);trackInsight.setCursor("+str(saved['cursor'])+");})()")
        async def check_downstream():
            state=await evaluate("(()=>{const a=trackInsight.analysis,p=a.paths.find(p=>p.interval.id===trackInsight.selected),rows=(a.signals?.downstream||[]).filter(r=>p&&r[0]>=p.start&&r[0]<p.end);return {expected:a.meta.downstream_time_basis==='bag_record'&&rows.length>0,rows,origin:a.clock.origin_ns,raw:a.raw.downstream,traces:document.getElementById('velocity').data.filter(t=>t.legendgroup==='downstream')};})()")
            if not state['expected']:
                assert not state['traces'],state
                return
            assert len(state['traces'])==3
            raw={str(v['stamp']['record_ns']):v for v in state['raw']}
            samples={r[0]:r for r in state['rows']}
            for row in state['rows']:
                assert row[5] in raw
                assert abs(row[0]-(int(row[5])-int(state['origin']))/1e9)<1e-9
            for axis,trace in enumerate(state['traces']):
                assert trace['line']['color']=='#FFD700' and 'record' in trace['name']
                for x,y,text in zip(trace['x'],trace['y'],trace['text']):
                    if x is None:continue
                    assert y==samples[x][axis+1]
                    assert 'bag record 时间' in text and '/ctrl/velocity/navi/clct' in text
        async def check_overview():
            saved=await evaluate("({key:trackInsight.selected,cursor:trackInsight.cursor})")
            await evaluate("document.getElementById('reportMode').click()")
            state=await evaluate("(()=>{const a=trackInsight.analysis;return {visible:!document.getElementById('reportOverview').hidden,active:document.getElementById('overviewNavigation').getAttribute('aria-current'),rows:[...document.querySelectorAll('#overviewCases tr[data-case]')].map(r=>r.dataset.case),expected:a.overview.case_ids,averages:[...document.querySelectorAll('#overviewAverages tr')].map(r=>({key:r.dataset.metric,cells:[...r.cells].map(c=>c.textContent)})),reference:a.overview.averages,plots:getComputedStyle(document.getElementById('reviewWorkspace')).display,star:getComputedStyle(document.getElementById('favoritePicker')).display};})()")
            assert state['visible'] and state['active']=='page' and state['rows']==state['expected'],state
            assert state['plots']=='none' and state['star']=='none',state
            for row in state['averages']:
                ref=state['reference'].get(row['key'],{})
                for column,key in [(1,'case_mean_rmse'),(2,'pooled_rmse')]:
                    value=ref.get(key)
                    if value is None:assert row['cells'][column].startswith('—'),row
                    else:assert abs(float(row['cells'][column].split()[0])-value)<=.000050001,row
            if state['rows']:
                key=state['rows'][-1]
                await evaluate("document.querySelector('#overviewCases button[data-case=\""+key+"\"]').click()")
                await asyncio.sleep(.3)
                assert await evaluate("trackInsight.selected")==key
                assert await evaluate("document.getElementById('reportOverview').hidden")
                assert await evaluate("getComputedStyle(document.getElementById('reviewWorkspace')).display")!='none'
            await evaluate("document.getElementById('overviewNavigation').click()")
            assert await evaluate("!document.getElementById('reportOverview').hidden")
            await asyncio.sleep(.2)
            shot=await call("Page.captureScreenshot",{"format":"png","captureBeyondViewport":True})
            Path(args.screenshot).with_stem(Path(args.screenshot).stem+'_overview').write_bytes(base64.b64decode(shot['data']))
            await evaluate("document.getElementById('reviewMode').click()")
            await asyncio.sleep(.3)
            assert await evaluate("document.getElementById('reportOverview').hidden")
            assert await evaluate("getComputedStyle(document.getElementById('overviewNavigation')).display")=='none'
            await evaluate("(async()=>{const key="+json.dumps(saved['key'])+";await trackInsight.select(trackInsight.analysis.paths.filter(p=>p.interval.id===key||p.interval.navigation_id===key),key);trackInsight.setCursor("+str(saved['cursor'])+");})()")
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
        async def check_report_filter():
            entries=await evaluate("[...document.querySelectorAll('#navigation button')].map(b=>{const p=trackInsight.analysis.paths.find(p=>p.interval.id===b.dataset.key),t=trackInsight.analysis.tasks.find(t=>t.id===(p?.interval.navigation_id||b.dataset.key));return {key:b.dataset.key,excluded:['replaced','failed'].includes(t?.outcome)}})")
            eligible=[entry['key'] for entry in entries if not entry['excluded']]
            excluded=[entry['key'] for entry in entries if entry['excluded']]
            options="[...document.getElementById('quickSelection').options].map(o=>o.value)"
            assert await evaluate(options)==eligible,'Default comprehensive list includes excluded cases'
            for mode in ['reportMode','reviewMode']:
                await evaluate("document.getElementById('"+mode+"').click()")
                await asyncio.sleep(.2)
                assert await evaluate(options)==eligible,'Mode switch restored excluded cases'
                visible=await evaluate("[...document.querySelectorAll('#navigation button')].filter(b=>!b.hidden&&!b.closest('details[hidden]')).map(b=>b.dataset.key)")
                assert visible==eligible,visible
                starred=await evaluate("[...document.getElementById('favoriteSelection').options].map(o=>o.value)")
                assert not set(starred)&set(excluded),'Excluded favorite visible'
                if eligible:
                    assert await evaluate('trackInsight.selected') in eligible,'Selection remained on excluded case'
                    await evaluate("(()=>{const s=document.getElementById('quickSelection');s.selectedIndex=0;s.dispatchEvent(new Event('change'));})()")
                    for index,key in enumerate(eligible):
                        if index:await evaluate("document.getElementById('nextSelection').click()")
                        await asyncio.sleep(.15)
                        assert await evaluate('trackInsight.selected')==key,'Paging visited excluded case'
                    assert await evaluate("document.getElementById('nextSelection').disabled")
            await evaluate("document.getElementById('reportMode').click()")
        async def check_delete_selection():
            stored=await evaluate("Object.fromEntries(Object.entries(localStorage).filter(([key])=>key.startsWith('trackInsight:')))")
            async def reload_report():
                await call('Page.reload',{'ignoreCache':True});await asyncio.sleep(.3)
                for _ in range(100):
                    if await evaluate("Boolean(window.trackInsight)&&document.getElementById('favoriteSelection').options.length>0"):return
                    await asyncio.sleep(.2)
                raise AssertionError('Report did not initialize after reload')
            try:
                await evaluate("document.getElementById('reviewMode').click()")
                keys=await evaluate("[...document.getElementById('quickSelection').options].map(o=>o.value)")
                if len(keys)<2:return
                key=keys[1]
                await evaluate("(()=>{const s=document.getElementById('quickSelection');s.value="+json.dumps(key)+";s.dispatchEvent(new Event('change'));})()")
                await asyncio.sleep(.2)
                before=await evaluate("document.getElementById('cards').textContent")
                await evaluate("if(document.getElementById('toggleFavorite').getAttribute('aria-pressed')!=='true')document.getElementById('toggleFavorite').click();document.getElementById('deleteSelection').click()")
                await asyncio.sleep(.2)
                expected=keys[2] if len(keys)>2 else keys[0]
                assert await evaluate('trackInsight.selected')==expected,'Delete did not choose adjacent item'
                state="(()=>{const key="+json.dumps(key)+";return {normal:[...document.getElementById('quickSelection').options].some(o=>o.value===key),starred:[...document.getElementById('favoriteSelection').options].some(o=>o.value===key)}})()"
                assert await evaluate(state)=={'normal':False,'starred':False},'Deleted item remains in comprehensive dropdowns'
                await evaluate("document.getElementById('reportMode').click()")
                assert await evaluate(state)=={'normal':True,'starred':True},'Delete affected report mode'
                assert await evaluate("getComputedStyle(document.getElementById('deleteSelection')).display")=='none'
                await evaluate("(()=>{const s=document.getElementById('favoriteSelection');s.value="+json.dumps(key)+";s.dispatchEvent(new Event('change'));})()")
                await asyncio.sleep(.2)
                assert await evaluate("document.getElementById('cards').textContent")==before,'Report metrics changed'
                await evaluate("document.getElementById('reviewMode').click()")
                assert await evaluate(state)=={'normal':False,'starred':False}
                await reload_report()
                assert await evaluate(state)=={'normal':False,'starred':False},'Deleted item returned after reload'
                await evaluate("document.getElementById('reportMode').click()")
                assert await evaluate(state)=={'normal':True,'starred':True},'Report lost item after reload'
            finally:
                await evaluate("(()=>{for(const key of Object.keys(localStorage))if(key.startsWith('trackInsight:'))localStorage.removeItem(key);for(const [key,value] of Object.entries("+json.dumps(stored)+"))localStorage.setItem(key,value);})()")
                await reload_report()
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
        assert await evaluate("!document.querySelector('[data-velocity-group=tracking_raw]').checked&&document.getElementById('velocity').data.filter(t=>t.legendgroup==='tracking_raw').every(t=>t.visible==='legendonly')"),'Raw commands must be hidden by default'
        await check_velocity_maximize()
        await check_velocity_options()
        await check_downstream()
        await check_overview()
        await check_favorites()
        await check_report_filter()
        await check_delete_selection()
        await evaluate("document.getElementById('reportMode').click()")
        # Single-path tasks have one shared entry; multi-path and empty tasks retain their identity.
        navigation_cases=await evaluate("trackInsight.analysis.tasks.filter(t=>!['replaced','failed'].includes(t.outcome)).map(t=>({id:t.id,paths:trackInsight.analysis.paths.filter(p=>p.interval.navigation_id===t.id).map(p=>p.interval.id)}))")
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
        # Review records are portable, recoverable and isolated to one test round.
        saved_storage=await evaluate("Object.fromEntries(Object.entries(localStorage).filter(([key])=>key.startsWith('trackInsight:')))")
        async def reload_records():
            await evaluate('window.reviewReloadPending=true')
            await call('Page.reload',{'ignoreCache':True})
            for _ in range(100):
                await asyncio.sleep(.1)
                if await evaluate("!window.reviewReloadPending&&Boolean(window.trackInsight)&&Boolean(document.getElementById('reviewRecordsDialog'))"):return
            raise AssertionError('Review records did not initialize')
        async def import_record(record):
            text=record if isinstance(record,str) else json.dumps(record)
            await evaluate("(()=>{const input=document.getElementById('reviewRecordsFile'),data=new DataTransfer();data.items.add(new File(["+json.dumps(text)+"],'records.json',{type:'application/json'}));input.files=data.files;input.dispatchEvent(new Event('change'));})()")
            await asyncio.sleep(.3)
            return await evaluate("document.getElementById('reviewRecordsStatus').textContent")
        try:
            await evaluate("(()=>{const m=trackInsight.analysis.meta,i=JSON.stringify([m.input_sha256,m.start_ns,m.end_ns]);localStorage.removeItem('trackInsight:stars:v2:'+i);localStorage.setItem('trackInsight:favorites:v1:'+i,JSON.stringify(['E1']));localStorage.setItem('trackInsight:hidden:v1:'+i,JSON.stringify(['E2']));})()")
            await reload_records()
            assert await evaluate("document.getElementById('toggleBlackFavorite').getAttribute('aria-pressed')")=='true','Old favorite was lost'
            assert not await evaluate("[...document.getElementById('quickSelection').options].some(o=>o.value==='E2')"),'Old hidden item was lost'
            await evaluate("document.getElementById('manageReviewRecords').click()")
            assert await evaluate("document.getElementById('reviewRecordsDialog').matches(':modal')")
            assert await evaluate("document.querySelector('#hiddenReviewRecords [data-key=E2]')!==null")
            download_dir=Path(args.screenshot).resolve().parent/'review_record_downloads'
            download_dir.mkdir(exist_ok=True)
            await call('Browser.setDownloadBehavior',{'behavior':'allow','downloadPath':str(download_dir)})
            previous_downloads={p:p.stat().st_mtime_ns for p in download_dir.glob('Track_Insight_review_*.json')}
            await evaluate("document.getElementById('exportReviewRecords').click()")
            for _ in range(50):
                downloads=[p for p in download_dir.glob('Track_Insight_review_*.json') if p.stat().st_mtime_ns!=previous_downloads.get(p)]
                if downloads:break
                await asyncio.sleep(.1)
            assert downloads,'No review record download'
            record=json.loads(max(downloads,key=lambda p:p.stat().st_mtime_ns).read_text())
            assert record['schema_version']==2 and record['favorites']=={'red':[], 'black':['E1']} and record['hidden']==['E2'],record
            await evaluate("document.querySelector('#hiddenReviewRecords [data-key=E2] button').click()")
            assert await evaluate("[...document.getElementById('quickSelection').options].some(o=>o.value==='E2')")
            before_metrics=await evaluate("JSON.stringify(trackInsight.analysis.overview)")
            record['favorites']['red'].append('E2')
            assert '导入完成' in await import_record(record)
            assert not await evaluate("[...document.getElementById('quickSelection').options].some(o=>o.value==='E2')")
            snapshot="Object.fromEntries(Object.entries(localStorage).filter(([key])=>key.startsWith('trackInsight:')))"
            unchanged=await evaluate(snapshot)
            for bad in [dict(record,source={}),dict(record,schema_version=999),dict(record,hidden=['unknown']),dict(record,favorites='E1'),'not json']:
                assert '导入失败' in await import_record(bad)
                assert await evaluate(snapshot)==unchanged,'Invalid import mutated saved records'
            all_keys=await evaluate("[...document.querySelectorAll('#navigation button')].map(b=>b.dataset.key)")
            record['hidden']=all_keys
            assert '导入完成' in await import_record(record)
            assert await evaluate("document.getElementById('quickSelection').options.length")==0
            await evaluate("document.getElementById('restoreAllHidden').click()")
            assert await evaluate("document.getElementById('quickSelection').options.length")>0
            assert await evaluate("document.getElementById('restoreAllHidden').disabled")
            assert await evaluate("[...document.getElementById('favoriteSelection').options].map(o=>o.value).includes('E2')")
            assert await evaluate("JSON.stringify(trackInsight.analysis.overview)")==before_metrics
            await reload_records()
            assert await evaluate("[...document.getElementById('quickSelection').options].some(o=>o.value==='E2')")
            assert await evaluate("[...document.getElementById('favoriteSelection').options].map(o=>o.value).includes('E2')")
            # Storage refusal does not break the session or export/recovery controls.
            favorite_before=await evaluate("document.getElementById('toggleFavorite').getAttribute('aria-pressed')")
            await evaluate("window.reviewOriginalSetItem=Storage.prototype.setItem;Storage.prototype.setItem=function(){throw new DOMException('blocked','SecurityError')}")
            try:
                await evaluate("document.getElementById('toggleFavorite').click();document.getElementById('manageReviewRecords').click()")
                assert '无法保存' in await evaluate("document.getElementById('reviewRecordsStatus').textContent")
                assert await evaluate("document.getElementById('toggleFavorite').getAttribute('aria-pressed')")!=favorite_before
                assert not await evaluate("document.getElementById('exportReviewRecords').disabled")
            finally:
                await evaluate('Storage.prototype.setItem=window.reviewOriginalSetItem;delete window.reviewOriginalSetItem')
        finally:
            await evaluate("(()=>{for(const k of Object.keys(localStorage))if(k.startsWith('trackInsight:'))localStorage.removeItem(k);for(const [k,v] of Object.entries("+json.dumps(saved_storage)+"))localStorage.setItem(k,v);})()")
            await reload_records()
        initial=await evaluate("({title:document.title,paths:trackInsight.analysis.plans.length,time:trackInsight.time(10),traces:document.getElementById('velocity').data.length})")
        await call("Emulation.setTimezoneOverride",{"timezoneId":"America/Los_Angeles"})
        assert await evaluate("trackInsight.time(10)")==initial["time"],"Viewer timezone changed labels"
        await evaluate("trackInsight.select([trackInsight.analysis.paths[Math.min(8,trackInsight.analysis.paths.length-1)]],'acceptance')")
        await evaluate("trackInsight.setCursor(trackInsight.analysis.paths[Math.min(8,trackInsight.analysis.paths.length-1)].start+1)")
        assert await evaluate("document.getElementById('trajectory').data.at(-2).x.length>0"),"Cursor position absent"
        await asyncio.sleep(.3)
        assert await evaluate("Array.from(document.querySelector('#trajectoryPanel .panelTitle').querySelectorAll('button,input,select,summary')).map(e=>e.id)")==['exportTrajectory']
        assert await evaluate("document.getElementById('trajectory').data.some(t=>t.name==='规划') && document.getElementById('trajectory').data.some(t=>t.name==='实际')")
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
                          "playback":True,"report_overview":True,"favorites_add_remove_reload_navigate":True,"review_records_restore_import_export":True,"report_outcome_filter":True,"comprehensive_delete_persist_report_preserved":True,"step_50ms_both_modes":True,"step_pauses_and_clamps":True,"range_link":True,"review_mode":True,"shared_cursor":True,
                          "navigation_cases":sorted(tested),"state_preserved":True,"viewports":["1366x768","1920x1080"],"errors":len(errors)},ensure_ascii=False))
asyncio.run(main())
