#include "track_insight/report.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <set>
#include <chrono>
#include <unistd.h>

namespace track_insight {
namespace fs=std::filesystem;
namespace {
std::string csv(const std::string& s){std::string value="\"";for(char ch:s){value+=ch;if(ch=='"')value+='"';}return value+'"';}
std::string scalar(const Json& j){return j.is_null()?"":j.is_string()?j.get<std::string>():j.dump();}
std::string metricCsv(const Json& analysis) {
  std::ostringstream out;out<<"navigation,plan,interval,scope,metric,unit,value,bias,mae,rmse,p95,max,coverage,valid_s,candidate_s,quality,reason\n";
  auto write=[&](const std::string& n,const std::string& p,const std::string& e,const std::string& scope,const Json& metrics){
    for(auto it=metrics.begin();it!=metrics.end();++it) {
      out<<csv(n)<<','<<csv(p)<<','<<csv(e)<<','<<csv(scope)<<','<<csv(it.key());
      for(const auto& key:{"unit","value","bias","mae","rmse","p95","max","coverage","valid_s","candidate_s","quality","reason"})
        out<<','<<csv(scalar(it.value().at(key)));
      out<<'\n';
    }
  };
  for(const auto& p:analysis["paths"]) {
    write(p["interval"]["navigation_id"],p["interval"]["plan_id"],p["interval"]["id"],"interval",p["metrics"]);
    for(auto it=p["stages"].begin();it!=p["stages"].end();++it)
      write(p["interval"]["navigation_id"],p["interval"]["plan_id"],p["interval"]["id"],it.key(),it.value());
  }
  for(auto it=analysis["navigation_metrics"].begin();it!=analysis["navigation_metrics"].end();++it)
    write(it.key(),"","","navigation",it.value());
  return out.str();
}
void replaceAll(std::string& text,const std::string& from,const std::string& to) {
  std::size_t at=0;while((at=text.find(from,at))!=std::string::npos){text.replace(at,from.size(),to);at+=to.size();}
}
void validate(const Json& a) {
  if(a.at("schema_version")!=1||a.at("generator").at("name")!="Track Insight")
    throw std::runtime_error("unsupported Track Insight analysis schema");
  for(const auto& key:{"paths","plans","tasks","events","diagnostics"})if(!a.at(key).is_array())
    throw std::runtime_error(std::string("invalid analysis array: ")+key);
  const auto clock=readClock(a);(void)clock;
  for(const auto& p:a["paths"]) {
    if(!p.at("tracking").is_array()||!p.at("velocity").is_array())throw std::runtime_error("invalid sample arrays");
    for(const auto& r:p["tracking"])if(r.size()!=17)throw std::runtime_error("invalid tracking row");
    for(const auto& r:p["velocity"])if(r.size()!=11)throw std::runtime_error("invalid velocity row");
  }
}
void checkOwned(const fs::path& path) {
  if(fs::is_symlink(path))throw std::runtime_error("output must not be a symlink");
  if(!fs::exists(path))return;
  if(!fs::is_directory(path))throw std::runtime_error("output is not a directory");
  if(fs::is_empty(path))return;
  if(!fs::exists(path/".track_insight")||readText(path/".track_insight")!="Track Insight report v1\n")
    throw std::runtime_error("refusing to overwrite a directory not owned by Track Insight");
  const auto manifest=Json::parse(readText(path/"manifest.json"));
  std::set<std::string> owned{".track_insight","manifest.json"};
  for(const auto& f:manifest.at("files"))owned.insert(f.get<std::string>());
  for(const auto& entry:fs::recursive_directory_iterator(path)) {
    if(entry.is_symlink())throw std::runtime_error("output contains symlinks");
    if(entry.is_regular_file()&&!owned.count(fs::relative(entry.path(),path).generic_string()))
      throw std::runtime_error("output contains user-added files; choose a new directory");
  }
}
}
void validateReportOutput(const fs::path& output,bool replace) {
  if(output==output.root_path())throw std::runtime_error("invalid output root");
  checkOwned(output);
  if(fs::exists(output)&&!fs::is_empty(output)&&!replace)throw std::runtime_error("output already exists; use --replace for an owned report");
}
int renderReport(Json analysis,const fs::path& requested,const fs::path& resources,bool replace,
                 const std::function<void(double)>& progress,const std::string& archive_name) {
  if(progress)progress(0);
  validate(analysis);analysis["overview"]=makeOverview(analysis);const auto output=fs::absolute(requested).lexically_normal();
  validateReportOutput(output,replace);fs::create_directories(output.parent_path());
  const auto lock=output.string()+".track_insight.lock";
  if(!fs::create_directory(lock))throw std::runtime_error("another generation holds the output lock");
  const auto staging=output.string()+".track_insight.stage."+std::to_string(getpid());
  const auto backup=output.string()+".track_insight.previous."+std::to_string(getpid());
  if(fs::exists(staging)||fs::exists(backup)){fs::remove(lock);throw std::runtime_error("stale transaction path; choose a new output");}
  fs::create_directory(staging);bool backed_up=false;
  try {
    std::vector<std::string> failures;
    const auto plot_style=Json::parse(readText(resources/"plot_style.json"));
    auto files=writeFigures(analysis,plot_style,staging,failures,[&](double fraction){if(progress)progress(.85*fraction);});
    bool usable=false;
    for(const auto& p:analysis["paths"])for(const auto& name:{"path_distance","vx_residual"})
      if(!p["metrics"][name]["rmse"].is_null())usable=true;
    const bool partial=!failures.empty();
    Json manifest={{"schema_version",1},{"generator","Track Insight"},{"status",partial?"partial":"complete"},
      {"analysis_quality",!usable?"unavailable":analysis["capabilities"]["confirmed_intervals"].get<bool>()?"confirmed_available":"inferred"},
      {"failures",failures},{"files",files},{"source_sha256",analysis["meta"]["input_sha256"]},
      {"metric_version",analysis["metric_version"]},{"plotly_version","2.35.2"}};
    const auto generated_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    manifest["generated_at_ns"]=std::to_string(generated_ns);
    manifest["log_sha256"]=analysis["meta"]["log_sha256"];manifest["effective_config"]=analysis["config"];
    manifest["viewer_dependency"]=Json::parse(readText(resources/"viewer"/"vendor.json"));
    analysis["generation"]={{"status",manifest["status"]},{"failures",failures}};
    Json summary={{"metric_version",analysis["metric_version"]},{"paths",Json::array()},
      {"navigation",analysis["navigation_metrics"]},{"overview",analysis["overview"]}};
    for(const auto& p:analysis["paths"])summary["paths"].push_back({{"interval",p["interval"]},{"metrics",p["metrics"]},{"stages",p["stages"]}});
    writeText(fs::path(staging)/"analysis.json",analysis.dump());
    writeText(fs::path(staging)/"metrics.json",summary.dump(2));
    writeText(fs::path(staging)/"metrics.csv",metricCsv(analysis));
    if(progress)progress(.9);
    std::string payload=analysis.dump();replaceAll(payload,"<","\\u003c");replaceAll(payload,">","\\u003e");replaceAll(payload,"&","\\u0026");
    std::string html=readText(resources/"viewer"/"index.html");
    std::string archive_link;
    if(!archive_name.empty()) {
      auto label=archive_name;replaceAll(label,"&","&amp;");replaceAll(label,"<","&lt;");
      replaceAll(label,">","&gt;");replaceAll(label,"\"","&quot;");
      archive_link="<a id=\"archiveHome\" href=\"../../index.html\" title=\""+label+
        "\">测试总览</a>";
    }
    replaceAll(html,"__TRACK_STYLE__",readText(resources/"viewer"/"viewer.css"));
    replaceAll(html,"__TRACK_PLOTLY__",readText(resources/"viewer"/"plotly.min.js"));
    replaceAll(html,"__TRACK_OVERVIEW_STYLE__",readText(resources/"viewer"/"overview.css"));
    replaceAll(html,"__TRACK_OVERVIEW_SCRIPT__",readText(resources/"viewer"/"overview.js"));
    replaceAll(html,"__TRACK_SCRIPT__",readText(resources/"viewer"/"viewer.js"));
    replaceAll(html,"__TRACK_REVIEW_STYLE__",readText(resources/"viewer"/"review.css"));
    replaceAll(html,"__TRACK_REVIEW_SCRIPT__",readText(resources/"viewer"/"review.js"));
    replaceAll(html,"__TRACK_REVIEW_STATE__",readText(resources/"viewer"/"review_state.js"));
    auto plot_payload=plot_style.dump();replaceAll(plot_payload,"<","\\u003c");
    replaceAll(html,"__TRACK_PLOT_STYLE__",plot_payload);
    replaceAll(html,"__TRACK_DATA__",payload);
    const std::string archive_slot="__TRACK_ARCHIVE_LINK__";const auto slot=html.find(archive_slot);
    if(slot==std::string::npos)throw std::runtime_error("report template is missing its archive slot");
    html.replace(slot,archive_slot.size(),archive_link);
    writeText(fs::path(staging)/"report.html",html);
    for(const auto& name:{"analysis.json","metrics.json","metrics.csv","report.html"})manifest["files"].push_back(name);
    writeText(fs::path(staging)/".track_insight","Track Insight report v1\n");
    writeText(fs::path(staging)/"manifest.json",manifest.dump(2));
    if(progress)progress(.95);
    if(fs::exists(output)){fs::rename(output,backup);backed_up=true;}
    try{fs::rename(staging,output);}catch(...){if(backed_up){fs::rename(backup,output);backed_up=false;}throw;}
    if(backed_up){fs::remove_all(backup);backed_up=false;}
    fs::remove(lock);if(progress)progress(1);return partial?2:0;
  }catch(...) {
    std::error_code ec;fs::remove_all(staging,ec);
    if(backed_up&&!fs::exists(output))fs::rename(backup,output,ec);
    fs::remove(lock,ec);throw;
  }
}
}
