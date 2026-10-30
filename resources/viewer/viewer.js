'use strict';
(() => {
  const A=JSON.parse(document.getElementById('analysis').textContent);
  const style=JSON.parse(document.getElementById('plotStyle').textContent),trajectoryStyle=style.trajectory;
  const velocityStyles=style.velocity.series;
  const $=id=>document.getElementById(id), origin=BigInt(A.clock.origin_ns);
  const name={goal_position_error:'任务终点位置误差',goal_yaw_error:'任务终点姿态误差',stopped_residual_error:'停车残差',stopped_drift:'停车漂移',settling_time:'结果后稳定时间',lateral:'横向误差',path_distance:'路径距离',endpoint_distance:'路径终点距离',
    yaw_error:'目标姿态误差',vx_residual:'vx 反馈 − 指令',vy_residual:'vy 反馈 − 指令',
    omega_residual:'ω 反馈 − 指令',speed_vector_residual:'平面速度残差'};
  const velocityControls=Object.fromEntries([...$('velocityOptions').querySelectorAll('input')].map(input=>[input.dataset.velocityGroup,input]));
  for(const s of velocityStyles)velocityControls[s.key].checked=s.default_visible;
  const fmt=(v,d=3)=>v===null||v===undefined?'—':Number(v).toFixed(d);
  const ns=t=>origin+BigInt(Math.round(t*1e9));
  const rel=s=>Number(BigInt(s)-origin)/1e9;
  function groupedTimestamp(t,spacing=2) {
    const stamp=ns(t),magnitude=stamp<0n?-stamp:stamp;
    const seconds=(magnitude/1000000000n).toString();
    const grouped=seconds.length>3?seconds.slice(0,-3)+'\u00a0'.repeat(spacing)+seconds.slice(-3):seconds;
    return (stamp<0n?'-':'')+grouped+'.'+(magnitude%1000000000n/1000000n).toString().padStart(3,'0');
  }
  function time(t,full=true) {
    if(!A.clock.wall_clock)return (t<0?'-':'+')+(Math.trunc(Math.abs(t)*1000)/1000).toFixed(3)+' s';
    const stamp=ns(t);let offset=0;
    for(const o of A.clock.offsets)if(stamp>=BigInt(o.from_ns))offset=o.seconds;
    const date=new Date(Number(stamp/1000000n)+offset*1000).toISOString();
    return full?date.slice(0,10)+' '+date.slice(11,23):date.slice(11,23);
  }
  let selected=[],selectedKey='',range=[0,1],cursor=0,playing=false,lastFrame=0,linked=false,ready=false,cursorPinned=false;
  const config={responsive:true,displaylogo:false,scrollZoom:true,
    modeBarButtonsToRemove:['lasso2d','select2d','sendDataToCloud'],toImageButtonOptions:{format:'png',scale:2}};
  const common={font:{family:'Inter, Noto Sans CJK SC, sans-serif',size:11,color:'#617188'},
    paper_bgcolor:'white',plot_bgcolor:'white',margin:{l:68,r:30,t:24,b:45},
    legend:{orientation:'h',y:1.1,x:0},hovermode:'x unified',dragmode:'zoom'};
  function ticks(bounds) {
    const count=document.body.classList.contains('review-mode')&&!document.body.classList.contains('velocity-maximized')?3:6;
    const values=Array.from({length:count},(_,i)=>bounds[0]+(bounds[1]-bounds[0])*i/(count-1));
    return {tickmode:'array',tickvals:values,ticktext:values.map(t=>time(t,false)),gridcolor:'#e8edf5',zeroline:false};
  }
  function badgeText(p) {
    const i=p.interval;return i.identity==='confirmed'&&i.start_quality==='confirmed'&&i.end_quality==='confirmed'?'已确认执行区间':'';
  }
  function button(label,small,key,paths,group=false,parent=$('navigation')) {
    const b=document.createElement('button');b.className=group?'navGroup':'';
    b.dataset.key=key;b.append(document.createTextNode(label));
    if(small){const s=document.createElement('small');s.textContent=small;b.append(s);}
    b.onclick=()=>select(paths,key);parent.append(b);
  }
  for(const task of A.tasks) {
    const paths=A.paths.filter(p=>p.interval.navigation_id===task.id);
    if(paths.length===1) {
      const p=paths[0];button(task.id+' · '+p.interval.plan_id+' · '+task.outcome,time(p.start,false),p.interval.id,paths,true);
    } else if(paths.length>1) {
      const group=document.createElement('details'),heading=document.createElement('summary');
      group.className='navigationTask';heading.title='展开或收起路径版本';group.append(heading);$('navigation').append(group);
      if(task.path_filter_applied)heading.textContent=task.id+' · '+task.outcome+' · '+paths.length+' 条非直线路径';
      else button(task.id+' · '+task.outcome,time(rel(task.start_ns),false)+' · '+paths.length+' 条路径',task.id,paths,true,heading);
      for(const p of paths)button(p.interval.plan_id,time(p.start,false),p.interval.id,[p],false,group);
    } else button(task.id+' · '+task.outcome,time(rel(task.start_ns),false)+' · 无可评价路径',task.id,paths,true);
  }
  const unassigned=A.paths.filter(p=>!p.interval.navigation_id);
  for(const p of unassigned)button(p.interval.plan_id,time(p.start,false)+' · '+(p.end-p.start).toFixed(1)+' s',p.interval.id,[p]);
  for(const p of A.plans.filter(p=>!p.valid)){const div=document.createElement('p');div.textContent=p.id+' · 不可评价：'+p.reason;$('navigation').append(div);}
  $('pathCount').textContent=A.plans.length+' P';
  $('runTime').textContent=A.meta.start_label+' → '+A.meta.end_label.slice(11);
  $('evidence').textContent=A.meta.log_compatible?
    '日志证据已关联；路径身份和执行边界分别标注。':'缺少同轮日志证据。P 按记录的路径发布顺序编号，时间窗口不能证明控制器已采用该路径。';
  $('quality').textContent='定位来源：TF · '+A.meta.frame+' → '+A.meta.base+'\n数据质量记录：'+A.diagnostics.length;
  $('footer').textContent='Track Insight '+A.generator.version+' · Metric '+A.metric_version+
    ' · 生成时系统本地时间 · '+A.meta.input_path+' · SHA-256 '+A.meta.input_sha256;
  if(A.generation.status!=='complete'){$('failureBanner').hidden=false;$('failureBanner').textContent='报告部分完成：'+A.generation.failures.join('；')+'。请查看 manifest.json。';}
  const escapeCSV=v=>'"'+String(v??'').replaceAll('"','""')+'"';
  function download(text,name,type) {
    const u=URL.createObjectURL(new Blob([text],{type})),a=document.createElement('a');
    a.href=u;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(u),1000);
  }
  function currentMetrics() {
    const stage=$('stage').value;
    if(A.tasks.some(t=>t.id===selectedKey))return A.navigation_metrics[selectedKey+'/'+$('aggregateQuality').value]||{};
    if(selected.length===1)return stage?selected[0].stages[stage]||{}:selected[0].metrics;
    return A.navigation_metrics[selectedKey+'/inferred']||A.navigation_metrics[selectedKey+'/confirmed']||{};
  }
  function showMetrics() {
    const values=currentMetrics();$('metrics').replaceChildren();$('cards').replaceChildren();
    const coverage=A.tasks.some(t=>t.id===selectedKey)?'导航全程覆盖率':$('stage').value?'阶段区间覆盖率':'路径区间覆盖率';
    $('coverageHeading').textContent=coverage;
    for(const [key,m] of Object.entries(values)) {
      const tr=document.createElement('tr');
      for(const v of [name[key]||key,(m.value??m.rmse)==null?'不可用':fmt(m.value??m.rmse)+' '+m.unit,fmt(m.mae),fmt(m.bias),fmt(m.p95),fmt(m.max),
        fmt(m.exceedance_s),fmt(m.longest_exceedance_s),
        m.coverage===null?'—':fmt(m.coverage*100,1)+'%',fmt(m.valid_s,2)+' / '+fmt(m.candidate_s,2)+' s',
        m.rmse===null&&m.value===null?m.reason:m.quality]){const td=document.createElement('td');td.textContent=v;tr.append(td);}
      tr.title=JSON.stringify(m.excluded);$('metrics').append(tr);
    }
    const groups=[['跟踪精度',[
      ['lateral','rmse','横向误差 RMSE'],['lateral','p95','横向误差 P95'],['lateral','max','最大横向误差']
    ]],['速度执行',[
      ['vx_residual','rmse','vx 反馈 − 指令 RMSE'],['vy_residual','rmse','vy 反馈 − 指令 RMSE'],
      ['omega_residual','rmse','ω 反馈 − 指令 RMSE']
    ]]];
    for(const [title,items] of groups) {
      const group=document.createElement('section');group.className='metricGroup';
      const heading=document.createElement('h2');heading.textContent=title;group.append(heading);
      for(const [key,stat,title] of items) {
        const m=values[key],card=document.createElement('div');card.className='card';
        const label=document.createElement('div');label.className='label';label.textContent=title;
        const value=document.createElement('div');value.className='value';value.textContent=m?.[stat]==null?'不可用':fmt(m[stat]);
        if(m?.[stat]!=null){const unit=document.createElement('span');unit.className='unit';unit.textContent=m.unit;value.append(unit);}
        const detail=document.createElement('div');detail.className='detail';
        detail.textContent=m?.coverage!=null?coverage+' '+fmt(m.coverage*100,1)+'%':'当前范围无可用指标';
        if(m?.coverage!=null)detail.append(document.createElement('br'),'有效 '+fmt(m.valid_s,2)+' s / 区间 '+fmt(m.candidate_s,2)+' s');
        if(stat==='max'&&m?.max!=null&&m.max_ns) {
          const stamp=BigInt(m.max_ns),magnitude=stamp<0n?-stamp:stamp;
          const timestamp=(stamp<0n?'-':'')+magnitude/1000000000n+'.'+
            (magnitude%1000000000n/1000000n).toString().padStart(3,'0');
          detail.append(document.createElement('br'),time(rel(m.max_ns)),document.createElement('br'),'Timestamp: '+timestamp);
        }
        card.append(label,value,detail);group.append(card);
      }
      $('cards').append(group);
    }
  }
  $('downloadMetrics').onclick=()=>{
    const rows=[['plan','metric','value','rmse','mae','bias','p95','max','coverage','valid_s','candidate_s','unit','quality','reason']];
    for(const p of selected)for(const [k,m]of Object.entries(p.metrics))rows.push([p.interval.plan_id,k,...['value','rmse','mae','bias','p95','max','coverage','valid_s','candidate_s','unit','quality','reason'].map(k=>m[k])]);
    download(rows.map(r=>r.map(escapeCSV).join(',')).join('\n'),'track_insight_metrics.csv','text/csv;charset=utf-8');
  };
  // Display-only reduction retains each bucket's extrema plus all validity/gap boundaries.
  function reduce(rows,columns,validIndex,endIndex,limit=2400) {
    if(rows.length<=limit)return rows;
    const keep=new Set([0,rows.length-1]),size=Math.ceil(rows.length/Math.max(1,Math.floor(limit/(columns.length*2+2))));
    for(let start=0;start<rows.length;start+=size) {
      const end=Math.min(rows.length,start+size);keep.add(start);keep.add(end-1);
      for(const col of columns){let low=start,high=start;
        for(let i=start;i<end;i++){if(rows[i][col]<rows[low][col])low=i;if(rows[i][col]>rows[high][col])high=i;}
        keep.add(low);keep.add(high);}
    }
    for(let i=1;i<rows.length;i++)if(rows[i][validIndex]!==rows[i-1][validIndex]||rows[i][0]>rows[i-1][endIndex]+1e-8){keep.add(i-1);keep.add(i);}
    const indices=[...keep].sort((a,b)=>a-b);
    return indices.map((i,n)=>{const copy=rows[i].slice();const next=indices[n+1];
      if(next!==undefined){let contiguous=true;for(let j=i;j<next;j++)if(!rows[j][validIndex]||!rows[j+1][validIndex]||rows[j+1][0]>rows[j][endIndex]+1e-8){contiguous=false;break;}
        if(contiguous)copy[endIndex]=rows[next][0];}
      return copy;});
  }
  function arrays(rows,x,y,valid,end,label=r=>time(r[0])) {
    const xs=[],ys=[],hover=[];let previous=null;
    for(const r of rows) {
      if(!valid(r)){xs.push(null);ys.push(null);hover.push('');previous=null;continue;}
      if(previous!==null&&r[0]>previous+1e-8){xs.push(null);ys.push(null);hover.push('');}
      xs.push(x(r));ys.push(y(r));hover.push(label(r));previous=end(r);
    }
    return {x:xs,y:ys,text:hover,hovertemplate:'%{text}<br>%{y:.4f}<extra>%{fullData.name}</extra>',connectgaps:false};
  }
  async function trajectory() {
    const traces=[];
    const plans=A.plans.filter(p=>selected.some(s=>s.interval.plan_id===p.id));
    for(const [index,p]of plans.entries())traces.push({x:p.poses.map(r=>r[0]),y:p.poses.map(r=>r[1]),
      mode:'lines',name:trajectoryStyle.planned.label,line:{color:trajectoryStyle.plan_palette[index%trajectoryStyle.plan_palette.length],width:2},
      hovertemplate:trajectoryStyle.planned.label+'<br>x %{x:.3f} · y %{y:.3f}<extra></extra>'});
    for(const p of selected) {
      const rows=reduce(p.tracking,[1,2,6,7],10,13);
      traces.push({...arrays(rows,r=>r[1],r=>r[2],r=>r[10],r=>r[13],r=>{
        const stamp=BigInt(r[12]),seconds=stamp/1000000000n,millis=stamp%1000000000n/1000000n;
        return time(r[0])+'<br>Timestamp: '+seconds+'.'+millis.toString().padStart(3,'0');
      }),mode:'lines',
        name:trajectoryStyle.actual.label,line:{color:trajectoryStyle.actual.color,width:2},hovertemplate:'%{text}<br>x %{x:.3f} · y %{y:.3f}<extra></extra>'});
      const valid=p.tracking.filter(r=>r[10]);
      if(valid.length)for(const [index,r]of [valid[0],valid.at(-1)].entries()) {
        if(!Number.isFinite(r[3]))continue;
        traces.push({x:[r[1]],y:[r[2]],mode:'lines',name:index?'终点朝向':'起点朝向',
          meta:{endpoint_pose:r.slice(1,4)},line:{color:trajectoryStyle[index?'end':'start'].color,width:4},
          showlegend:false,hoverinfo:'skip'});
      }
      if(valid.length)traces.push({x:[valid[0][1],valid.at(-1)[1]],y:[valid[0][2],valid.at(-1)[2]],
        mode:'markers+text',name:p.interval.plan_id+' 起终点',showlegend:false,text:[trajectoryStyle.start.label,trajectoryStyle.end.label],textposition:'top center',
        customdata:[time(valid[0][0]),time(valid.at(-1)[0])],hovertemplate:'%{customdata}<extra></extra>',
        marker:{color:[trajectoryStyle.start.color,trajectoryStyle.end.color],size:trajectoryStyle.marker_size,symbol:[trajectoryStyle.start.symbol,trajectoryStyle.end.symbol]}});
    }
    traces.push({x:[],y:[],mode:'lines+markers',name:'位置与投影',line:{color:'#e36260',dash:'dot'},
      marker:{size:10,color:['#facc15','#e36260']},showlegend:false,hoverinfo:'skip'});
    traces.push({x:[],y:[],mode:'lines',name:'机器人方向',line:{color:trajectoryStyle.direction,width:4},showlegend:false,hoverinfo:'skip'});
    await Plotly.react($('trajectory'),traces,{...common,legend:{...common.legend,entrywidth:80,entrywidthmode:'pixels'},hovermode:'closest',margin:{l:65,r:35,t:30,b:48},
      xaxis:{title:A.meta.frame+' x (m)',gridcolor:'#e8edf5',zeroline:false},
      yaxis:{title:A.meta.frame+' y (m)',scaleanchor:'x',scaleratio:1,gridcolor:'#e8edf5',zeroline:false},
      uirevision:selectedKey},config);
  }
  function layout(axes,bounds=range) {
    const result={...common,showlegend:true,hovermode:'x unified',grid:{rows:axes.length,columns:1,pattern:'independent'},uirevision:selectedKey};
    axes.forEach((label,i)=>{const suffix=i?String(i+1):'';
      result['xaxis'+suffix]={...ticks(bounds),range:bounds.slice(),matches:i?'x':undefined,showticklabels:i===axes.length-1};
      result['yaxis'+suffix]={title:label,gridcolor:'#e8edf5',zerolinecolor:'#c3cfdf'};});
    return result;
  }
  async function series() {
    const velocity=[];
    for(const p of selected) {
      for(let group=0;group<4;group++) {
        const presentation=velocityStyles[group],source=A.signals?.[presentation.signal];
        if(group>=2&&!source?.length)continue;
        if(group===3&&A.meta.downstream_time_basis!=='bag_record')continue;
        const rows=source?source.filter(r=>r[0]>=p.start&&r[0]<p.end):p.velocity.map(r=>[r[0],...r.slice(group?4:1,group?7:4),r[7],r[8],r[9],r[10]]);
        if(group>=2&&!rows.length)continue;
        const vr=reduce(rows,[1,2,3],4,6),key=presentation.key;
        for(let axis=0;axis<3;axis++)velocity.push({
          ...arrays(vr,r=>r[0],r=>r[1+axis],r=>r[4],r=>r[6],r=>groupedTimestamp(r[0])),
          hovertemplate:(key==='tracking_raw'?'原始指令':presentation.label)+': %{text}<br>%{y:.4f}<extra></extra>',
          mode:'lines',name:presentation.label+(group===0?' · '+A.config.topics.command:group===3?' · '+A.config.topics.downstream+'<br>（record 时间）':''),
          legendgroup:key,visible:velocityControls[key].checked?true:'legendonly',showlegend:p===selected[0]&&axis===0,
          xaxis:axis?'x'+(axis+1):'x',yaxis:axis?'y'+(axis+1):'y',
          line:{color:presentation.color,width:1.5,dash:presentation.dash,shape:group===1?'linear':'hv'}});
      }
    }
    for(const [key,input] of Object.entries(velocityControls))input.disabled=!velocity.some(trace=>trace.legendgroup===key);
    await Promise.all([Plotly.react($('velocity'),velocity,{...layout(['vx (m/s)','vy (m/s)','ω (rad/s)']),
        legend:{...common.legend,y:1.02,yanchor:'bottom'}},config),errorSeries()]);
  }
  async function errorSeries(bounds=range) {
    const hasYaw=selected.some(p=>p.tracking.some(r=>r[10]&&Number.isFinite(r[9])));
    $('yawErrorOption').hidden=!hasYaw;
    $('showYawError').disabled=!hasYaw;
    const labels=style.errors.labels.slice(0,hasYaw&&$('showYawError').checked?3:2),errors=[];
    for(const p of selected) {
      const tr=reduce(p.tracking,[6,7,9],10,13);
      for(let axis=0;axis<labels.length;axis++) {
        const col=[6,7,9][axis];
        errors.push({...arrays(tr,r=>r[0],r=>axis===2?r[col]*180/Math.PI:r[col],r=>r[10]&&Number.isFinite(r[col]),r=>r[13]),
          mode:'lines',name:['横向','路径距离','姿态'][axis],showlegend:false,
          xaxis:axis?'x'+(axis+1):'x',yaxis:axis?'y'+(axis+1):'y',line:{color:style.errors.colors[axis],width:1.5}});
      }
    }
    await Plotly.react($('errors'),errors,layout(labels,bounds),config);
  }
  function findAt(rows,t,endIndex) {
    let lo=0,hi=rows.length;
    while(lo<hi){const m=(lo+hi)>>1;if(rows[m][0]<=t)lo=m+1;else hi=m;}
    const r=rows[lo-1];return r&&t<=r[endIndex]+1e-9?r:null;
  }
  function setCursorPinned(pinned) {
    cursorPinned=pinned;
    for(const id of ['releaseCursor','releaseVelocityCursor'])$(id).hidden=!pinned;
    if(pinned) {
      playing=false;lastFrame=0;$('play').textContent='播放';
      for(const id of ['velocity','errors'])Plotly.Fx.unhover($(id));
    }
  }
  function pinCursor(t) {
    if(!ready||!Number.isFinite(t))return;
    setCursorPinned(true);setCursor(t);
  }
  function setCursor(t) {
    cursor=Math.max(range[0],Math.min(range[1],t));
    $('timeline').value=String((cursor-range[0])/Math.max(range[1]-range[0],1e-9)*10000);
    $('cursorTime').textContent=time(cursor);$('cursorTime').title=time(cursor);
    if(!ready)return;
    const p=selected.find(p=>cursor>=p.start&&cursor<p.end);
    const r=p?findAt(p.tracking,cursor,13):null,v=p?findAt(p.velocity,cursor,9):null;
    const graph=$('trajectory'),last=graph.data.length;
    const valid=r&&r[10];let x=[],y=[],dx=[],dy=[];
    if(valid){x=[r[1],r[4]];y=[r[2],r[5]];
      const span=graph._fullLayout.xaxis.range;
      const length=(span[1]-span[0])*trajectoryStyle.direction_ratio;dx=[r[1],r[1]+Math.cos(r[3])*length];dy=[r[2],r[2]+Math.sin(r[3])*length];}
    const xs=[x,dx],ys=[y,dy],indices=[last-2,last-1];
    const span=graph._fullLayout.xaxis.range,endpointLength=(span[1]-span[0])*trajectoryStyle.endpoint_direction_ratio;
    graph.data.forEach((trace,index)=>{
      const pose=trace.meta?.endpoint_pose;if(!pose)return;
      xs.push([pose[0],pose[0]+Math.cos(pose[2])*endpointLength]);
      ys.push([pose[1],pose[1]+Math.sin(pose[2])*endpointLength]);indices.push(index);
    });
    Plotly.restyle(graph,{x:xs,y:ys},indices);
    const shapes=[{type:'line',xref:'x',yref:'paper',x0:cursor,x1:cursor,y0:0,y1:1,line:{color:'#7890ae',width:1,dash:'dot'}}];
    const timestamp=groupedTimestamp(cursor);
    for(const id of ['velocity','errors']) {
      const plot=$(id),axes=plot._fullLayout,axis=axes.xaxis;
      const pixel=axis.l2p(cursor),halfWidth=Math.min(axis._length,timestamp.length*6.7+10)/2;
      const annotations=(plot.layout.annotations||[]).filter(a=>a.name!=='cursorTimestamp');
      annotations.push({name:'cursorTimestamp',xref:'x',yref:'paper',x:cursor,
        y:axes.yaxis2?(axes.yaxis.domain[0]+axes.yaxis2.domain[1])/2:.5,
        text:timestamp,showarrow:false,xanchor:'center',yanchor:'middle',
        xshift:Math.max(halfWidth,Math.min(axis._length-halfWidth,pixel))-pixel,
        visible:pixel>=0&&pixel<=axis._length,font:{family:'monospace',size:11,color:'#445873'},
        bgcolor:'#ffffff',bordercolor:'#c8d5e6',borderwidth:1,borderpad:3});
      Plotly.relayout(plot,{shapes,annotations});
    }
    let text=valid?'横向 '+fmt(r[6])+' m · 路径距离 '+fmt(r[7])+' m · '+r[11]:'当前时刻没有有效轨迹，可能处于缺口或排除区间';
    if(v&&v[7])text+=' | 指令 ['+v.slice(1,4).map(x=>fmt(x)).join(', ')+'] · 反馈 ['+v.slice(4,7).map(x=>fmt(x)).join(', ')+']';
    else text+=' | 速度配对不可用';
    const state=A.meta.log_compatible?A.events.filter(e=>e.kind==='tracking_state'&&e.time<=cursor&&cursor-e.time<=Number(A.config.log_age_ns)/1e9).at(-1):null;
    if(state)for(const field of ['mode','progress_s','source_index','limit_reason'])if(state.fields[field]!==undefined)text+=' · '+field+'='+state.fields[field];
    $('cursorReadout').textContent=text;
    document.dispatchEvent(new CustomEvent('trackinsight:cursor',{detail:{
      ns:ns(cursor).toString(),position:valid?r.slice(1,4):null,
      errors:valid?[r[6],r[7],r[9]]:null,
      command:v&&v[7]?v.slice(1,4):null,feedback:v&&v[7]?v.slice(4,7):null
    }}));
  }
  function showEvents() {
    const list=[];
    for(const p of selected) {
      list.push({time:p.start,category:'navigation',kind:p.interval.plan_id+' 路径窗口',raw:JSON.stringify(p.interval,null,2)});
      list.push(...p.findings.map(f=>({...f,time:f.start,raw:JSON.stringify(f,null,2)})));
    }
    for(const e of A.events)if(e.time>=range[0]&&e.time<=range[1])list.push(e);
    for(const e of A.diagnostics)if(e.time>=range[0]&&e.time<=range[1]||e.ns==='0')list.push({...e,time:e.ns==='0'?range[0]:e.time,category:'data_quality',kind:e.code,raw:e.message});
    list.sort((a,b)=>a.time-b.time);$('events').replaceChildren();
    const filter=$('eventType').value;
    for(const e of list)if(!filter||e.category===filter) {
      const b=document.createElement('button');b.className='event';
      b.textContent=time(e.time,false)+' · '+e.kind;
      const s=document.createElement('small');s.textContent='  '+e.category;b.append(s);
      b.onclick=()=>{setCursor(e.time);$('logDetail').textContent=(e.source?e.source+' : '+e.line+'\n':'')+(e.raw||e.message||'');};
      $('events').append(b);
    }
  }
  async function select(paths,key) {
    document.dispatchEvent(new Event('trackinsight:selection'));
    setCursorPinned(false);playing=false;$('play').textContent='播放';ready=false;selected=paths;selectedKey=key;
    for(const b of $('navigation').querySelectorAll('button')) {
      b.classList.toggle('selected',b.dataset.key===key);
      if(b.dataset.key===key&&b.parentElement.matches('details.navigationTask'))b.parentElement.open=true;
    }
    const task=A.tasks.find(t=>t.id===key)||A.tasks.find(t=>paths.length===1&&t.id===paths[0].interval.navigation_id);
    const navigation=task?.id===key;
    range=navigation?[rel(task.start_ns),rel(task.end_ns)]:paths.length?[Math.min(...paths.map(p=>p.start)),Math.max(...paths.map(p=>p.end))]:[0,rel(A.meta.end_ns)];
    const title=$('selectionTitle');
    title.dataset.report=key==='empty'?'暂无可显示的导航或路径':navigation?key+' · 导航总览':paths.length===1?(task?task.id+' · ':'')+paths[0].interval.plan_id+' · 轨迹跟踪分析':key+' · 导航总览';
    title.dataset.review=!navigation&&paths.length===1?paths[0].interval.plan_id:title.dataset.report;
    title.textContent=title.dataset[document.body.classList.contains('review-mode')?'review':'report'];
    const plan=paths.length===1?A.plans.find(p=>p.id===paths[0].interval.plan_id):null;
    const endpoints=$('titleEndpoints');endpoints.hidden=!plan?.poses.length;
    endpoints.textContent=plan?.poses.length?'start('+plan.poses[0].slice(0,2).map(v=>fmt(v)).join(', ')+') ---> goal('+plan.poses.at(-1).slice(0,2).map(v=>fmt(v)).join(', ')+')':'';
    endpoints.title=plan?'规划路径首尾坐标 · '+plan.frame+' · 单位 m':'';
    const startLabel=time(range[0]),endLabel=time(range[1]),duration=fmt(range[1]-range[0],2)+' s';
    $('windowLabel').textContent=(navigation?'导航区间：':'路径区间：')+startLabel+' — '+endLabel+' · '+duration;
    const startDate=A.clock.wall_clock?startLabel.slice(0,10):'',endDate=A.clock.wall_clock?endLabel.slice(0,10):'';
    $('windowDate').textContent=startDate===endDate?startDate:startDate+' — '+endDate;
    $('windowDate').hidden=!A.clock.wall_clock;
    $('windowClockRange').textContent=time(range[0],false)+' — '+time(range[1],false);
    $('windowEpochRange').textContent='('+range.map(t=>groupedTimestamp(t,3)).join(' — ')+')';
    $('windowEpochRange').title='开始 — 结束（秒，精确到毫秒）';
    $('windowDuration').textContent='\u00a0耗时'+fmt(range[1]-range[0],2)+'s';
    $('taskInfo').hidden=!task;
    if(task) {
      const reason={next_navigation_start:'被下一次导航替换',recorded_navigation_result:'已记录导航结果',recording_ended_without_result:'录制结束时未记录结果',session_changed_without_result:'会话切换时未记录结果'}[task.reason]||task.reason;
      $('taskInfo').textContent='导航 '+task.id+'：'+time(rel(task.start_ns))+' — '+time(rel(task.end_ns))+' · '+task.outcome;
      if(!paths.length)$('taskInfo').textContent+=' · 无可评价路径 · '+reason+'；相关证据见事件与日志';
    }
    $('association').textContent=paths.length===1?badgeText(paths[0]):paths.length?'路径版本分别评价':'无可评价路径';
    $('association').hidden=!$('association').textContent;
    $('stage').replaceChildren(new Option('全部阶段',''));
    if(paths.length===1)for(const stage of Object.keys(paths[0].stages))$('stage').add(new Option(stage,stage));
    $('aggregateQuality').hidden=!navigation||!paths.length;
    if(navigation)$('aggregateQuality').value=A.navigation_metrics[key+'/confirmed']?'confirmed':'inferred';
    $('stage').disabled=paths.length!==1||navigation;showMetrics();showEvents();$('logDetail').textContent='选择事件查看证据、日志行号和原始内容。';$('candidates').hidden=true;
    await Promise.all([trajectory(),series()]);ready=true;setCursor(range[0]);
  }
  $('aggregateQuality').onchange=showMetrics;$('stage').onchange=showMetrics;$('eventType').onchange=showEvents;
  for(const [key,input] of Object.entries(velocityControls))input.onchange=()=>{
    const indices=$('velocity').data.flatMap((trace,i)=>trace.legendgroup===key?[i]:[]);
    if(indices.length)Plotly.restyle($('velocity'),{visible:input.checked?true:'legendonly'},indices);
  };
  $('timeline').oninput=()=>{
    playing=false;lastFrame=0;$('play').textContent='播放';
    setCursor(range[0]+Number($('timeline').value)/10000*(range[1]-range[0]));
  };
  for(const id of ['releaseCursor','releaseVelocityCursor'])$(id).onclick=()=>setCursorPinned(false);
  let releasingEscape=false;
  document.addEventListener('keydown',e=>{
    if(e.key==='Escape'&&(cursorPinned||releasingEscape)) {
      e.preventDefault();e.stopPropagation();releasingEscape=true;setCursorPinned(false);
    }
  },true);
  document.addEventListener('keyup',e=>{if(e.key==='Escape')releasingEscape=false;},true);
  window.addEventListener('blur',()=>{releasingEscape=false;});
  $('showYawError').onchange=async()=>{
    await errorSeries($('errors')._fullLayout.xaxis.range.slice());setCursor(cursor);
  };
  $('resetTime').onclick=()=>{
    for(const id of ['velocity','errors']) {
      const plot=$(id),patch={};
      for(const k of Object.keys(plot._fullLayout).filter(k=>/^xaxis\d*$/.test(k))) {
        patch[k+'.range']=range.slice();
        for(const [name,value] of Object.entries(ticks(range)))patch[k+'.'+name]=value;
      }
      Plotly.relayout(plot,patch);
    }
  };
  for(const [id,graph]of [['exportTrajectory','trajectory'],['exportVelocity','velocity'],['exportErrors','errors']])$(id).onclick=()=>Plotly.downloadImage($(graph),{format:'png',filename:'Track_Insight_'+selectedKey+'_'+graph,width:1500,height:graph==='velocity'?1000:800,scale:1.5});
  $('play').onclick=()=>{playing=!playing;if(playing)setCursorPinned(false);if(playing&&cursor>=range[1])setCursor(range[0]);lastFrame=0;$('play').textContent=playing?'暂停':'播放';};
  for(const [id,delta] of [['stepBack',-.05],['stepForward',.05]])$(id).onclick=()=>{
    if(!ready)return;
    playing=false;lastFrame=0;$('play').textContent='播放';setCursor(cursor+delta);
  };
  function animation(now) {
    if(playing&&ready) {
      if(lastFrame&&now-lastFrame>=40){setCursor(cursor+(now-lastFrame)/1000*parseInt($('rate').value));lastFrame=now;}
      if(!lastFrame)lastFrame=now;
      if(cursor>=range[1]){playing=false;$('play').textContent='播放';}
    }
    requestAnimationFrame(animation);
  }
  async function init() {
    if(A.paths.length)await select([A.paths[0]],A.paths[0].interval.id);else if(A.tasks.length)await select([],A.tasks[0].id);else await select([],'empty');
    for(const id of ['velocity','errors']) {
      const plot=$(id);
      plot.on('plotly_beforehover',()=>!cursorPinned);
      plot.on('plotly_hover',e=>{if(!cursorPinned&&e.points.length)setCursor(e.points[0].x);});
      // A primary click anywhere inside a time subplot pins; drags still belong to Plotly.
      let press=null;
      plot.addEventListener('pointerdown',e=>{
        if(!ready||e.button!==0||!e.isPrimary||!e.target.classList.contains('nsewdrag'))return;
        const bounds=e.target.getBoundingClientRect(),axis=plot._fullLayout.xaxis;
        press={id:e.pointerId,x:e.clientX,y:e.clientY,time:axis.p2d(e.clientX-bounds.left)};
      },true);
      document.addEventListener('pointermove',e=>{
        if(press&&e.pointerId===press.id&&Math.hypot(e.clientX-press.x,e.clientY-press.y)>4)press=null;
      },true);
      document.addEventListener('pointercancel',()=>{press=null;},true);
      document.addEventListener('pointerup',e=>{
        if(!press||e.pointerId!==press.id)return;
        const clicked=press;press=null;
        if(e.button===0&&Math.hypot(e.clientX-clicked.x,e.clientY-clicked.y)<=4)pinCursor(clicked.time);
      },true);
      $(id).on('plotly_relayout',async e=>{
        if(linked)return;
        let bounds=null;
        for(let i=1;i<=3;i++){const k='xaxis'+(i===1?'':i);if(e[k+'.range[0]']!==undefined)bounds=[e[k+'.range[0]'],e[k+'.range[1]']];if(Array.isArray(e[k+'.range']))bounds=e[k+'.range'];}
        if(!bounds)return;linked=true;
        try{await Promise.all(['velocity','errors'].map(id=>{
          const graph=$(id),patch={};
          for(const k of Object.keys(graph._fullLayout).filter(k=>/^xaxis\d*$/.test(k))) {
            patch[k+'.range']=bounds;for(const [name,value]of Object.entries(ticks(bounds)))patch[k+'.'+name]=value;
          }
          return Plotly.relayout(graph,patch);
        }));setCursor(cursor);}finally{linked=false;}
      });
    }
    $('velocity').on('plotly_restyle',()=>{
      for(const [key,input] of Object.entries(velocityControls)) {
        const traces=$('velocity').data.filter(trace=>trace.legendgroup===key);
        if(traces.length)input.checked=traces.some(trace=>trace.visible!==false&&trace.visible!=='legendonly');
      }
    });
    $('trajectory').on('plotly_relayout',e=>{
      if(Object.keys(e).some(key=>/^[xy]axis\.(range|autorange)/.test(key)))setCursor(cursor);
    });
    $('trajectory').on('plotly_click',e=>{
      if(!e.points.length)return;const q=e.points[0],axis=$('trajectory')._fullLayout.xaxis;
      const tolerance=(axis.range[1]-axis.range[0])*.012,candidates=[];
      for(const p of selected)for(const r of p.tracking)if(r[10]&&Math.hypot(r[1]-q.x,r[2]-q.y)<=tolerance) {
        if(!candidates.length||r[0]-candidates.at(-1)[0]>.25)candidates.push(r);
      }
      $('candidates').replaceChildren();$('candidates').hidden=candidates.length<=1;
      if(candidates.length){setCursor(candidates[0][0]);for(const r of candidates){const b=document.createElement('button');b.textContent=time(r[0],false);b.onclick=()=>setCursor(r[0]);$('candidates').append(b);}}
    });
    requestAnimationFrame(animation);
    window.trackInsight={analysis:A,metricNames:name,select,setCursor,get cursor(){return cursor;},get selected(){return selectedKey;},time};
    document.dispatchEvent(new Event('trackinsight:ready'));setCursor(cursor);
  }
  init().catch(error=>{$('failureBanner').hidden=false;$('failureBanner').textContent='页面初始化失败：'+error.message;console.error(error);});
})();
