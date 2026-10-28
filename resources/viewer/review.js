// Layout and instantaneous readouts share the existing plots and time cursor.
'use strict';
(() => {
  const $=id=>document.getElementById(id),graphs=['trajectory','velocity','errors'];
  document.addEventListener('trackinsight:ready',()=>{
    const api=window.trackInsight,body=document.body,dock=$('playbackDock');
    let reportScroll=0,resizeFrame=0;
    const selection=$('quickSelection');
    for(const button of $('navigation').querySelectorAll('button')) {
      button.dataset.report=button.firstChild.textContent.trim();
      const path=api.analysis.paths.find(p=>p.interval.id===button.dataset.key);
      const task=api.analysis.tasks.find(t=>t.id===(path?.interval.navigation_id||button.dataset.key));
      button.dataset.review=path?.interval.plan_id||button.dataset.report;
      button.dataset.reportExcluded=String(['replaced','failed'].includes(task?.outcome));
      selection.add(new Option(button.dataset.report,button.dataset.key));
    }
    function updateSelectionButtons() {
      $('deleteSelection').disabled=selection.selectedIndex<0;
      $('previousSelection').disabled=selection.selectedIndex<=0;
      $('nextSelection').disabled=selection.selectedIndex<0||selection.selectedIndex>=selection.options.length-1;
    }
    selection.value=api.selected;
    updateSelectionButtons();
    for(const [id,delta] of [['previousSelection',-1],['nextSelection',1]])$(id).onclick=()=>{
      const index=selection.selectedIndex+delta;
      if(index<0||index>=selection.options.length)return;
      selection.selectedIndex=index;selection.dispatchEvent(new Event('change'));
    };
    selection.onchange=()=>{
      updateSelectionButtons();
      const button=[...$('navigation').querySelectorAll('button')].find(b=>b.dataset.key===selection.value);
      if(button&&!button.hidden)button.click();
    };
    $('deleteSelection').onclick=()=>{
      if(!body.classList.contains('review-mode')||selection.selectedIndex<0)return;
      records.hide(api.selected);
    };
    const fields=[['position',['x (m)','y (m)','朝向 (rad)']],
      ['command',['vx (m/s)','vy (m/s)','ω (rad/s)']],['errors',['横向 (m)','路径距离 (m)','姿态 (rad)']]];
    for(const [key,labels] of fields) {
      const group=document.createElement('div');group.className='instantGroup';
      const title=document.createElement('div');title.className='instantTitle';
      title.textContent={position:'当前位置',command:'指令与反馈',errors:'当前跟踪误差'}[key];group.append(title);
      const table=document.createElement('table'),head=document.createElement('tr');
      if(key==='command')head.append(document.createElement('th'));
      for(const text of labels){const cell=document.createElement('th');cell.textContent=text;head.append(cell);}
      table.append(head);
      for(const row of key==='command'?['command','feedback']:[key]) {
        const tr=document.createElement('tr');
        if(key==='command'){const cell=document.createElement('th');cell.textContent=row==='command'?'指令':'反馈';cell.className=row;tr.append(cell);}
        for(let i=0;i<3;i++){const cell=document.createElement('td'),value=document.createElement('output');value.id='instant-'+row+'-'+i;value.textContent='—';cell.append(value);tr.append(cell);}
        table.append(tr);
      }
      group.append(table);$('instantReadouts').append(group);
    }
    document.addEventListener('trackinsight:cursor',event=>{
      const values=event.detail,stamp=BigInt(values.ns),magnitude=stamp<0n?-stamp:stamp;
      $('cursorTimestamp').textContent='Timestamp: '+(stamp<0n?'-':'')+magnitude/1000000000n+'.'+
        (magnitude%1000000000n/1000000n).toString().padStart(3,'0');
      selection.value=api.selected;updateSelectionButtons();
      for(const row of ['position','command','feedback','errors'])for(let i=0;i<3;i++) {
        const value=values[row]?.[i];$('instant-'+row+'-'+i).textContent=Number.isFinite(value)?value.toFixed(3):'—';
      }
    });
    async function resizePlots() {
      if(body.classList.contains('report-overview'))return;
      await Promise.all(graphs.map(id=>Plotly.Plots.resize($(id))));
      const count=body.classList.contains('review-mode')&&!body.classList.contains('velocity-maximized')?3:6;
      for(const id of ['velocity','errors']) {
        const patch={};
        for(let i=1;i<=3;i++) {
          const key='xaxis'+(i===1?'':i),bounds=$(id).layout[key].range;
          const values=Array.from({length:count},(_,n)=>bounds[0]+(bounds[1]-bounds[0])*n/(count-1));
          patch[key+'.tickvals']=values;patch[key+'.ticktext']=values.map(t=>api.time(t,false));
        }
        await Plotly.relayout($(id),patch);
      }
      api.setCursor(api.cursor);
    }
    function scheduleResize() {
      cancelAnimationFrame(resizeFrame);resizeFrame=requestAnimationFrame(()=>resizePlots().catch(error=>{
        $('failureBanner').hidden=false;$('failureBanner').textContent='图表布局更新失败：'+error.message;
      }));
    }
    const velocityView=$('velocityView'),velocityPanel=$('velocityPanel'),maximizeVelocity=$('maximizeVelocity');
    const velocityParent=velocityPanel.parentNode,velocityNext=velocityPanel.nextSibling;
    let velocityScroll=0;
    maximizeVelocity.onclick=()=>{
      if(velocityView.open){velocityView.close();return;}
      velocityScroll=window.scrollY;
      if($('play').textContent==='暂停')$('play').click();
      velocityView.append(velocityPanel);body.classList.add('velocity-maximized');
      maximizeVelocity.textContent='返回';maximizeVelocity.setAttribute('aria-expanded','true');
      velocityView.showModal();scheduleResize();
    };
    velocityView.addEventListener('close',()=>{
      velocityParent.insertBefore(velocityPanel,velocityNext);body.classList.remove('velocity-maximized');
      maximizeVelocity.textContent='最大化';maximizeVelocity.setAttribute('aria-expanded','false');
      maximizeVelocity.focus({preventScroll:true});window.scrollTo(0,velocityScroll);scheduleResize();
    });
    new ResizeObserver(scheduleResize).observe(velocityView);
    $('toggleSidebar').onclick=()=>{
      const collapsed=body.classList.toggle('sidebar-collapsed');
      $('toggleSidebar').textContent=collapsed?'展开导航':'收起导航';
      $('toggleSidebar').setAttribute('aria-expanded',String(!collapsed));scheduleResize();
    };
    function updateNavigation(review,fallbackIndex=0) {
      const labelMode=review?'review':'report';
      $('selectionTitle').textContent=$('selectionTitle').dataset[labelMode];
      selection.replaceChildren();
      for(const button of $('navigation').querySelectorAll('button')) {
        button.firstChild.textContent=button.dataset[labelMode];
        button.hidden=button.dataset.reportExcluded==='true'||(review&&records.hidden.has(button.dataset.key));
        if(!button.hidden)selection.add(new Option(button.dataset[labelMode],button.dataset.key));
      }
      for(const group of $('navigation').querySelectorAll('details.navigationTask')) {
        group.hidden=[...group.querySelectorAll('button')].every(button=>button.hidden);
      }
      selection.value=api.selected;
      selection.disabled=selection.options.length===0;
      if(selection.selectedIndex<0) {
        if(selection.options.length){selection.selectedIndex=Math.min(fallbackIndex,selection.options.length-1);selection.dispatchEvent(new Event('change'));}
        else api.select([],'empty');
      }
      updateSelectionButtons();records.refresh();
    }
    function setMode(review) {
      if(review===body.classList.contains('review-mode'))return;
      if(review)reportScroll=window.scrollY;
      body.classList.toggle('review-mode',review);
      updateNavigation(review);
      (review?$('reviewWorkspace'):$('trajectoryPanel')).append(dock);
      for(const id of ['trajectoryOptions','velocityOptions'])$(id).open=!review;
      for(const [id,active]of [['reportMode',!review],['reviewMode',review]]) {
        $(id).classList.toggle('active',active);$(id).setAttribute('aria-pressed',String(active));
      }
      document.dispatchEvent(new CustomEvent('trackinsight:mode',{detail:{review}}));
      scheduleResize();
      window.scrollTo(0,review?0:reportScroll);
    }
    $('reportMode').onclick=()=>setMode(false);$('reviewMode').onclick=()=>setMode(true);
    new ResizeObserver(scheduleResize).observe($('chartDeck'));
    const records=window.createReviewRecords(api,()=>updateNavigation(body.classList.contains('review-mode'),Math.max(0,selection.selectedIndex)));
    records.refresh();
    setMode(true);
  },{once:true});
})();
