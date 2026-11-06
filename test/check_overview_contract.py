#!/usr/bin/env python3
"""Compare the production viewer with C++ summaries for every fixture deletion subset."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from check_overview_sort import ROOT, check

parser = argparse.ArgumentParser()
parser.add_argument('--test-binary', required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='track_overview_contract_') as directory:
    output = Path(directory) / 'cases.json'
    env = dict(os.environ, TRACK_INSIGHT_OVERVIEW_FIXTURE=str(output))
    subprocess.run([args.test_binary, '--gtest_filter=Overview.SelectionContractForDeletedSubsets'],
                   env=env, check=True, stdout=subprocess.DEVNULL)
    fixtures = json.loads(output.read_text())
source = (ROOT/'resources/viewer/overview.js').read_text()
for fixture in fixtures:
    setup = """
    let deleted=new Set();
    window.trackInsight={analysis:"""+json.dumps(fixture['analysis'])+""",metricNames:{},
      isCaseDeleted:id=>deleted.has(id),selected:''};
    document.getElementById('downloadMetrics').onclick=()=>{};
    """
    checks = """
    const checks="""+json.dumps(fixture['checks'])+""";
    const original=JSON.stringify(trackInsight.analysis);
    const equal=(actual,expected,path)=>{
      if(typeof expected==='number'){
        if(!Number.isFinite(actual)||Math.abs(actual-expected)>1e-12*Math.max(Math.abs(expected),1e-20))
          throw Error(path+': '+actual+' != '+expected);
      }else if(Array.isArray(expected)){
        if(JSON.stringify(actual)!==JSON.stringify(expected))throw Error(path+' array mismatch');
      }else if(expected&&typeof expected==='object'){
        if(!actual)throw Error(path+' missing');
        for(const [k,v] of Object.entries(expected))equal(actual[k],v,path+'.'+k);
      }else if(actual!==expected)throw Error(path+' value mismatch');
    };
    document.dispatchEvent(new Event('trackinsight:ready'));
    for(const item of checks){
      deleted=new Set(item.deleted);document.dispatchEvent(new Event('trackinsight:cases'));
      const actual=trackInsight.reportOverview,expected=item.expected;
      for(const key of ['case_ids','best_ids','worst_ids','suppressed_worst_ids','ranked_cases','averages'])
        equal(actual[key],expected[key],key+' deleted='+item.deleted.join(','));
      if(Object.keys(actual.averages).length!==Object.keys(expected.averages).length)throw Error('Extra metrics');
      const rows=[...document.querySelectorAll('#overviewCases tr[data-case]')].map(r=>r.dataset.case);
      equal(rows,expected.case_ids,'rows');
      for(const [name,cls] of [['best_ids','overviewBest'],['worst_ids','overviewWorst']]){
        const marked=[...document.querySelectorAll('#overviewCases .'+cls)].map(r=>r.dataset.case);
        equal(marked,expected[name],'marked '+name);
      }
    }
    deleted.clear();document.dispatchEvent(new Event('trackinsight:cases'));
    if(trackInsight.reportOverview!==trackInsight.analysis.overview)throw Error('Restore lost original formal summary');
    if(JSON.stringify(trackInsight.analysis)!==original)throw Error('Selection mutated analysis');
    """
    check(source, setup, checks)
print('PASS: C++/browser parity for',sum(len(f['checks']) for f in fixtures),
      'deleted subsets; ties/invalid/zero metrics, exclusions, custom bounds, scale separation, overflow and restore')
