#!/usr/bin/env python3
"""Check PID snapshot display, safe text rendering and semantic differences in Chrome."""
import json
from check_overview_sort import ROOT, check

source=(ROOT/'resources/viewer/parameters.js').read_text()
base={'schema_version':1,'source_path':'/test/pid.yaml','captured_at_label':'2026-10-30 12:00:00',
      'values':{'unchanged':.8,'changed':2,'added':True,'type_changed':'1','unsafe':'<img src=x onerror=alert(1)>'},
      'comparison':{'status':'available','run_name':'previous','values':{'unchanged':.80,'changed':1,'removed':3,'type_changed':1,'unsafe':'<img src=x onerror=alert(1)>'}}}
checks='''
const assert=(v,m)=>{if(!v)throw Error(m);};
const before=JSON.stringify(trackInsight.analysis);
document.dispatchEvent(new Event('trackinsight:ready'));
const panel=document.getElementById('pidParameters'),rows=[...document.querySelectorAll('#pidParameterRows tr')];
assert(document.getElementById('reportOverview').lastElementChild===panel,'Parameters must be last');
const changed=rows.filter(r=>getComputedStyle(r).backgroundColor==='rgb(224, 224, 224)').map(r=>r.dataset.parameter).sort();
assert(JSON.stringify(changed)===JSON.stringify(['added','changed','removed','type_changed']),'Wrong changed rows: '+changed);
assert(rows.find(r=>r.dataset.parameter==='removed').cells[1].textContent==='—','Removed value still shown as current');
assert(rows.find(r=>r.dataset.parameter==='changed').cells[2].textContent==='1','Previous value lost');
assert(rows.find(r=>r.dataset.parameter==='added').cells[3].textContent==='新增','Addition not labeled');
assert(!panel.querySelector('img')&&panel.textContent.includes('<img src=x'),'Parameter values must remain plain text');
assert(JSON.stringify(trackInsight.analysis)===before,'Display mutated snapshot');
'''
check(source,'window.trackInsight={analysis:{pid_parameters:'+json.dumps(base)+'}};',checks)
for comparison in (None,{'status':'no_previous_round'},{'status':'previous_snapshot_missing','run_name':'legacy'}):
    data=dict(base)
    if comparison is None:data.pop('comparison')
    else:data['comparison']=comparison
    check(source,'window.trackInsight={analysis:{pid_parameters:'+json.dumps(data)+'}};', '''
      document.dispatchEvent(new Event('trackinsight:ready'));
      if(document.querySelector('.parameterChanged'))throw Error('No baseline must not mark parameters changed');
      if(!document.getElementById('pidParameters').textContent.includes('基准'))throw Error('Missing baseline explanation');
    ''')
check(source,'window.trackInsight={analysis:{}};', '''
  document.dispatchEvent(new Event('trackinsight:ready'));
  if(!document.getElementById('pidParameters').textContent.includes('未保存'))throw Error('Legacy snapshot should be unavailable');
''')
print('PASS: parameter values, gray changes/additions/removals, previous values, no baseline, legacy report, safe text, immutable snapshot')
