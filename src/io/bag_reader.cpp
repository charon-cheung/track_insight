#include "track_insight/input.hpp"
#include "tf_history.hpp"
#include <rosbag2_cpp/reader.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <nav_msgs/msg/path.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <tf2_msgs/msg/tf_message.hpp>
#include <action_msgs/msg/goal_status_array.hpp>
#include <tf2/LinearMath/Matrix3x3.hpp>
#include <tf2/LinearMath/Quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace track_insight {
namespace {
TimestampNs stamp(const builtin_interfaces::msg::Time& time){return static_cast<TimestampNs>(time.sec)*kSecond+time.nanosec;}
template<typename T>T decode(const rosbag2_storage::SerializedBagMessage& message){
  rclcpp::SerializedMessage bytes(*message.serialized_data);rclcpp::Serialization<T> serializer;T result;
  serializer.deserialize_message(&bytes,&result);return result;
}
Pose pose(const geometry_msgs::msg::Pose& p){
  tf2::Quaternion q(p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w);
  if(!std::isfinite(q.length2())||q.length2()<1e-12)throw std::runtime_error("invalid path quaternion");
  q.normalize();double r,pitch,y;tf2::Matrix3x3(q).getRPY(r,pitch,y);
  if(!std::isfinite(p.position.x)||!std::isfinite(p.position.y))throw std::runtime_error("non-finite path coordinates");
  return {p.position.x,p.position.y,y};
}
Velocity velocity(const geometry_msgs::msg::Twist& twist,const Stamp& s,const std::string& frame){
  Velocity result;result.stamp=s;result.frame=frame;result.reference_point=frame;
  result.vx=twist.linear.x;result.vy=twist.linear.y;result.omega=twist.angular.z;
  result.valid=std::isfinite(result.vx)&&std::isfinite(result.vy)&&std::isfinite(result.omega);
  if(!result.valid)result.reason="non_finite_velocity";
  return result;
}
}
RunData readBag(const std::filesystem::path& input,const std::filesystem::path& log,const Config& config,
                const std::function<void(double)>& progress){
  if(progress)progress(0);
  InputPackage package(input,config);RunData data;data.input_path=std::filesystem::absolute(input).string();
  data.input_hash=hashInput(input);data.frame=config.frame;data.base=config.base;
  rosbag2_cpp::Reader reader;rosbag2_storage::StorageOptions options;options.uri=package.bagPath().string();options.storage_id="mcap";
  reader.open(options);const auto metadata=reader.get_metadata();
  data.start=metadata.starting_time.time_since_epoch().count();data.end=data.start+metadata.duration.count()+1;
  for(const auto& topic:metadata.topics_with_message_count)
    data.topics.push_back({topic.topic_metadata.name,topic.topic_metadata.type,topic.message_count});
  std::map<std::string,std::string> types;
  for(const auto& t:reader.get_all_topics_and_types())types[t.name]=t.type;
  rosbag2_storage::StorageFilter filter;
  filter.topics={config.plan_topic,config.command_topic,config.feedback_topic,config.downstream_topic,config.tracking_raw_topic,"/tf","/tf_static"};
  for(const auto& t:types)if(t.second=="action_msgs/msg/GoalStatusArray"&&
    t.first.find("navigate_to_pose/_action/status")!=std::string::npos)filter.topics.push_back(t.first);
  reader.set_filter(filter);
  std::uint64_t total=0;
  for(const auto& topic:data.topics)
    if(std::find(filter.topics.begin(),filter.topics.end(),topic.name)!=filter.topics.end())total+=topic.count;
  std::map<std::string,int> goal_status;
  TfHistory transforms(config,data.end-data.start);
  std::uint64_t sequence=0;TimestampNs last_record=0;int epoch=0;
  std::map<std::string,TimestampNs> previous_header;std::set<std::string> clock_invalid;
  while(reader.has_next()){
    const auto message=reader.read_next();Stamp s;s.record_ns=message->recv_timestamp;s.send_ns=message->send_timestamp;
    s.ns=s.record_ns;s.sequence=sequence++;s.source=message->topic_name;
    if(progress&&total&&sequence%1024==0)progress(.8*std::min(1.0,double(sequence)/total));
    if(last_record&&s.record_ns<last_record-kSecond){++epoch;data.diagnostics.push_back({"record_clock_reset","Recording time moved backwards",s.source,s.record_ns});}
    last_record=s.record_ns;s.epoch=epoch;
    auto setHeader=[&](const builtin_interfaces::msg::Time& time){
      s.header_ns=stamp(time);s.ns=s.header_ns?s.header_ns:s.record_ns;s.clock_domain=s.header_ns?"message_header":"record_fallback";
      auto& previous=previous_header[s.source];
      if(previous&&s.header_ns&&s.header_ns<previous-kSecond)clock_invalid.insert(s.source);
      previous=s.header_ns;
      if(s.header_ns&&std::abs(s.header_ns-s.record_ns)>5*kSecond)clock_invalid.insert(s.source);
      if(clock_invalid.count(s.source)){s.valid=false;s.clock_domain="unverified"; }
    };
    try{
      const auto& type=types.at(s.source);
      if(s.source==config.plan_topic&&type=="nav_msgs/msg/Path"){
        const auto path=decode<nav_msgs::msg::Path>(*message);Plan plan;plan.id="P"+std::to_string(data.plans.size()+1);
        plan.frame=path.header.frame_id;s.header_ns=stamp(path.header.stamp);plan.stamp=s;
        std::ostringstream signature;signature<<plan.frame<<std::setprecision(17);
        try {
          for(const auto& p:path.poses){
            if(!p.header.frame_id.empty()&&p.header.frame_id!=plan.frame)throw std::runtime_error("inconsistent path pose frame");
            plan.poses.push_back(pose(p.pose));const auto& value=plan.poses.back();
            signature<<'|'<<value.x<<','<<value.y<<','<<value.yaw;}
        }catch(const std::exception& error){plan.reason=error.what();}

        plan.fingerprint=hashText(signature.str());plan.valid=plan.reason.empty()&&!plan.poses.empty()&&plan.frame==config.frame;
        if(!plan.valid&&plan.reason.empty())plan.reason=plan.poses.empty()?"empty_path":"path_frame_mismatch";
        data.plans.push_back(std::move(plan));
      }else if((s.source=="/tf"||s.source=="/tf_static")&&type=="tf2_msgs/msg/TFMessage"){
        const auto tf=decode<tf2_msgs::msg::TFMessage>(*message);
        for(const auto& transform:tf.transforms)transforms.add(transform,s.source=="/tf_static",epoch,s);
      }else if(type=="action_msgs/msg/GoalStatusArray") {
        const auto statuses=decode<action_msgs::msg::GoalStatusArray>(*message);
        for(const auto& value:statuses.status_list) {
          std::ostringstream id;for(auto byte:value.goal_info.goal_id.uuid)id<<std::hex<<std::setw(2)<<std::setfill('0')<<int(byte);
          const auto uuid=id.str();if(goal_status.count(uuid)&&goal_status[uuid]==value.status)continue;
          const auto start=stamp(value.goal_info.stamp);
          if(!goal_status.count(uuid)&&start>=data.start&&start<=s.record_ns)
            data.events.push_back({start,0,epoch,"navigation_start","navigation","Recorded navigation action goal","bag_action_status",{{"uuid",uuid}}});
          goal_status[uuid]=value.status;
          const std::string kind=value.status==4?"navigation_succeeded":value.status==5?"navigation_canceled":value.status==6?"navigation_failed":"";
          if(!kind.empty())data.events.push_back({s.record_ns,0,epoch,kind,"navigation","Recorded navigation action status","bag_action_status",{{"uuid",uuid}}});
        }
      }else if(s.source==config.feedback_topic&&type=="nav_msgs/msg/Odometry"){
        const auto odom=decode<nav_msgs::msg::Odometry>(*message);setHeader(odom.header.stamp);
        data.feedback.push_back(velocity(odom.twist.twist,s,odom.child_frame_id));
      }else if(type=="geometry_msgs/msg/TwistStamped"||type=="geometry_msgs/msg/Twist"){
        Velocity v;
        if(type=="geometry_msgs/msg/TwistStamped"){
          const auto twist=decode<geometry_msgs::msg::TwistStamped>(*message);
          // The smoother retains the input header; its output is timed by bag receipt.
          if(s.source==config.downstream_topic){s.header_ns=stamp(twist.header.stamp);s.clock_domain="bag_record";}
          else setHeader(twist.header.stamp);
          v=velocity(twist.twist,s,twist.header.frame_id.empty()?config.command_frame:twist.header.frame_id);
        }else v=velocity(decode<geometry_msgs::msg::Twist>(*message),s,config.command_frame);
        if(s.source==config.command_topic)data.commands.push_back(v);
        if(s.source==config.downstream_topic){v.stamp.clock_domain="bag_record";data.downstream.push_back(v);}
        if(s.source==config.tracking_raw_topic)data.tracking_raw.push_back(v);
      }else data.diagnostics.push_back({"unsupported_type","Selected topic has unsupported message type: "+type,s.source,s.ns});
    }catch(const std::exception& error){
      data.diagnostics.push_back({"decode_error",error.what(),s.source,s.record_ns});
      if(s.source==config.plan_topic){Plan invalid;invalid.id="P"+std::to_string(data.plans.size()+1);
        invalid.stamp=s;invalid.valid=false;invalid.reason="path_decode_error";data.plans.push_back(std::move(invalid));}
    }
  }
  if(progress)progress(.8);
  transforms.finish();data.poses=transforms.poses();
  if(transforms.hasClockReset())data.diagnostics.push_back({"tf_header_clock_reset","TF header time reset; affected chains excluded rather than merged","/tf",0});
  for(auto* values:{&data.commands,&data.feedback,&data.downstream,&data.tracking_raw}){
    for(auto& v:*values){if(clock_invalid.count(v.stamp.source))v.stamp.valid=false;transforms.convert(v);}
    std::stable_sort(values->begin(),values->end(),[](const auto& a,const auto& b){
      return a.stamp.ns==b.stamp.ns?a.stamp.sequence<b.stamp.sequence:a.stamp.ns<b.stamp.ns;});
  }
  for(const auto& topic:clock_invalid)data.diagnostics.push_back({"clock_domain_unverified","Header time is discontinuous or incompatible with recording time",topic,0});
  for(const auto& p:data.poses)if(!p.valid&&p.reason=="localization_jump")
    data.diagnostics.push_back({"localization_jump","Recorded localization discontinuity; affected cells excluded from geometry metrics","/tf",p.stamp.ns});
  data.wall_clock=data.start>=946684800LL*kSecond;
  if(log.empty())data.diagnostics.push_back({"log_missing","No matching nav2.log supplied; execution windows are inferred","",0});
  else if(!std::filesystem::exists(log))data.diagnostics.push_back({"log_missing","Requested nav2.log is unavailable",log.string(),0});
  else {
    data.log_path=std::filesystem::absolute(log).string();data.log_hash=hashInput(log);auto log_events=parseLog(log);
    std::size_t overlap=0,anchors=0;
    for(const auto& e:log_events) {
      if(e.ns<data.start||e.ns>data.end)continue;
      ++overlap;
      if(e.kind=="handoff"&&e.fields.count("path_stamp_ns")&&e.fields.count("poses")&&e.fields.count("frame"))try {
        for(const auto& p:data.plans)if(p.stamp.header_ns==std::stoll(e.fields.at("path_stamp_ns"))&&
          p.poses.size()==std::stoull(e.fields.at("poses"))&&p.frame==e.fields.at("frame"))anchors+=2;
      }catch(...){}
      if(e.kind=="tracking_state"&&e.fields.count("robot_x")&&e.fields.count("robot_y"))try {
        const double x=std::stod(e.fields.at("robot_x")),y=std::stod(e.fields.at("robot_y"));
        auto p=std::lower_bound(data.poses.begin(),data.poses.end(),e.ns,[](const PoseSample& p,TimestampNs t){return p.stamp.ns<t;});
        if(p!=data.poses.end()&&p->valid&&p->stamp.valid&&std::abs(p->stamp.ns-e.ns)<=100000000&&
           std::hypot(p->pose.x-x,p->pose.y-y)<=0.10)++anchors;
      }catch(...){}
    }
    data.log_compatible=anchors>=2;
    if(!data.log_compatible)data.diagnostics.push_back({overlap?"log_identity_unverified":"log_time_mismatch",
      overlap?"Time overlaps but no sufficient shared path or repeated pose evidence; log association disabled":
      "No parsed log events overlap the recording; log association disabled",log.string(),0});
    data.events.insert(data.events.end(),log_events.begin(),log_events.end());
  }
  std::stable_sort(data.events.begin(),data.events.end(),[](const auto& a,const auto& b){return a.ns<b.ns;});
  if(data.plans.empty())data.diagnostics.push_back({"paths_missing","No validly decoded path publications",config.plan_topic,0});
  if(data.commands.empty())data.diagnostics.push_back({"command_missing","No controller commands recorded",config.command_topic,0});
  if(data.feedback.empty())data.diagnostics.push_back({"feedback_missing","No odometry velocity feedback recorded",config.feedback_topic,0});
  if(data.poses.empty())data.diagnostics.push_back({"poses_missing","No dynamic TF timestamps recorded","/tf",0});
  if(progress)progress(1);
  return data;
}
}
