#include "track_insight/domain.hpp"
#include <fstream>
#include <regex>
#include <stdexcept>

namespace track_insight {
std::vector<LogEvent> parseLog(const std::filesystem::path& path) {
  std::ifstream input(path);if(!input)throw std::runtime_error("cannot read log: "+path.string());
  const std::regex ansi("\x1b\\[[0-9;]*[A-Za-z]");
  const std::regex timestamp(R"(\[([0-9]+\.[0-9]+)\])");
  const std::regex field(R"(([A-Za-z_][A-Za-z_0-9]*)=([^\s,}]+))");
  const std::regex goal(R"(to \(([-+0-9.eE]+),\s*([-+0-9.eE]+)\))");
  std::vector<LogEvent> events;std::string line;std::size_t number=0;TimestampNs previous=0;int session=0;
  std::optional<unsigned long> revision;
  while(std::getline(input,line)) {
    ++number;line=std::regex_replace(line,ansi,"");std::smatch match;
    if(!std::regex_search(line,match,timestamp))continue;
    LogEvent event;try{event.ns=parseTimestampNs(match[1].str());}catch(...){continue;}
    if(previous&&event.ns<previous-kSecond){++session;revision.reset();}
    previous=event.ns;event.line=number;event.raw=line;event.source=path.string();
    for(auto it=std::sregex_iterator(line.begin(),line.end(),field);it!=std::sregex_iterator();++it)
      event.fields[(*it)[1].str()]=(*it)[2].str();
    const bool navigator=line.find("bt_navigator")!=std::string::npos;
    if(line.find("Begin navigating from current location")!=std::string::npos){
      event.kind="navigation_start";
      if(std::regex_search(line,match,goal)){event.fields["goal_x"]=match[1].str();event.fields["goal_y"]=match[2].str();}
    }else if(navigator&&(line.find("Goal succeeded")!=std::string::npos||line.find("Navigation succeeded")!=std::string::npos))event.kind="navigation_succeeded";
    else if(navigator&&(line.find("Goal was canceled")!=std::string::npos||line.find("Navigation canceled")!=std::string::npos||line.find("Goal canceled")!=std::string::npos))event.kind="navigation_canceled";
    else if(navigator&&(line.find("Goal failed")!=std::string::npos||line.find("Navigation failed")!=std::string::npos||line.find("Aborting")!=std::string::npos))event.kind="navigation_failed";
    else if(line.find("RBS lattice current request")!=std::string::npos)event.kind="planning_request";
    else if(line.find("lattice_summary")!=std::string::npos)event.kind="planning_summary";
    else if(line.find("PID_LOG path_handoff_rejected")!=std::string::npos)event.kind="handoff_rejected";
    else if(line.find("PID_LOG path_handoff")!=std::string::npos)event.kind="handoff";
    else if(line.find("PID_LOG tracking_state")!=std::string::npos)event.kind="tracking_state";
    else if(line.find("PID_LOG holonomic")!=std::string::npos)event.kind="control_state";
    else if(line.find("PID_LOG rotation_ack")!=std::string::npos)event.kind="rotation_ack";
    else if(line.find("PidTrackingController received a path")!=std::string::npos)event.kind="path_received";
    else if(line.find("ERROR")!=std::string::npos||line.find("WARN")!=std::string::npos){event.kind="recorded_warning_or_error";event.category="navigation";}
    else if(line.find("PID_LOG")!=std::string::npos)event.kind="unclassified_controller_event";
    else continue;
    if(event.kind=="handoff"&&event.fields.count("plan_revision")){
      try{auto value=std::stoul(event.fields.at("plan_revision"));if(revision&&value<=*revision)++session;revision=value;}catch(...){}
    }
    event.session=session;events.push_back(std::move(event));
  }
  return events;
}

}  // namespace track_insight
