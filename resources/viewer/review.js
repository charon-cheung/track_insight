// Layout and instantaneous readouts share the existing plots and time cursor.
'use strict';
(() => {
  const $=id=>document.getElementById(id),graphs=['trajectory','velocity','errors'];
  function initFavorites(api) {
    const buttons=[...$('navigation').querySelectorAll('button')];
    const entries=new Map(buttons.map(button=>[button.dataset.key,button]));
    const star=$('toggleFavorite'),picker=$('favoriteSelection');
    const meta=api.analysis.meta;
    const storageKey='trackInsight:favorites:v1:'+JSON.stringify([meta.input_sha256,meta.start_ns,meta.end_ns]);
    let favorites=new Set(),storageUnavailable=false,lastSelected;
    try {
      const saved=JSON.parse(localStorage.getItem(storageKey)||'[]');
      if(Array.isArray(saved))favorites=new Set(saved.filter(key=>typeof key==='string'&&entries.has(key)));
    } catch(error) {storageUnavailable=error.name!=='SyntaxError';}
    function syncFavorite() {
      lastSelected=api.selected;
      const active=favorites.has(api.selected),label=active?'取消星标':'加入星标';
      star.disabled=!entries.has(api.selected)||entries.get(api.selected).hidden;
      star.classList.toggle('starred',active);star.setAttribute('aria-pressed',String(active));
      star.setAttribute('aria-label',label);
      star.title=label+(storageUnavailable?'（无法保存到浏览器，星标仅本次有效）':'');
      picker.value=active?api.selected:'';
    }
    function renderFavorites() {
      picker.replaceChildren(new Option('选择星标',''));
      for(const [key,button] of entries)if(favorites.has(key)&&!button.hidden)picker.add(new Option(button.dataset.review,key));
      $('favoritePicker').hidden=picker.options.length===1;
      syncFavorite();
    }
    star.onclick=()=>{
      if(!entries.has(api.selected)||entries.get(api.selected).hidden)return;
      if(favorites.has(api.selected))favorites.delete(api.selected);else favorites.add(api.selected);
      try {localStorage.setItem(storageKey,JSON.stringify([...favorites]));storageUnavailable=false;}
      catch {storageUnavailable=true;}
      renderFavorites();
    };
    picker.onchange=()=>{
      if(!favorites.has(picker.value)||entries.get(picker.value)?.hidden)return;
      $('quickSelection').value=picker.value;
      $('quickSelection').dispatchEvent(new Event('change'));
    };
    document.addEventListener('trackinsight:cursor',()=>{
      if(lastSelected!==api.selected)syncFavorite();
    });
    renderFavorites();
    return renderFavorites;
  }
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
    const meta=api.analysis.meta;
    const hiddenStorageKey='trackInsight:hidden:v1:'+JSON.stringify([meta.input_sha256,meta.start_ns,meta.end_ns]);
    let removedSelections=new Set();
    try {
      const saved=JSON.parse(localStorage.getItem(hiddenStorageKey)||'[]');
      const keys=new Set([...selection.options].map(option=>option.value));
      if(Array.isArray(saved))removedSelections=new Set(saved.filter(key=>typeof key==='string'&&keys.has(key)));
    } catch { /* Keep the report usable when browser storage is unavailable. */ }
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
      const index=selection.selectedIndex;removedSelections.add(api.selected);
      try {localStorage.setItem(hiddenStorageKey,JSON.stringify([...removedSelections]));}
      catch {$('deleteSelection').title='浏览器无法保存隐藏记录，本次刷新后可能恢复';}
      updateNavigation(true,index);
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
        button.hidden=button.dataset.reportExcluded==='true'||(review&&removedSelections.has(button.dataset.key));
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
      updateSelectionButtons();refreshFavorites();
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
    const refreshFavorites=initFavorites(api);
    setMode(true);
  },{once:true});
})();
