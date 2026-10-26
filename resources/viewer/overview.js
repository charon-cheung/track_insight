'use strict';
(() => {
  document.addEventListener('trackinsight:ready',()=>{
    const api=window.trackInsight,$=id=>document.getElementById(id),root=$('reportOverview');
    const summary=api.analysis.overview,paths=new Map(api.analysis.paths.map(p=>[p.interval.id,p]));
    const cases=(summary?.case_ids||[]).map(id=>paths.get(id)).filter(Boolean);
    const fmt=(v,d=3)=>Number.isFinite(v)?v.toFixed(d):'—';
    const percent=v=>Number.isFinite(v)?(v*100).toFixed(1)+'%':'—';
    const columns=[['lateral','rmse','横向 RMSE (m)'],['lateral','p95','横向 P95 (m)'],
      ['lateral','max','最大横向误差 (m)'],['path_distance','rmse','路径距离 RMSE (m)'],
      ['vx_residual','rmse','vx RMSE (m/s)'],
      ['vy_residual','rmse','vy RMSE (m/s)'],['omega_residual','rmse','ω RMSE (rad/s)']];
    const metricKeys=['lateral','path_distance','yaw_error','vx_residual','vy_residual','omega_residual','speed_vector_residual'];
    function element(tag,text,className) {
      const node=document.createElement(tag);if(text!==undefined)node.textContent=text;
      if(className)node.className=className;return node;
    }
    function caseButton(p) {
      const button=element('button',p.interval.plan_id,'caseLink');button.type='button';
      button.dataset.case=p.interval.id;
      button.onclick=()=>{
        const target=[...$('navigation').querySelectorAll('button')].find(b=>b.dataset.key===p.interval.id);
        if(target&&!target.hidden)target.click();
      };
      return button;
    }
    function table(headers) {
      const scroll=element('div',undefined,'tableScroll'),table=element('table'),head=element('thead'),row=element('tr'),body=element('tbody');
      for(const label of headers)row.append(element('th',label));
      head.append(row);table.append(head,body);scroll.append(table);return {scroll,body};
    }
    function panel(title) {
      const section=element('section',undefined,'panel'),heading=element('div',undefined,'panelTitle');
      heading.append(element('h2',title));section.append(heading);return section;
    }
    function metricDetails(title,ids) {
      const section=panel(title);
      if(!ids.length){section.append(element('p','没有可用于横向误差排名的数据。','overviewNote'));return section;}
      for(const id of ids) {
        const p=paths.get(id);if(!p)continue;
        const heading=element('h3',undefined,'overviewCase');heading.append(caseButton(p),
          document.createTextNode(' · '+p.start_label+' · '+fmt(p.end-p.start,2)+' s'));section.append(heading);
        const detail=table(['指标','RMSE / 值','MAE','P95','最大值','覆盖率']);
        for(const [key,m] of Object.entries(p.metrics)) {
          const row=element('tr');row.title=m.reason||'';
          for(const value of [api.metricNames[key]||key,fmt(m.value??m.rmse)+' '+m.unit,
            fmt(m.mae),fmt(m.p95),fmt(m.max),percent(m.coverage)])row.append(element('td',value));
          detail.body.append(row);
        }
        section.append(detail.scroll);
      }
      return section;
    }
    root.append(element('h1','跟踪精度总览'));
    const ranked=summary?.ranked_cases||0,excluded=summary?.excluded_cases||0;
    root.append(element('p',cases.length+' 个路径 case · 横向精度可评价 '+ranked+' 个 · 排除 failed / replaced '+excluded+' 个','overviewNote'));
    root.append(element('p','每个 P 路径执行区间统计一次，不重复计入导航 N 总览；综合模式中隐藏的 case 仍参与本报告。','overviewNote'));
    const all=panel('所有 case 的跟踪精度');
    const listing=table(['Case',...columns.map(c=>c[2]),'横向覆盖率','速度覆盖率']);
    listing.body.id='overviewCases';
    for(const p of cases) {
      const row=element('tr');row.dataset.case=p.interval.id;
      const label=element('td');label.append(caseButton(p));row.append(label);
      const values=[...columns.map(([key,stat])=>fmt(p.metrics[key]?.[stat])),
        percent(p.metrics.lateral?.coverage),percent(p.metrics.vx_residual?.coverage)];
      for(const value of values)row.append(element('td',value));listing.body.append(row);
    }
    if(!cases.length){const row=element('tr'),cell=element('td','没有符合条件的路径 case。');cell.colSpan=columns.length+3;row.append(cell);listing.body.append(row);}
    all.append(listing.scroll);root.append(all);
    const averages=panel('所有 case 的平均跟踪精度');
    const means=table(['指标','case 平均 RMSE','整体 RMSE（按有效时长合并）','纳入 case 数','有效时长 (s)','整体覆盖率']);
    means.body.id='overviewAverages';
    for(const key of metricKeys) {
      const m=summary?.averages?.[key]||{},row=element('tr');row.dataset.metric=key;
      for(const value of [api.metricNames[key]||key,fmt(m.case_mean_rmse,4)+(m.unit?' '+m.unit:''),
        fmt(m.pooled_rmse,4)+(m.unit?' '+m.unit:''),(m.case_count||0)+' / '+cases.length,
        fmt(m.valid_s,2),percent(m.coverage)])row.append(element('td',value));
      means.body.append(row);
    }
    averages.append(means.scroll,element('p','case 平均 RMSE = 各有效 case 的 RMSE 算术平均；整体 RMSE = √[Σ(RMSE² × 有效时长) / Σ有效时长]。缺失项不按 0 计算，不参与对应均值。','overviewNote'));
    root.append(averages);
    root.append(element('p','以下按未四舍五入的横向误差 RMSE 选出最好和最差 case，越小越好；相同值并列展示。请结合覆盖率和“推定 / 已确认”依据比较。','overviewNote'));
    const extremes=element('div',undefined,'overviewExtremes');
    extremes.append(metricDetails('跟踪精度最好',summary?.best_ids||[]),metricDetails('跟踪精度最差',summary?.worst_ids||[]));root.append(extremes);
    root.append(element('p','速度指标均为反馈 − 指令的残差；姿态误差缺少已验证目标朝向时不可用。定位轨迹不是独立真值。','overviewNote'));
    function show(visible) {
      if(visible&&$('play').textContent==='暂停')$('play').click();
      document.body.classList.toggle('report-overview',visible);root.hidden=!visible;
      $('overviewNavigation').classList.toggle('selected',visible);
      $('overviewNavigation').setAttribute('aria-current',visible?'page':'false');
      for(const button of $('navigation').querySelectorAll('button'))button.classList.toggle('selected',!visible&&button.dataset.key===api.selected);
    }
    $('overviewNavigation').onclick=()=>{show(true);window.scrollTo(0,0);};
    document.addEventListener('trackinsight:selection',()=>show(false));
    document.addEventListener('trackinsight:mode',event=>show(!event.detail.review));
    const exportSelected=$('downloadMetrics').onclick;
    $('downloadMetrics').onclick=event=>{
      if(!document.body.classList.contains('report-overview'))return exportSelected(event);
      const rows=[['case',...columns.map(c=>c[2]),'lateral_coverage','velocity_coverage']];
      for(const p of cases)rows.push([p.interval.plan_id,
        ...columns.map(([key,stat])=>p.metrics[key]?.[stat]),p.metrics.lateral?.coverage,p.metrics.vx_residual?.coverage]);
      const text=rows.map(row=>row.map(v=>'"'+String(v??'').replaceAll('"','""')+'"').join(',')).join('\n');
      const url=URL.createObjectURL(new Blob(['\ufeff'+text],{type:'text/csv;charset=utf-8'})),link=element('a');
      link.href=url;link.download='track_insight_overview.csv';link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
    };
    show(!document.body.classList.contains('review-mode'));
  },{once:true});
})();
