#include "track_insight/report.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <regex>

namespace track_insight {
namespace {
std::string xml(const std::string& s) {
  std::string out;for(char c:s)switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;
    case '>':out+="&gt;";break;case '"':out+="&quot;";break;default:out+=c;}return out;
}
std::string num(double x,int precision=2){std::ostringstream o;o<<std::fixed<<std::setprecision(precision)<<x;return o.str();}
struct Canvas {
  std::ostringstream out;
  Canvas(const std::string& title,const std::string& subtitle) {
    out<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1500\" height=\"1050\" viewBox=\"0 0 1500 1050\">"
      "<rect width=\"1500\" height=\"1050\" fill=\"#fff\"/><g font-family=\"DejaVu Sans, sans-serif\">";
    text(55,55,title,27,"#17233a");text(55,89,subtitle,15,"#54657e");
  }
  void text(double x,double y,const std::string& text,int size=13,const std::string& color="#40516b") {
    out<<"<text x=\""<<x<<"\" y=\""<<y<<"\" font-size=\""<<size<<"\" fill=\""<<color<<"\">"<<xml(text)<<"</text>";
  }
  void line(double x1,double y1,double x2,double y2,const std::string& color="#dfe6f0",double width=1) {
    out<<"<path d=\"M "<<x1<<' '<<y1<<" L "<<x2<<' '<<y2<<"\" stroke=\""<<color<<"\" stroke-width=\""<<width<<"\" fill=\"none\"/>";
  }
  void dot(double x,double y,const std::string& color,int r=5) {
    out<<"<circle cx=\""<<x<<"\" cy=\""<<y<<"\" r=\""<<r<<"\" fill=\""<<color<<"\" stroke=\"white\" stroke-width=\"2\"/>";
  }
  std::string finish(const std::string& note) {
    text(55,1000,note,13);text(55,1025,"Recorded localization estimate | Duration-weighted cells | No time-shift correction",12);
    out<<"</g></svg>";return out.str();
  }
};
struct Axis {
  double x,y,w,h,xmin,xmax,ymin,ymax;
  double px(double v)const{return x+(v-xmin)/(xmax-xmin)*w;}
  double py(double v)const{return y+h-(v-ymin)/(ymax-ymin)*h;}
};
void axes(Canvas& c,const Axis& a,const DisplayClock* clock=nullptr) {
  for(int i=0;i<=5;++i) {
    double xv=a.xmin+(a.xmax-a.xmin)*i/5,yv=a.ymin+(a.ymax-a.ymin)*i/5;
    c.line(a.px(xv),a.y,a.px(xv),a.y+a.h);c.line(a.x,a.py(yv),a.x+a.w,a.py(yv));
    std::string label=clock?formatTimestamp(clock->origin_ns+static_cast<TimestampNs>(xv*1e9),*clock,false):num(xv);
    c.text(a.px(xv)-35,a.y+a.h+23,label,12);c.text(a.x-65,a.py(yv)+4,num(yv),12);
  }
}
template<class Valid,class X,class Y>
void polyline(Canvas& c,const Json& rows,const Axis& a,const std::string& color,Valid valid,X x,Y y,
              bool time_series=false,bool step=false) {
  std::ostringstream path;bool connected=false;double previous_end=0,previous_y=0;
  for(const auto& r:rows) {
    if(!valid(r)){connected=false;continue;}
    const double xv=x(r),yv=y(r);
    if(time_series&&connected&&xv-previous_end>1e-8)connected=false;
    if(connected&&step)path<<" L "<<a.px(xv)<<' '<<a.py(previous_y);
    path<<(connected?" L ":" M ")<<a.px(xv)<<' '<<a.py(yv);connected=true;previous_y=yv;
    if(time_series)previous_end=r.at(r.size()==17?13:r.size()==8?6:9).get<double>();
  }
  c.out<<"<path d=\""<<path.str()<<"\" stroke=\""<<color<<"\" stroke-width=\"2\" fill=\"none\"/>";
}
std::string trajectory(const Json& a,const Json& p,const Json& plan,const DisplayClock& clock) {
  const auto& rows=p.at("tracking");const auto& points=plan.at("poses");
  Canvas c("Track Insight | "+plan.at("id").get<std::string>()+" | Planned and recorded trajectory",
    p.at("start_label").get<std::string>()+"  —  "+p.at("end_label").get<std::string>()+" | "+a["meta"]["frame"].get<std::string>());
  double xmin=1e100,xmax=-1e100,ymin=1e100,ymax=-1e100;
  auto bounds=[&](double x,double y){xmin=std::min(xmin,x);xmax=std::max(xmax,x);ymin=std::min(ymin,y);ymax=std::max(ymax,y);};
  for(const auto& r:points)bounds(r[0],r[1]);
  for(const auto& r:rows)if(r[10].get<bool>())bounds(r[1],r[2]);
  if(xmax<xmin){xmin=ymin=0;xmax=ymax=1;}
  const double scale=std::max({(xmax-xmin)/1180,(ymax-ymin)/690,0.001})*1.16;
  const double cx=(xmin+xmax)/2,cy=(ymin+ymax)/2;
  Axis ax{130,175,1180,690,cx-590*scale,cx+590*scale,cy-345*scale,cy+345*scale};axes(c,ax);
  polyline(c,points,ax,"#d3a915",[](const Json&){return true;},[](const Json& r){return r[0].get<double>();},[](const Json& r){return r[1].get<double>();});
  // Break actual trajectory at invalid cells or temporal discontinuities.
  std::ostringstream path;bool connected=false;double end=0;
  const Json* first=nullptr;const Json* last=nullptr;
  for(const auto& r:rows) {
    if(!r[10].get<bool>()){connected=false;continue;}
    if(connected&&r[0].get<double>()>end+1e-8)connected=false;
    path<<(connected?" L ":" M ")<<ax.px(r[1])<<' '<<ax.py(r[2]);connected=true;end=r[13];
    if(!first)first=&r;
    last=&r;
  }
  c.out<<"<path d=\""<<path.str()<<"\" stroke=\"#2274e0\" stroke-width=\"2.5\" fill=\"none\"/>";
  if(!points.empty()) {
    for(const auto* point:{&points.front(),&points.back()}) {
      c.out<<"<circle cx=\""<<ax.px((*point)[0])<<"\" cy=\""<<ax.py((*point)[1])
        <<"\" r=\"9\" fill=\"none\" stroke=\"#c7a11a\" stroke-width=\"2\"/>";
    }
  }
  const std::size_t stride=std::max<std::size_t>(1,rows.size()/7);
  for(std::size_t i=0;i<rows.size();i+=stride)if(rows[i][10].get<bool>()) {
    const auto& r=rows[i];const double angle=r[3],x=ax.px(r[1]),y=ax.py(r[2]);
    const double tx=x+13*std::cos(angle),ty=y-13*std::sin(angle);
    c.line(x,y,tx,ty,"#184581",2);
    c.line(tx,ty,tx-5*std::cos(angle-.5),ty+5*std::sin(angle-.5),"#184581",2);
    c.line(tx,ty,tx-5*std::cos(angle+.5),ty+5*std::sin(angle+.5),"#184581",2);
  }
  const auto& max_metric=p["metrics"]["path_distance"];
  if(!max_metric["max"].is_null())for(const auto& r:rows)if(r[12]==max_metric["max_ns"]) {
    c.dot(ax.px(r[1]),ax.py(r[2]),"#d95663",6);
    c.text(130,957,"Max distance: "+num(max_metric["max"],3)+" m at "+
      formatTimestamp(std::stoll(max_metric["max_ns"].get<std::string>()),clock,false),14,"#ad4351");break;
  }
  if(first) {
    c.dot(ax.px((*first)[1]),ax.py((*first)[2]),"#11a682",7);
    c.text(130,925,"Actual start: "+formatTimestamp(std::stoll((*first)[12].get<std::string>()),clock),14);
  }
  if(last) {
    c.dot(ax.px((*last)[1]),ax.py((*last)[2]),"#e47a2c",7);
    c.text(750,925,"Actual last sample: "+formatTimestamp(std::stoll((*last)[12].get<std::string>()),clock),14);
  }
  c.text(180,135,"Yellow: published path (complete snapshot)",15,"#9c7b00");
  c.text(750,135,"Blue: recorded localization",15,"#2274e0");
  c.text(600,908,a["meta"]["frame"].get<std::string>()+" x / y (m), equal scale",14);
  return c.finish("Association: "+p["interval"]["identity"].get<std::string>()+
    " | Start: "+p["interval"]["start_quality"].get<std::string>()+" | End: "+p["interval"]["end_quality"].get<std::string>());
}
std::string series(const Json& a,const Json& p,const DisplayClock& clock,bool error) {
  Canvas c("Track Insight | "+p["interval"]["plan_id"].get<std::string>()+(error?" | Tracking errors":" | Command and feedback"),
    p["start_label"].get<std::string>()+"  —  "+p["end_label"].get<std::string>());
  const auto& rows=p.at(error?"tracking":"velocity");
  Json commands=Json::array(),feedback=Json::array();
  if(!error&&a.contains("signals"))for(const auto& key:{"commands","feedback"}) {
    auto& target=std::string(key)=="commands"?commands:feedback;
    for(const auto& row:a["signals"][key])if(row[0]>=p["start"]&&row[0]<p["end"])target.push_back(row);
  }
  const std::vector<std::string> names=error?std::vector<std::string>{"Signed lateral (m)","Distance to segment (m)","Target yaw error (rad)"}:
    std::vector<std::string>{"vx (m/s)","vy (m/s)","omega (rad/s)"};
  if(!error){c.text(155,123,"Blue: controller command",14,"#2274e0");c.text(600,123,"Orange: odometry feedback",14,"#e47a2c");}
  for(int panel=0;panel<3;++panel) {
    double lo=0,hi=0;std::size_t count=0;const int index=error?(panel==0?6:panel==1?7:9):panel+1;
    auto valid=[&](const Json& r){return r[error?10:7].get<bool>()&&!r[index].is_null();};
    for(const auto& r:rows)if(valid(r)){lo=std::min(lo,r[index].get<double>());hi=std::max(hi,r[index].get<double>());++count;
      if(!error){lo=std::min(lo,r[index+3].get<double>());hi=std::max(hi,r[index+3].get<double>());}}
    if(!error)for(const auto* signal:{&commands,&feedback})for(const auto& r:*signal)if(r[4].get<bool>()){
      lo=std::min(lo,r[index].get<double>());hi=std::max(hi,r[index].get<double>());++count;
    }
    double pad=std::max((hi-lo)*0.12,0.01);
    Axis ax{140,175.0+panel*265,1220,190,p["start"],p["end"],lo-pad,hi+pad};
    if(ax.xmax<=ax.xmin)ax.xmax=ax.xmin+1;
    c.text(140,ax.y-18,names[panel],16);axes(c,ax,&clock);
    if(count) {
      auto x=[](const Json& r){return r[0].get<double>();};
      if(error)polyline(c,rows,ax,"#2274e0",valid,x,[&](const Json& r){return r[index].get<double>();},true);
      else {
        auto ok=[](const Json& r){return r[4].get<bool>();};
        polyline(c,commands,ax,"#2274e0",ok,x,[&](const Json& r){return r[index].get<double>();},true,true);
        polyline(c,feedback,ax,"#e47a2c",ok,x,[&](const Json& r){return r[index].get<double>();},true);
      }
    }else c.text(400,ax.y+95,error&&panel==2?"Unavailable: no verified target-yaw evidence":"Unavailable: no valid aligned data",18);
  }
  return c.finish("Time: "+std::string(clock.wall_clock?"generation-system local time":"relative ROS time")+" | Gaps are not connected");
}
bool png(const std::filesystem::path& input,const std::filesystem::path& output) {
  const auto pid=fork();if(pid<0)return false;
  if(pid==0){execlp("rsvg-convert","rsvg-convert","--output",output.c_str(),input.c_str(),static_cast<char*>(nullptr));_exit(127);}
  int status=0;while(waitpid(pid,&status,0)<0){if(errno!=EINTR)return false;}
  return WIFEXITED(status)&&WEXITSTATUS(status)==0&&std::filesystem::exists(output)&&std::filesystem::file_size(output)>0;
}
}
std::vector<std::string> writeFigures(const Json& a,const std::filesystem::path& output,std::vector<std::string>& failures) {
  std::filesystem::create_directories(output/"figures");std::vector<std::string> files;
  const auto clock=readClock(a);
  for(const auto& p:a.at("paths")) {
    const Json* plan=nullptr;
    for(const auto& candidate:a.at("plans"))if(candidate["id"]==p["interval"]["plan_id"]){plan=&candidate;break;}
    if(!plan)continue;
    const auto plan_id=p["interval"]["plan_id"].get<std::string>();
    if(!std::regex_match(plan_id,std::regex("P[1-9][0-9]*")))throw std::runtime_error("invalid plan identifier");
    for(const auto& kind:{"trajectory","velocity","errors"}) {
      const std::string stem="figures/"+plan_id+"_"+kind;
      const std::string svg=std::string(kind)=="trajectory"?trajectory(a,p,*plan,clock):series(a,p,clock,std::string(kind)=="errors");
      writeText(output/(stem+".svg"),svg);files.push_back(stem+".svg");
      if(png(output/(stem+".svg"),output/(stem+".png")))files.push_back(stem+".png");
      else {std::filesystem::remove(output/(stem+".png"));failures.push_back(stem+".png: conversion failed");}
    }
  }
  return files;
}
}
