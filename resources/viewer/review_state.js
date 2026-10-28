// Per-run review preferences; analysis data and report metrics are never changed.
'use strict';
window.createReviewRecords=(api,onChange)=>{
  const $=id=>document.getElementById(id),meta=api.analysis.meta;
  const identity=[meta.input_sha256,meta.start_ns,meta.end_ns];
  const entries=new Map([...$('navigation').querySelectorAll('button')].map(b=>[b.dataset.key,b]));
  const keys={favorites:'trackInsight:favorites:v1:'+JSON.stringify(identity),hidden:'trackInsight:hidden:v1:'+JSON.stringify(identity)};
  let storageUnavailable=false,lastSelected;
  function load(key) {
    try {const saved=JSON.parse(localStorage.getItem(key)||'[]');
      return new Set(Array.isArray(saved)?saved.filter(v=>typeof v==='string'&&entries.has(v)):[]);
    } catch(error){storageUnavailable=error.name!=='SyntaxError';return new Set();}
  }
  const favorites=load(keys.favorites),hidden=load(keys.hidden);
  const node=(tag,text)=>{const e=document.createElement(tag);if(text!==undefined)e.textContent=text;return e;};
  const dialog=node('dialog');dialog.id='reviewRecordsDialog';dialog.setAttribute('aria-labelledby','reviewRecordsTitle');
  const heading=node('div');heading.className='recordsHeading';
  const title=node('h2','复盘记录');title.id='reviewRecordsTitle';
  const close=node('button','关闭');close.type='button';close.onclick=()=>dialog.close();heading.append(title,close);
  const note=node('p','隐藏仅影响综合模式，报告统计不变。导入会合并同一轮测试的星标与隐藏项。');
  const counts=node('p'),actions=node('div');actions.className='recordsActions';
  const exportButton=node('button','导出记录'),importButton=node('button','导入记录'),restoreAll=node('button','恢复全部隐藏项');
  exportButton.id='exportReviewRecords';importButton.id='importReviewRecords';restoreAll.id='restoreAllHidden';
  for(const button of [exportButton,importButton,restoreAll]){button.type='button';actions.append(button);}
  const input=node('input');input.type='file';input.accept='.json,application/json';input.hidden=true;input.id='reviewRecordsFile';
  const status=node('p');status.id='reviewRecordsStatus';status.setAttribute('role','status');
  const list=node('div');list.id='hiddenReviewRecords';
  dialog.append(heading,note,counts,actions,input,status,node('h3','已隐藏的项目'),list);document.body.append(dialog);
  const star=$('toggleFavorite'),picker=$('favoriteSelection');
  function message(text) {
    status.textContent=text+(storageUnavailable?' 浏览器无法保存记录，本次操作仍有效，请导出备份。':'');
  }
  function save() {
    try {
      localStorage.setItem(keys.favorites,JSON.stringify([...favorites]));
      localStorage.setItem(keys.hidden,JSON.stringify([...hidden]));storageUnavailable=false;
    } catch {storageUnavailable=true;}
  }
  function refresh() {
    lastSelected=api.selected;
    const active=favorites.has(api.selected),label=active?'取消星标':'加入星标';
    star.disabled=!entries.has(api.selected)||entries.get(api.selected).hidden;
    star.classList.toggle('starred',active);star.setAttribute('aria-pressed',String(active));
    star.setAttribute('aria-label',label);star.title=label+(storageUnavailable?'（仅本次有效，请导出复盘记录）':'');
    picker.replaceChildren(new Option('选择星标',''));
    for(const [key,button] of entries)if(favorites.has(key)&&!button.hidden)picker.add(new Option(button.dataset.review,key));
    $('favoritePicker').hidden=picker.options.length===1;picker.value=active?api.selected:'';
    counts.textContent='星标 '+favorites.size+' 项 · 隐藏 '+hidden.size+' 项';
    restoreAll.disabled=hidden.size===0;
    list.replaceChildren();
    for(const [key,button] of entries)if(hidden.has(key)) {
      const row=node('div');row.className='hiddenRecord';row.dataset.key=key;
      const restore=node('button','恢复');restore.type='button';restore.setAttribute('aria-label','恢复 '+button.dataset.review);
      restore.onclick=()=>{hidden.delete(key);changed('已恢复 '+button.dataset.review+'。');};
      row.append(node('span',button.dataset.review+(favorites.has(key)?' ★':'')),restore);list.append(row);
    }
    if(!hidden.size)list.append(node('p','没有隐藏项目。'));
  }
  function changed(text) {save();onChange();refresh();message(text);}
  function hide(key) {
    if(!entries.has(key))return;
    hidden.add(key);changed('已隐藏 '+entries.get(key).dataset.review+'，可在此恢复。');
  }
  star.onclick=()=>{
    if(star.disabled)return;
    if(favorites.has(api.selected))favorites.delete(api.selected);else favorites.add(api.selected);
    changed('星标已更新。');
  };
  picker.onchange=()=>{
    if(!favorites.has(picker.value)||entries.get(picker.value)?.hidden)return;
    $('quickSelection').value=picker.value;$('quickSelection').dispatchEvent(new Event('change'));
  };
  restoreAll.onclick=()=>{hidden.clear();changed('已恢复全部隐藏项。');};
  exportButton.onclick=()=>{
    const record={schema_version:1,type:'track_insight_review_records',source:{input_sha256:identity[0],start_ns:identity[1],end_ns:identity[2]},favorites:[...favorites],hidden:[...hidden]};
    const url=URL.createObjectURL(new Blob([JSON.stringify(record,null,2)],{type:'application/json'}));
    const link=node('a');link.href=url;link.download='Track_Insight_review_'+meta.input_sha256.slice(0,12)+'.json';link.click();
    setTimeout(()=>URL.revokeObjectURL(url),1000);message('已导出本轮星标与隐藏记录。');
  };
  importButton.onclick=()=>{input.value='';input.click();};
  input.onchange=async()=>{
    const file=input.files[0];if(!file)return;
    try {
      if(file.size>1024*1024)throw new Error('文件超过 1 MiB，请选择导出的复盘记录 JSON。');
      const record=JSON.parse(await file.text());
      if(!record||record.schema_version!==1||record.type!=='track_insight_review_records')throw new Error('不是受支持的复盘记录文件。');
      const source=record.source;
      if(!source||source.input_sha256!==identity[0]||source.start_ns!==identity[1]||source.end_ns!==identity[2])throw new Error('记录属于另一轮测试，未导入任何项目。');
      for(const key of ['favorites','hidden'])if(!Array.isArray(record[key])||record[key].some(v=>typeof v!=='string'||!entries.has(v)))throw new Error('记录包含本报告中不存在的项目，未导入任何项目。');
      for(const key of record.favorites)favorites.add(key);
      for(const key of record.hidden)hidden.add(key);
      changed('导入完成，已合并星标与隐藏项。');
    } catch(error){message('导入失败：'+error.message);}
    finally {input.value='';}
  };
  $('manageReviewRecords').onclick=()=>{
    if($('play').textContent==='暂停')$('play').click();
    refresh();message('');dialog.showModal();
  };
  document.addEventListener('trackinsight:cursor',()=>{if(lastSelected!==api.selected)refresh();});
  return {hidden,hide,refresh};
};
