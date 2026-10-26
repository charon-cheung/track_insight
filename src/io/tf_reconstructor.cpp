#include "tf_history.hpp"
#include <tf2/LinearMath/Quaternion.hpp>
#include <tf2/LinearMath/Matrix3x3.hpp>
#include <algorithm>
#include <cmath>
#include <deque>

namespace track_insight {
namespace {
TimestampNs stamp(const builtin_interfaces::msg::Time& time){return static_cast<TimestampNs>(time.sec)*kSecond+time.nanosec;}
double yaw(const geometry_msgs::msg::Quaternion& q) {
  tf2::Quaternion rotation(q.x,q.y,q.z,q.w);
  if(!std::isfinite(rotation.length2())||rotation.length2()<1e-12)throw std::runtime_error("invalid quaternion");
  rotation.normalize();double roll,pitch,result;tf2::Matrix3x3(rotation).getRPY(roll,pitch,result);return result;
}
}
TfHistory::TfHistory(const Config& config,TimestampNs duration):config_(config),duration_(duration){}
void TfHistory::add(const geometry_msgs::msg::TransformStamped& transform,bool fixed,int epoch,const Stamp& source){records_.push_back({transform,fixed,epoch,source});}
void TfHistory::finish(){
  std::map<std::pair<int,std::string>,TimestampNs> previous_header;
  for(const auto& r:records_)if(!r.fixed) {
    const auto ns=stamp(r.transform.header.stamp);auto& previous=previous_header[{r.epoch,r.transform.child_frame_id}];
    if(previous&&ns<previous-kSecond)clock_resets_.insert(r.transform.child_frame_id);
    previous=ns;
  }
  std::set<int> epochs{0};for(const auto& r:records_)epochs.insert(r.epoch);
  for(int epoch:epochs){
    buffers_[epoch]=std::make_unique<tf2::BufferCore>(std::chrono::nanoseconds(std::max<TimestampNs>(duration_+5*kSecond,10*kSecond)));
    auto& edges=edges_[epoch];
    for(const auto& r:records_){
      if(!r.fixed&&r.epoch!=epoch)continue;
      const auto& t=r.transform;auto& edge=edges[t.child_frame_id];
      if(!edge.parent.empty()&&edge.parent!=t.header.frame_id)edge.conflict=true;
      if(clock_resets_.count(t.child_frame_id))edge.conflict=true;
      edge.parent=t.header.frame_id;edge.fixed=r.fixed;edge.times.push_back(stamp(t.header.stamp));
      try{if(!buffers_[epoch]->setTransform(t,"bag",r.fixed))edge.conflict=true;}catch(...){edge.conflict=true;}
    }
    for(auto& item:edges)std::sort(item.second.times.begin(),item.second.times.end());
  }
}
bool TfHistory::validChain(const std::string& target,const std::string& source,TimestampNs ns,int epoch)const{
  if(target==source)return true;
  auto found=edges_.find(epoch);if(found==edges_.end()||ns==0)return false;
  std::deque<std::string> queue{source};std::set<std::string> visited{source};
  while(!queue.empty()){
    const auto current=queue.front();queue.pop_front();
    for(const auto& item:found->second){
      const auto& e=item.second;std::string next;
      if(current==item.first)next=e.parent;else if(current==e.parent)next=item.first;else continue;
      if(e.conflict||visited.count(next))continue;
      bool valid=e.fixed;
      if(!valid){
        auto right=std::lower_bound(e.times.begin(),e.times.end(),ns);
        valid=right!=e.times.end()&&(*right==ns||(right!=e.times.begin()&&*right-*std::prev(right)<=config_.tf_gap));
      }
      if(!valid)continue;
      if(next==target)return true;
      visited.insert(next);queue.push_back(next);
    }
  }
  return false;
}
std::vector<PoseSample> TfHistory::poses(){
  std::set<std::pair<int,TimestampNs>> times;
  for(const auto& r:records_)if(!r.fixed)times.emplace(r.epoch,stamp(r.transform.header.stamp));
  std::vector<PoseSample> result;
  std::map<std::pair<int,TimestampNs>,Stamp> provenance;
  for(const auto& r:records_)if(!r.fixed)provenance.emplace(std::make_pair(r.epoch,stamp(r.transform.header.stamp)),r.source);
  for(const auto& point:times){
    PoseSample sample;sample.stamp.ns=point.second;sample.stamp.header_ns=point.second;
    sample.stamp.epoch=point.first;sample.stamp.source="/tf";
    sample.stamp.clock_domain="message_header";sample.valid=false;sample.reason="missing_or_stale_tf_chain";
    if(validChain(config_.frame,config_.base,point.second,point.first)){
      try{
        const auto transform=buffers_.at(point.first)->lookupTransform(config_.frame,config_.base,
          tf2::TimePoint(std::chrono::nanoseconds(point.second)));
        sample.pose={transform.transform.translation.x,transform.transform.translation.y,yaw(transform.transform.rotation)};
        sample.valid=std::isfinite(sample.pose.x)&&std::isfinite(sample.pose.y)&&std::isfinite(sample.pose.yaw);
        if(sample.valid)sample.reason.clear();
      }catch(...){}
    }
    const auto& source=provenance.at(point);
    sample.stamp.record_ns=source.record_ns;sample.stamp.send_ns=source.send_ns;
    sample.stamp.sequence=source.sequence;
    if(source.record_ns&&std::abs(point.second-source.record_ns)>5*kSecond) {
      sample.valid=false;sample.stamp.valid=false;sample.reason="tf_clock_domain_unverified";
    }
    result.push_back(sample);
  }
  for(std::size_t i=1;i<result.size();++i){
    auto& previous=result[i-1];auto& current=result[i];
    const double dt=static_cast<double>(current.stamp.ns-previous.stamp.ns)/kSecond;
    const double distance=std::hypot(current.pose.x-previous.pose.x,current.pose.y-previous.pose.y);
    if(current.valid&&previous.valid&&current.stamp.epoch==previous.stamp.epoch&&dt>0&&
       distance>config_.jump_distance&&distance/dt>config_.max_position_jump_speed){
      previous.valid=false;previous.reason="localization_jump_boundary";
      current.valid=false;current.reason="localization_jump";
    }
  }
  std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.stamp.ns<b.stamp.ns;});
  return result;
}
void TfHistory::convert(Velocity& v){
  if(v.frame==config_.base){v.reference_point=config_.base;return;}
  if(!validChain(config_.base,v.frame,v.stamp.ns,v.stamp.epoch)){
    v.valid=false;v.reason="missing_velocity_frame_transform";return;
  }
  try{
    const auto t=buffers_.at(v.stamp.epoch)->lookupTransform(config_.base,v.frame,
      tf2::TimePoint(std::chrono::nanoseconds(v.stamp.ns)));
    const auto& rotation=t.transform.rotation;
    if(std::abs(rotation.x)>1e-6||std::abs(rotation.y)>1e-6)throw std::runtime_error("non-planar twist transform unsupported");
    const auto angle=yaw(rotation);
    const double x=std::cos(angle)*v.vx-std::sin(angle)*v.vy+v.omega*t.transform.translation.y;
    const double y=std::sin(angle)*v.vx+std::cos(angle)*v.vy-v.omega*t.transform.translation.x;
    v.vx=x;v.vy=y;v.frame=config_.base;v.reference_point=config_.base;
  }catch(...){v.valid=false;v.reason="velocity_transform_failed";}
}
}
