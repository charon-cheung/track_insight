#include "track_insight/input.hpp"
#include "track_insight/report.hpp"
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <iostream>
#include <algorithm>
#include <iomanip>
#include <map>
#include <set>

using namespace track_insight;
namespace {
void usage() {
  std::cout<<"Track Insight 0.1.0\n"
    "  track_insight inspect --bag INPUT [--log nav2.log] [--config FILE]\n"
    "  track_insight analyze --bag INPUT [--log nav2.log] --output DIR [--config FILE] [--replace]\n"
    "  track_insight render --analysis analysis.json --output DIR [--replace]\n"
    "Input: one bag ZIP, bag directory, or MCAP. No ROS node or bag playback.\n"
    "Exit: 0 generated; 2 partial; 1 fatal. Inferred evidence is labeled separately.\n";
}
}
int main(int argc,char** argv) {
  int displayed_progress=-1;
  try {
    if(argc<2||std::string(argv[1])=="--help"){usage();return argc<2?1:0;}
    const std::string command=argv[1];
    if(command!="inspect"&&command!="analyze"&&command!="render")throw std::invalid_argument("unknown command: "+command);
    std::map<std::string,std::string> args;
    const std::set<std::string> allowed=command=="render"?std::set<std::string>{"--analysis","--output","--replace"}:
      command=="inspect"?std::set<std::string>{"--bag","--log","--config"}:
      std::set<std::string>{"--bag","--log","--config","--output","--replace"};
    for(int i=2;i<argc;++i) {
      const std::string key=argv[i];if(!allowed.count(key)||args.count(key))throw std::invalid_argument("invalid or repeated option: "+key);
      if(key=="--replace"){args[key]="true";continue;}
      if(i+1>=argc)throw std::invalid_argument("missing value: "+key);
      args[key]=argv[++i];
    }
    auto require=[&](const std::string& key){if(!args.count(key)||args[key].empty())throw std::invalid_argument("required option: "+key);return args.at(key);};
    const auto resources=std::filesystem::path(ament_index_cpp::get_package_share_directory("track_insight"))/"resources";
    if(command=="render") {
      const auto input=require("--analysis"),output=require("--output");
      const int code=renderReport(Json::parse(readText(input)),output,resources,args.count("--replace"));
      std::cout<<"Report: "<<std::filesystem::absolute(output)/"report.html"<<'\n';return code;
    }
    const auto input=require("--bag");if(command=="analyze")require("--output");
    const auto config=loadConfig(args["--config"]);
    // Weighted work phases: input 60%, evaluation 10%, serialization 5%, report 25%.
    auto progress=[&](double fraction){
      if(command!="analyze")return;
      const int percent=std::clamp(int(fraction*100),0,100);
      if(percent<=displayed_progress)return;
      displayed_progress=percent;const int filled=percent*30/100;
      std::cerr<<'\r'<<'['<<std::string(filled,'=')<<std::string(30-filled,' ')<<"] "
        <<std::setw(3)<<percent<<'%'<<std::flush;
    };
    auto data=readBag(input,args["--log"],config,[&](double fraction){progress(.6*fraction);});
    const auto evaluation=evaluate(data,config);progress(.7);
    auto analysis=makeAnalysis(data,evaluation,config,captureDisplayClock(data.start,data.end,data.wall_clock));
    progress(.75);
    if(command=="inspect") {
      Json info={{"meta",analysis["meta"]},{"topics",analysis["topics"]},{"capabilities",analysis["capabilities"]},
        {"diagnostics",analysis["diagnostics"]},{"path_publications",data.plans.size()},
        {"pose_samples",data.poses.size()},{"command_samples",data.commands.size()},
        {"tracking_raw_samples",data.tracking_raw.size()},{"feedback_samples",data.feedback.size()},{"navigation_tasks",evaluation.tasks.size()}};
      std::cout<<info.dump(2)<<'\n';return data.plans.empty()?2:0;
    }
    const int code=renderReport(std::move(analysis),args["--output"],resources,args.count("--replace"),
      [&](double fraction){progress(.75+.25*fraction);});
    std::cerr<<"\n\n";
    std::cout<<"Report is in: "<<std::filesystem::absolute(args["--output"])/"report.html"
      <<"\nPaths: "<<data.plans.size()<<"; navigation tasks: "<<evaluation.tasks.size()
      <<'\n';
    return code;
  }catch(const std::exception& e){if(displayed_progress>=0)std::cerr<<'\n';std::cerr<<"Track Insight: "<<e.what()<<'\n';return 1;}
}
