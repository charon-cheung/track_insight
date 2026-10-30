// Captured configuration is report evidence, not a live read of the controller's parameters.
'use strict';
document.addEventListener('trackinsight:ready',()=>{
  const snapshot=window.trackInsight.analysis.pid_parameters,root=document.getElementById('reportOverview');
  const node=(tag,text,className)=>{const e=document.createElement(tag);if(text!==undefined)e.textContent=text;if(className)e.className=className;return e;};
  const panel=node('section',undefined,'panel');panel.id='pidParameters';
  const heading=node('div',undefined,'panelTitle');heading.append(node('h2','本轮 PID 参数'));panel.append(heading);root.append(panel);
  if(!snapshot?.values||snapshot.schema_version!==1) {
    panel.append(node('p','本轮未保存 PID 参数快照。','overviewNote'));return;
  }
  panel.append(node('p','配置文件：'+snapshot.source_path+' · 快照时间：'+snapshot.captured_at_label,'overviewNote'));
  panel.append(node('p','生成报告时读取的配置快照，未核验机器人运行时参数。重新渲染沿用已保存快照。','overviewNote'));
  const comparison=snapshot.comparison,hasBaseline=comparison?.status==='available'&&comparison.values;
  panel.append(node('p',hasBaseline?'比较基准：上一轮 '+comparison.run_name+'。灰色行表示修改、新增或删除的参数。':
    comparison?.status==='previous_snapshot_missing'?'上一轮 '+comparison.run_name+' 未保存参数快照，无可用比较基准。':
    comparison?.status==='no_previous_round'?'首轮参数快照，无上一轮参数基准。':'未关联上一轮测试报告，无可用比较基准。','overviewNote'));
  const current=snapshot.values,previous=hasBaseline?comparison.values:{};
  const hidden=new Set(['controller_frequency','failure_tolerance','publish_zero_velocity','PidTrackingController.plugin']);
  const keys=[...new Set([...Object.keys(current),...Object.keys(previous)])].filter(key=>!hidden.has(key)).sort();
  const scroll=node('div',undefined,'tableScroll'),table=node('table'),head=node('thead'),header=node('tr'),body=node('tbody');
  body.id='pidParameterRows';for(const text of ['参数','本轮值','上一轮值','变化'])header.append(node('th',text));head.append(header);
  const owns=(object,key)=>Object.prototype.hasOwnProperty.call(object,key);
  const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
  for(const key of keys) {
    const present=owns(current,key),before=owns(previous,key);
    const status=!hasBaseline?'无基准':!before?'新增':!present?'已删除':same(current[key],previous[key])?'未修改':'已修改';
    const row=node('tr');row.dataset.parameter=key;
    if(hasBaseline&&status!=='未修改')row.className='parameterChanged';
    for(const text of [key.replace(/^PidTrackingController\./,''),present?JSON.stringify(current[key]):'—',before?JSON.stringify(previous[key]):'—',status])row.append(node('td',text));
    body.append(row);
  }
  table.append(head,body);scroll.append(table);panel.append(scroll);
},{once:true});
