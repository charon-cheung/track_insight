#include "track_insight/input.hpp"
#include "track_insight/report.hpp"
#include "track_insight/archive.hpp"
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <iostream>
#include <algorithm>
#include <iomanip>
#include <map>
#include <set>
#include <unistd.h>

using namespace track_insight;
namespace {
void usage() {
  std::cout<<"Track Insight 0.1.0\n"
    "  track_insight inspect --bag INPUT [--log nav2.log] [--config FILE]\n"
    "  track_insight analyze --bag INPUT [--log nav2.log] --output DIR [--config FILE] [--replace]\n"
    "  track_insight render --analysis analysis.json --output DIR [--replace]\n"
    "  track_insight analyze --bag INPUT [--log nav2.log] --archive DIR [--name NAME] [--note TEXT] [--config FILE] [--replace]\n"
    "  track_insight render --analysis analysis.json --archive DIR [--name NAME] [--note TEXT] [--replace]\n"
    "Archive: append each run; --replace updates only the matching bag. --output and --archive are exclusive.\n"
    "Input: one bag ZIP, bag directory, or MCAP. No ROS node or bag playback.\n"
    "Exit: 0 generated; 2 partial; 1 fatal. Inferred evidence is labeled separately.\n";
}
}
int main(int argc,char** argv) {
  int displayed_progress=-1;
  const bool color_progress=isatty(STDERR_FILENO);
  try {
    if(argc<2||std::string(argv[1])=="--help"){usage();return argc<2?1:0;}
    const std::string command=argv[1];
    if(command!="inspect"&&command!="analyze"&&command!="render")throw std::invalid_argument("unknown command: "+command);
    std::map<std::string,std::string> args;
    const std::set<std::string> allowed=command=="render"?std::set<std::string>{"--analysis","--output","--replace","--archive","--name","--note"}:
      command=="inspect"?std::set<std::string>{"--bag","--log","--config"}:
      std::set<std::string>{"--bag","--log","--config","--output","--replace","--archive","--name","--note"};
    for(int i=2;i<argc;++i) {
      const std::string key=argv[i];if(!allowed.count(key)||args.count(key))throw std::invalid_argument("invalid or repeated option: "+key);
      if(key=="--replace"){args[key]="true";continue;}
      if(i+1>=argc)throw std::invalid_argument("missing value: "+key);
      args[key]=argv[++i];
    }
    auto require=[&](const std::string& key){if(!args.count(key)||args[key].empty())throw std::invalid_argument("required option: "+key);return args.at(key);};
    if(command!="inspect") {
      if(args.count("--archive")&&args.count("--output"))throw std::invalid_argument("--archive and --output are mutually exclusive");
      if(!args.count("--archive")&&(args.count("--name")||args.count("--note")))throw std::invalid_argument("--name and --note require --archive");
      require(args.count("--archive")?"--archive":"--output");
    }
    const auto resources=std::filesystem::path(ament_index_cpp::get_package_share_directory("track_insight"))/"resources";
    auto generate=[&](Json analysis,const std::function<void(double)>& progress={}) {
      if(args.count("--archive"))return renderArchive(std::move(analysis),args.at("--archive"),resources,args.count("--replace"),
        args.count("--name")?std::optional<std::string>(args.at("--name")):std::nullopt,
        args.count("--note")?std::optional<std::string>(args.at("--note")):std::nullopt,progress);
      const auto output=std::filesystem::absolute(args.at("--output"));
      return ArchiveResult{renderReport(std::move(analysis),output,resources,args.count("--replace"),progress),output/"report.html",{}};
    };
    if(command=="render") {
      const auto result=generate(Json::parse(readText(require("--analysis"))));
      if(!result.home.empty())std::cout<<"Overview is in: "<<result.home<<'\n';
      std::cout<<"Report: "<<result.report<<'\n';return result.code;
    }
    const auto input=require("--bag");
    const auto config=loadConfig(args["--config"]);
    // Weighted work phases: input 60%, evaluation 10%, serialization 5%, report 25%.
    auto progress=[&](double fraction){
      if(command!="analyze")return;
      const int percent=std::clamp(int(fraction*100),0,100);
      if(percent<=displayed_progress)return;
      displayed_progress=percent;const int filled=percent*30/100;
      std::cerr<<'\r'<<'['<<(color_progress?"\033[38;2;147;112;219m":"")<<std::string(filled,'=')
        <<(color_progress?"\033[0m":"")<<std::string(30-filled,' ')<<"] "
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
    const auto result=generate(std::move(analysis),[&](double fraction){progress(.75+.25*fraction);});
    std::cerr<<"\n\n";
    if(!result.home.empty())std::cout<<"Overview is in: "<<result.home<<'\n';
    std::cout<<"Report is in: "<<result.report
      <<"\nPaths: "<<data.plans.size()<<"; navigation tasks: "<<evaluation.tasks.size()
      <<'\n';
    return result.code;
  }catch(const std::exception& e){if(displayed_progress>=0)std::cerr<<'\n';std::cerr<<"Track Insight: "<<e.what()<<'\n';return 1;}
}
