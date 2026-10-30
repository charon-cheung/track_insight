#!/usr/bin/env python3
"""Exercise the production overview table in Chrome with numeric and missing-data cases."""
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOM = '''<div id="reportOverview"></div><div id="navigation"></div>
<button id="overviewNavigation"></button><button id="play">播放</button>
<button id="downloadMetrics"></button><pre id="result">PENDING</pre>'''
SETUP = '''
const cases=[
 {id:'E1',v:[.0204,.3,.4]}, {id:'E2',v:[.02,.04,.2]},
 {id:'E3',v:[null,NaN,Infinity]}, {id:'E4',v:[.02,.06,.12]},
 {id:'E5',v:[0,0,0]}, {id:'E6',v:[]}];
window.trackInsight={metricNames:{},analysis:{
 paths:cases.map(c=>({interval:{id:c.id,plan_id:c.id},metrics:c.v.length?{lateral:{rmse:c.v[0],p95:c.v[1],max:c.v[2]}}:{}})),
 overview:{case_ids:cases.map(c=>c.id),best_ids:['E5'],worst_ids:['E1'],averages:{},ranked_cases:4}}};
let navigated=null;
for(const c of cases){const b=document.createElement('button');b.dataset.key=c.id;b.onclick=()=>{navigated=c.id;};document.getElementById('navigation').append(b);}
'''
CHECKS = '''
const assert=(value,message)=>{if(!value)throw Error(message);};
const order=()=>[...document.querySelectorAll('#overviewCases tr[data-case]')].map(r=>r.dataset.case).join(',');
const original=JSON.stringify(trackInsight.analysis);
const checkRanks=()=>{
 const row=id=>document.querySelector('#overviewCases tr[data-case="'+id+'"]');
 assert(row('E5').textContent.includes('最好'),'Best case lacks visible label');
 assert(row('E1').textContent.includes('最差'),'Worst case lacks visible label');
 assert(getComputedStyle(row('E5')).backgroundColor==='rgb(135, 206, 235)','Best row must be sky blue');
 assert(getComputedStyle(row('E1')).backgroundColor==='rgb(255, 153, 153)','Worst row must be red');
 assert(![...document.querySelectorAll('#reportOverview h2')].some(h=>['跟踪精度最好','跟踪精度最差'].includes(h.textContent)),'Old detail panels remain');
};
document.dispatchEvent(new Event('trackinsight:ready'));
const table=document.getElementById('overviewCases').closest('table'),headers=[...table.querySelectorAll('thead th')];
assert(order()==='E1,E2,E3,E4,E5,E6','Initial order changed');checkRanks();
assert(['E1','E2','E4'].every(id=>document.querySelector('tr[data-case="'+id+'"]').cells[1].textContent==='0.020'),'Rounded-value fixture changed');
assert(table.querySelectorAll('thead button').length===3,'Expected exactly three sortable headers');
const summary=[...document.querySelectorAll('#reportOverview > section')].slice(1).map(s=>s.textContent).join('');
for(const [column,ascending,descending] of [
 [1,'E5,E2,E4,E1,E3,E6','E1,E2,E4,E5,E3,E6'],
 [2,'E5,E2,E4,E1,E3,E6','E1,E4,E2,E5,E3,E6'],
 [3,'E5,E4,E2,E1,E3,E6','E1,E2,E4,E5,E3,E6']]) {
 const button=headers[column].querySelector('button');assert(button.type==='button','Sort control is not a button');
 button.focus();assert(document.activeElement===button,'Sort header is not focusable');
 for(const [direction,expected] of [['ascending',ascending],['descending',descending]]) {
  button.click();checkRanks();assert(order()===expected,'Wrong '+direction+' order in column '+column+': '+order());
  assert(headers[column].getAttribute('aria-sort')===direction,'Missing accessible sort direction');
  assert(button.textContent.includes(direction==='ascending'?'↑':'↓'),'Missing sort arrow');
  assert(headers.filter(h=>['ascending','descending'].includes(h.getAttribute('aria-sort'))).length===1,'Multiple active columns');
 }
}
assert(JSON.stringify(trackInsight.analysis)===original,'Sorting mutated analysis');
assert([...document.querySelectorAll('#reportOverview > section')].slice(1).map(s=>s.textContent).join('')===summary,'Sorting changed summary');
document.querySelector('#overviewCases tr:first-child button').click();assert(navigated==='E1','Sorted case link navigates incorrectly');
const saved=order();document.getElementById('overviewNavigation').click();assert(order()===saved,'Revisiting overview lost sort');
'''


def check(source, setup, checks):
    with tempfile.TemporaryDirectory(prefix='track_sort_', ignore_cleanup_errors=True) as tmp:
        page = Path(tmp) / 'check.html'
        page.write_text('<meta charset="utf-8"><style>'+(ROOT/'resources/viewer/viewer.css').read_text()+(ROOT/'resources/viewer/overview.css').read_text()+'</style>'+DOM+'<script>'+setup+'</script><script>'+source+'</script><script>try{'+checks+
            ';document.getElementById("result").textContent="PASS";}catch(e){document.getElementById("result").textContent="FAIL: "+e.message;}</script>')
        run = subprocess.run(['/usr/bin/google-chrome','--headless','--disable-gpu','--disable-background-networking',
            '--no-first-run','--user-data-dir='+str(Path(tmp)/'profile'),'--dump-dom',page.as_uri()],capture_output=True,text=True,timeout=30)
        assert run.returncode == 0, run.stderr[-2000:]
        result = run.stdout.split('<pre id="result">')[-1].split('</pre>')[0]
        assert result == 'PASS', result


if __name__ == '__main__':
    source = (ROOT/'resources/viewer/overview.js').read_text()
    check(source, SETUP, CHECKS)
    check(source, SETUP+'trackInsight.analysis.paths=[];trackInsight.analysis.overview.case_ids=[];', '''
      document.dispatchEvent(new Event('trackinsight:ready'));
      const body=document.getElementById('overviewCases');
      for(const button of body.closest('table').querySelectorAll('thead button')){button.click();button.click();}
      if(body.rows.length!==1||!body.textContent.includes('没有符合条件'))throw Error('Empty-table message was lost');
    ''')
    check(source, SETUP+"trackInsight.analysis.overview.case_ids=['E1','E2','E4'];trackInsight.analysis.overview.best_ids=['E2','E4'];", """
      document.dispatchEvent(new Event('trackinsight:ready'));
      for(const id of ['E2','E4']){
        const row=document.querySelector('#overviewCases tr[data-case="'+id+'"]');
        if(!row.textContent.includes('最好')||getComputedStyle(row).backgroundColor!=='rgb(135, 206, 235)')throw Error('Tied best case not marked');
      }
    """)
    check(source, SETUP+"trackInsight.analysis.overview.case_ids=['E1'];trackInsight.analysis.overview.best_ids=['E1'];", """
      document.dispatchEvent(new Event('trackinsight:ready'));
      const row=document.querySelector('#overviewCases tr[data-case]');
      if(!row.textContent.includes('最好 / 最差')||!getComputedStyle(row).backgroundImage.includes('linear-gradient'))throw Error('Single ranked case must show both ranks');
    """)
    print('PASS: numeric ascending/descending; stable ties; zero/missing/invalid; indicators; unchanged statistics; case links; empty table')
