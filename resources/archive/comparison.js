'use strict';
(() => {
  const $=id=>document.getElementById(id),root=$('roundComparison');
  if(!$('error').hidden)return;
  const node=(tag,text,cls)=>{const e=document.createElement(tag);if(text!==undefined)e.textContent=text;if(cls)e.className=cls;return e;};
  const fmt=value=>Number.isFinite(value)&&value>=0?value.toFixed(4):'—';
  const hidden=new Set(['controller_frequency','failure_tolerance','publish_zero_velocity']);
  const visibleKey=key=>!hidden.has(key)&&key!=='plugin'&&!key.endsWith('.plugin');
  const label=key=>key.replace(/^PidTrackingController\./,'');
  const owns=(object,key)=>Object.prototype.hasOwnProperty.call(object,key);
  const snapshot=run=>{
    const s=run?.pid_parameters;
    return s?.schema_version===2&&s.source_kind==='navigation_log'&&s.status==='available'&&
      s.values&&typeof s.values==='object'&&!Array.isArray(s.values)&&Object.keys(s.values).length?s:null;
  };
  const valueText=value=>typeof value==='number'?String(value):JSON.stringify(value);
  const metric=run=>{
    const m=run?.overview?.averages?.lateral,value=m?.[$('archiveTrendMetric').value];
    return Number.isFinite(value)&&value>=0&&m.case_count>0?value:null;
  };
  const sameBasis=(a,b)=>!!a?.metric_version&&a.metric_version===b?.metric_version&&
    !!a.analysis_quality&&a.analysis_quality===b.analysis_quality&&a.analysis_quality!=='unavailable';
  function select(id,text,options,parent) {
    const field=node('label',text),control=node('select');control.id=id;
    for(const [value,title] of options)control.add(new Option(title,value));
    field.append(control);parent.append(field);return control;
  }
  try {
    const runs=JSON.parse($('archiveData').textContent).runs.slice().sort((a,b)=>{
      const x=BigInt(a.start_ns),y=BigInt(b.start_ns);
      return x===y?(a.id<b.id?-1:a.id>b.id?1:0):x>y?-1:1;
    });
    const title=node('div',undefined,'panelTitle');title.append(node('h2','跨轮参数与精度对比'));root.append(title);
    const body=node('div',undefined,'archiveComparisonBody');root.append(body);
    const controls=node('div',undefined,'archiveCompareControls');body.append(controls);
    const range=select('archiveTrendRange','显示范围',[['20','最近 20 轮'],['50','最近 50 轮'],['all','全部轮次']],controls);
    const metricChoice=select('archiveTrendMetric','指标',[['case_mean_rmse','平均横向 RMSE'],['pooled_rmse','整体横向 RMSE']],controls);
    const keys=[...new Set(runs.flatMap(r=>Object.keys(snapshot(r)?.values||{})))].filter(visibleKey).sort();
    const parameter=select('archiveTrendParameter','关注参数',[['','不选择参数'],...keys.map(key=>[key,label(key)])],controls);
    const chart=node('div',undefined,'archiveTrendScroll');chart.id='archiveTrend';body.append(chart);
    const pointInfo=node('p');pointInfo.id='archiveTrendPoint';pointInfo.setAttribute('aria-live','polite');body.append(pointInfo);
    const note=node('p','横轴按测试轮次等距排列；缺失指标保留空位，不计为零。指标版本或证据等级不同的相邻轮次不连线。','archiveCompareNote');body.append(note);
    const pair=node('div',undefined,'archiveCompareControls');body.append(pair);
    const options=runs.map(r=>[r.id,r.name+' · '+r.start_label]);
    const baseline=select('archiveCompareBaseline','基准轮',options,pair),target=select('archiveCompareTarget','对比轮',options,pair);
    if(runs.length>1)baseline.value=runs[1].id;
    const onlyLabel=node('label',undefined,'archiveChangedFilter'),only=node('input');only.type='checkbox';only.id='archiveOnlyChanges';
    onlyLabel.append(only,document.createTextNode('只看变化参数'));pair.append(onlyLabel);
    const summary=node('div',undefined,'archiveCompareSummary');summary.id='archiveCompareSummary';body.append(summary);
    const context=node('p');context.id='archiveCompareContext';body.append(context);
    const status=node('p');status.id='archiveParameterStatus';status.setAttribute('aria-live','polite');body.append(status);
    const scroll=node('div',undefined,'tableScroll'),table=node('table'),head=node('thead'),headRow=node('tr'),rows=node('tbody');
    rows.id='archiveParameterRows';
    for(const text of ['参数','基准轮值','对比轮值','变化'])headRow.append(node('th',text));
    head.append(headRow);table.append(head,rows);scroll.append(table);body.append(scroll);
    body.append(node('p','这里使用各轮归档时保存的指标与运行参数；单轮网页中的本地删除记录不会改写归档。路径、速度和纳入 case 不同也会影响结果，指标变化不能单独归因于 PID 参数。','archiveCompareNote'));
    const find=id=>runs.find(r=>r.id===id);
    const describe=(run,index)=>{
      const m=metric(run),key=parameter.value,s=snapshot(run);
      let text=(index===undefined?'':(index+1)+'. ')+run.name+' · '+run.start_label+' · '+metricChoice.selectedOptions[0].textContent+' '+fmt(m)+' m';
      text+=' · 可评价 '+run.overview.ranked_cases+'/'+run.overview.case_ids.length+' case · 指标版本 '+run.metric_version;
      if(key)text+=' · '+label(key)+'：'+(!s?'测试时参数未知':owns(s.values,key)?valueText(s.values[key]):'快照未包含此参数');
      return text;
    };
    function drawTrend() {
      chart.replaceChildren();pointInfo.textContent='';
      const displayed=(range.value==='all'?runs:runs.slice(0,Number(range.value))).slice().reverse();
      if(!displayed.length){chart.append(node('p','还没有可对比的测试轮次。','empty'));return;}
      const ns='http://www.w3.org/2000/svg';
      const svg=(tag,attrs,text)=>{const e=document.createElementNS(ns,tag);for(const [k,v] of Object.entries(attrs))e.setAttribute(k,v);if(text!==undefined)e.textContent=text;return e;};
      const plot=svg('svg',{viewBox:'0 0 1100 300',role:'group','aria-label':metricChoice.selectedOptions[0].textContent+'跨轮趋势，单位米'});
      chart.append(plot);const values=displayed.map(metric),maximum=values.reduce((max,v)=>Math.max(max,v??0),.001)*1.15;
      const x=i=>displayed.length===1?575:80+i*990/(displayed.length-1),y=v=>245-v/maximum*200;
      for(let i=0;i<=4;++i){const v=maximum*i/4;
        plot.append(svg('line',{x1:80,y1:y(v),x2:1070,y2:y(v),stroke:'#e2e7ef'}),svg('text',{x:68,y:y(v)+4,'text-anchor':'end'},v.toFixed(3)));
      }
      plot.append(svg('text',{x:15,y:22},'RMSE (m)'));
      displayed.forEach((run,i)=>{
        const v=values[i],previous=values[i-1];
        if(i&&v!==null&&previous!==null&&sameBasis(displayed[i-1],run))
          plot.append(svg('line',{x1:x(i-1),y1:y(previous),x2:x(i),y2:y(v),stroke:'#2473d6','stroke-width':2,'pointer-events':'none','data-trend-segment':run.id}));
        if(i===0||i===displayed.length-1||i%Math.ceil(displayed.length/6)===0)
          plot.append(svg('text',{x:x(i),y:280,'text-anchor':'middle'},run.name.length>14?run.name.slice(0,13)+'…':run.name));
        const group=svg('g',{'data-trend-run':run.id,role:'button',tabindex:0,'aria-label':describe(run,i),'aria-pressed':String(target.value===run.id)});
        group.append(svg('title',{},describe(run,i)));
        if(v===null)group.append(svg('text',{x:x(i),y:241,'text-anchor':'middle',class:'archiveMissingMetric'},'×'));
        else group.append(svg('circle',{cx:x(i),cy:y(v),r:target.value===run.id?7:5,fill:target.value===run.id?'#db5143':'#2473d6'}));
        const choose=()=>{target.value=run.id;drawTrend();compare();chart.querySelectorAll('[data-trend-run]')[i].focus();};
        group.addEventListener('click',choose);
        group.addEventListener('keydown',event=>{if(event.key==='Enter'||event.key===' '){event.preventDefault();choose();}});
        for(const event of ['mouseenter','focus'])group.addEventListener(event,()=>{pointInfo.textContent=describe(run,i);});
        plot.append(group);
      });
      pointInfo.textContent='显示 '+displayed.length+' 轮。选择图上的轮次可切换对比轮；悬停或聚焦可查看指标和关注参数。';
    }
    function compare() {
      summary.replaceChildren();rows.replaceChildren();
      const a=find(baseline.value),b=find(target.value);only.disabled=!snapshot(a)||!snapshot(b);
      if(!a||!b){status.textContent='还没有可比较的参数快照。';context.textContent='';return;}
      const before=metric(a),after=metric(b),comparable=sameBasis(a,b)&&before!==null&&after!==null;
      for(const [text,run,value] of [['基准轮',a,before],['对比轮',b,after]]) {
        const card=node('div',undefined,'card'),link=node('a',text+'：'+run.name);
        if(/^runs\/run_-?\d+_[a-f0-9]{12}\/report\.html$/.test(run.report))link.href=run.report;
        card.append(link,node('strong',fmt(value)+' m'));summary.append(card);
      }
      const delta=node('div',undefined,'card');delta.append(node('span','差值（对比轮 − 基准轮）'));
      const difference=after-before;
      delta.append(node('strong',comparable?(difference>0?'+':'')+difference.toFixed(4)+' m':'不可直接比较'));summary.append(delta);
      context.textContent='基准：'+a.overview.ranked_cases+'/'+a.overview.case_ids.length+' case，'+a.metric_version+'，'+a.analysis_quality+
        '；对比：'+b.overview.ranked_cases+'/'+b.overview.case_ids.length+' case，'+b.metric_version+'，'+b.analysis_quality+
        (comparable?'。':'。指标缺失、版本或证据等级不同，不计算差值。');
      const left=snapshot(a),right=snapshot(b),current=right?.values||{},previous=left?.values||{};
      const hasBoth=!!left&&!!right;
      status.textContent=hasBoth?'参数来自两轮对应导航日志的运行快照。灰色行表示修改、新增或删除。':
        [!left?'基准轮测试时参数未知：'+(a.pid_parameters?.reason||'没有可信运行参数快照。'):'',
         !right?'对比轮测试时参数未知：'+(b.pid_parameters?.reason||'没有可信运行参数快照。'):''].filter(Boolean).join(' ');
      for(const key of [...new Set([...Object.keys(previous),...Object.keys(current)])].filter(visibleKey).sort()) {
        const was=owns(previous,key),is=owns(current,key),changed=hasBoth&&(!was||!is||JSON.stringify(previous[key])!==JSON.stringify(current[key]));
        if(hasBoth&&only.checked&&!changed)continue;
        const state=!hasBoth?'不可比较':!was?'新增':!is?'已删除':changed?'已修改':'未修改';
        const row=node('tr');row.dataset.parameter=key;if(changed)row.className='archiveParameterChanged';
        for(const text of [label(key),!left?'未知':was?valueText(previous[key]):'—',!right?'未知':is?valueText(current[key]):'—',state])row.append(node('td',text));
        rows.append(row);
      }
      if(!rows.children.length){const row=node('tr'),cell=node('td',hasBoth?'没有符合筛选条件的参数。':'没有可显示的可信运行参数。');cell.colSpan=4;row.append(cell);rows.append(row);}
    }
    for(const control of [range,metricChoice,parameter,baseline,target])control.onchange=()=>{drawTrend();compare();};
    only.onchange=compare;
    for(const control of [range,metricChoice,parameter,baseline,target,only])if(!runs.length)control.disabled=true;
    root.hidden=false;drawTrend();compare();
  } catch(error) {
    root.hidden=false;root.append(node('p','跨轮对比无法显示：'+error.message,'archiveCompareError'));
  }
})();
