#include "track_insight/archive.hpp"
#include <algorithm>
#include <array>
#include <regex>
#include <set>
#include <unistd.h>

namespace track_insight {
namespace fs=std::filesystem;
namespace {
constexpr auto marker="Track Insight archive v1\n";
struct ArchiveLock {
  fs::path path;
  explicit ArchiveLock(const fs::path& root):path(root.string()+".track_insight.archive.lock") {
    fs::create_directories(root.parent_path());
    if(!fs::create_directory(path))throw std::runtime_error("another generation holds the archive lock");
  }
  ~ArchiveLock(){std::error_code ec;fs::remove(path,ec);}
};
bool hashValid(const std::string& value) {
  return value.size()==64&&value.find_first_not_of("0123456789abcdef")==std::string::npos;
}
void regular(const fs::path& path) {
  if(fs::is_symlink(path)||!fs::is_regular_file(path))throw std::runtime_error("expected a regular archive file: "+path.string());
}
Json loadCatalog(const fs::path& root) {
  Json empty={{"schema_version",1},{"generator","Track Insight archive"},{"runs",Json::array()}};
  if(fs::is_symlink(root))throw std::runtime_error("archive must not be a symlink");
  if(!fs::exists(root))return empty;
  if(!fs::is_directory(root))throw std::runtime_error("archive is not a directory");
  if(fs::is_empty(root))return empty;
  regular(root/".track_insight_archive");
  if(readText(root/".track_insight_archive")!=marker)throw std::runtime_error("directory is not a Track Insight archive");
  const std::set<std::string> allowed{".track_insight_archive","runs","runs.json","index.html"};
  for(const auto& entry:fs::directory_iterator(root))
    if(entry.is_symlink()||!allowed.count(entry.path().filename().string()))
      throw std::runtime_error("archive contains unmanaged entries: "+entry.path().string());
  if(!fs::is_directory(root/"runs"))throw std::runtime_error("archive runs directory is missing");
  if(!fs::exists(root/"runs.json")&&!fs::exists(root/"index.html")&&fs::is_empty(root/"runs"))return empty;
  regular(root/"runs.json");regular(root/"index.html");
  auto catalog=Json::parse(readText(root/"runs.json"));
  if(catalog.at("schema_version")!=1||catalog.at("generator")!="Track Insight archive"||!catalog.at("runs").is_array())
    throw std::runtime_error("unsupported archive index");
  std::set<std::string> ids,hashes;
  for(const auto& row:catalog["runs"]) {
    const auto id=row.at("id").get<std::string>(),hash=row.at("source_sha256").get<std::string>();
    if(!std::regex_match(id,std::regex("run_-?[0-9]+_[a-f0-9]{12}"))||!hashValid(hash)||
       !ids.insert(id).second||!hashes.insert(hash).second||row.at("report")!="runs/"+id+"/report.html")
      throw std::runtime_error("invalid or duplicate archive identity");
    std::stoll(row.at("start_ns").get<std::string>());
    validateReportOutput(root/"runs"/id,true);
    regular(root/"runs"/id/"report.html");
    const auto manifest=Json::parse(readText(root/"runs"/id/"manifest.json"));
    if(manifest.at("source_sha256")!=hash||manifest.at("metric_version")!=row.at("metric_version")||
       manifest.at("status")!=row.at("status")||manifest.at("analysis_quality")!=row.at("analysis_quality"))
      throw std::runtime_error("archive index does not match the published round manifest");
    for(const auto& key:{"name","note","start_label","end_label","metric_version"})row.at(key).get<std::string>();
    const auto& overview=row.at("overview");
    if(!row.at("paths").is_number_unsigned()||!row.at("tasks").is_number_unsigned()||
       !overview.at("case_ids").is_array()||!overview.at("ranked_cases").is_number_unsigned()||
       !overview.at("averages").is_object()||!row.at("worst").is_array())
      throw std::runtime_error("invalid archive summary fields");
    for(const auto& worst:row["worst"]) {
      worst.at("plan_id").get<std::string>();
      if(!worst.at("rmse").is_number())throw std::runtime_error("invalid archive worst-case metric");
    }
    if(overview["averages"].contains("lateral"))for(const auto& key:{"case_mean_rmse","pooled_rmse","coverage"}) {
      const auto& value=overview["averages"]["lateral"].at(key);
      if(!value.is_null()&&!value.is_number())throw std::runtime_error("invalid archive lateral metric");
    }
  }
  for(const auto& entry:fs::directory_iterator(root/"runs"))
    if(!ids.count(entry.path().filename().string()))throw std::runtime_error("archive contains an unindexed round; choose a new archive");
  return catalog;
}
Json summary(const Json& a,const std::string& id,const std::string& name,const std::string& note) {
  const auto& meta=a.at("meta");const auto overview=makeOverview(a);
  Json row={{"id",id},{"name",name},{"note",note},{"report","runs/"+id+"/report.html"},
    {"source_sha256",meta.at("input_sha256")},{"start_ns",meta.at("start_ns")},{"end_ns",meta.at("end_ns")},
    {"start_label",meta.at("start_label")},{"end_label",meta.at("end_label")},
    {"paths",a.at("plans").size()-overview.at("excluded_straight_plan_ids").size()},{"tasks",a.at("tasks").size()},
    {"metric_version",a.at("metric_version")},{"overview",overview},{"worst",Json::array()}};
  for(const auto& worst:overview.at("worst_ids"))for(const auto& p:a.at("paths"))if(p.at("interval").at("id")==worst)
    row["worst"].push_back({{"plan_id",p["interval"]["plan_id"]},{"rmse",p["metrics"]["lateral"]["rmse"]}});
  return row;
}
std::string homepage(const Json& catalog,const fs::path& resources) {
  std::string payload=catalog.dump(),escaped;
  for(char c:payload) {
    if(c=='<')escaped+="\\u003c";else if(c=='>')escaped+="\\u003e";else if(c=='&')escaped+="\\u0026";else escaped+=c;
  }
  auto html=readText(resources/"archive"/"index.html");
  const std::string token="__TRACK_ARCHIVE_DATA__";const auto at=html.find(token);
  if(at==std::string::npos)throw std::runtime_error("archive template is missing its data slot");
  html.replace(at,token.size(),escaped);return html;
}
}
ArchiveResult renderArchive(Json analysis,const fs::path& requested,const fs::path& resources,bool replace,
  const std::optional<std::string>& name,const std::optional<std::string>& note,const std::function<void(double)>& progress) {
  auto root=fs::absolute(requested).lexically_normal();
  if(root!=root.root_path()&&root.filename().empty())root=root.parent_path();
  if(root==root.root_path())throw std::runtime_error("invalid archive root");
  const auto hash=analysis.at("meta").at("input_sha256").get<std::string>();
  const auto start=analysis.at("meta").at("start_ns").get<std::string>();
  if(!hashValid(hash)||!std::regex_match(start,std::regex("-?[0-9]{1,19}")))throw std::runtime_error("invalid source identity for archive");
  std::stoll(start);
  if(name&&name->find_first_not_of(" \t\r\n")==std::string::npos)throw std::runtime_error("round name must not be empty");
  ArchiveLock lock(root);auto catalog=loadCatalog(root);
  auto found=std::find_if(catalog["runs"].begin(),catalog["runs"].end(),[&](const Json& row){return row["source_sha256"]==hash;});
  const bool exists=found!=catalog["runs"].end();
  if(exists&&!replace)throw std::runtime_error("round already exists; use --replace to update only this round");
  const auto id=exists?found->at("id").get<std::string>():"run_"+start+"_"+hash.substr(0,12);
  const auto output=root/"runs"/id;
  if(!exists&&fs::exists(output))throw std::runtime_error("round ID collision; refusing to overwrite");
  auto input=fs::path(analysis.at("meta").at("input_path").get<std::string>()).lexically_normal();
  if(input.filename().empty())input=input.parent_path();
  const auto default_name=input.filename().empty()?id:input.filename().string();
  auto row=summary(analysis,id,name.value_or(exists?found->at("name").get<std::string>():default_name),
    note.value_or(exists?found->at("note").get<std::string>():""));
  // Validate the published destination before staging, including user-added files.
  validateReportOutput(output,replace);
  const auto stage=fs::path(root.string()+".archive.stage."+std::to_string(getpid()));
  if(fs::exists(stage)||fs::is_symlink(stage))throw std::runtime_error("stale archive transaction; choose a new archive");
  fs::create_directory(stage);
  std::array<bool,3> backed{},published{};
  const std::array<fs::path,3> targets{output,root/"runs.json",root/"index.html"};
  const std::array<fs::path,3> sources{stage/"round",stage/"runs.json",stage/"index.html"};
  try {
    const int code=renderReport(std::move(analysis),stage/"round",resources,false,
      [&](double fraction){if(progress)progress(.95*fraction);},row["name"].get<std::string>());
    const auto manifest=Json::parse(readText(stage/"round"/"manifest.json"));
    row["status"]=manifest.at("status");row["analysis_quality"]=manifest.at("analysis_quality");
    if(exists)*found=row;else catalog["runs"].push_back(row);
    std::stable_sort(catalog["runs"].begin(),catalog["runs"].end(),[](const Json& a,const Json& b){
      const auto x=std::stoll(a["start_ns"].get<std::string>()),y=std::stoll(b["start_ns"].get<std::string>());
      return x!=y?x>y:a["id"].get<std::string>()<b["id"].get<std::string>();
    });
    writeText(stage/"runs.json",catalog.dump(2));writeText(stage/"index.html",homepage(catalog,resources));
    fs::create_directories(root/"runs");
    if(!fs::exists(root/".track_insight_archive"))writeText(root/".track_insight_archive",marker);
    // Publish completed artifacts only. Ordinary failures restore all three previous targets.
    for(std::size_t i=0;i<targets.size();++i) {
      if(fs::exists(targets[i])){fs::rename(targets[i],stage/("previous_"+std::to_string(i)));backed[i]=true;}
      fs::rename(sources[i],targets[i]);published[i]=true;
    }
    if(progress)progress(1);
    std::error_code ec;fs::remove_all(stage,ec);
    return {code,output/"report.html",root/"index.html"};
  }catch(...) {
    bool restored=true;
    for(int i=2;i>=0;--i) {
      std::error_code ec;
      if(published[i])fs::remove_all(targets[i],ec);
      if(!ec&&backed[i])fs::rename(stage/("previous_"+std::to_string(i)),targets[i],ec);
      if(ec)restored=false;
    }
    if(restored){std::error_code ec;fs::remove_all(stage,ec);}
    else throw std::runtime_error("archive rollback incomplete; preserved recovery files in "+stage.string());
    throw;
  }
}
}
