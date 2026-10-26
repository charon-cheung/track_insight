#include "track_insight/domain.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace track_insight {

double wrapAngle(double angle) { return std::atan2(std::sin(angle),std::cos(angle)); }

TimestampNs parseTimestampNs(const std::string& text) {
  if(text.empty()) throw std::invalid_argument("empty timestamp");
  const bool negative=text[0]=='-';
  const std::size_t begin=(negative||text[0]=='+')?1:0;
  const auto dot=text.find('.',begin);
  const auto integer=text.substr(begin,dot==std::string::npos?dot:dot-begin);
  auto fraction=dot==std::string::npos?std::string{}:text.substr(dot+1);
  if(integer.empty()||fraction.size()>9) throw std::invalid_argument("invalid timestamp precision");
  std::string digits=integer+fraction+std::string(9-fraction.size(),'0');
  std::uint64_t result=0;
  const auto limit=static_cast<std::uint64_t>(std::numeric_limits<TimestampNs>::max())+(negative?1ULL:0ULL);
  for(char c:digits) {
    if(c<'0'||c>'9'||result>(limit-static_cast<unsigned>(c-'0'))/10)
      throw std::invalid_argument("invalid or overflowing timestamp");
    result=result*10+static_cast<unsigned>(c-'0');
  }
  if(negative && result==limit) return std::numeric_limits<TimestampNs>::min();
  return negative?-static_cast<TimestampNs>(result):static_cast<TimestampNs>(result);
}

static int localOffset(TimestampNs ns) {
  const std::time_t sec=ns/kSecond;
  std::tm local{};
  if(!localtime_r(&sec,&local)) throw std::runtime_error("cannot resolve system local time");
  return static_cast<int>(local.tm_gmtoff);
}

DisplayClock captureDisplayClock(TimestampNs start,TimestampNs end,bool wall_clock) {
  DisplayClock clock{start,wall_clock,{}};
  if(!wall_clock) return clock;
  clock.offsets.push_back({start,localOffset(start)});
  for(TimestampNs left=start;left<end;) {
    const auto right=left+std::min<TimestampNs>(3600*kSecond,end-left);
    const int offset=localOffset(right);
    if(offset!=clock.offsets.back().seconds) {
      auto lo=left/kSecond, hi=right/kSecond;
      while(hi-lo>1) { const auto mid=lo+(hi-lo)/2;
        if(localOffset(mid*kSecond)==clock.offsets.back().seconds) lo=mid; else hi=mid;
      }
      clock.offsets.push_back({hi*kSecond,offset});
    }
    left=right;
  }
  return clock;
}

std::string formatTimestamp(TimestampNs ns,const DisplayClock& clock,bool full) {
  std::ostringstream out;
  if(!clock.wall_clock) {
    out<<std::showpos<<std::fixed<<std::setprecision(3)<<static_cast<double>((ns-clock.origin_ns)/1000000)/1000.0<<" s";
    return out.str();
  }
  int offset=clock.offsets.empty()?0:clock.offsets.front().seconds;
  for(const auto& change:clock.offsets) if(change.from_ns<=ns) offset=change.seconds;
  auto sec=ns/kSecond;
  auto remainder=ns%kSecond;
  if(remainder<0){--sec;remainder+=kSecond;}
  const std::time_t local=sec+offset;
  std::tm calendar{};
  if(!gmtime_r(&local,&calendar)) throw std::runtime_error("timestamp outside display range");
  out<<std::put_time(&calendar,full?"%Y-%m-%d %H:%M:%S":"%H:%M:%S")
     <<'.'<<std::setw(3)<<std::setfill('0')<<remainder/1000000;
  return out.str();
}

Metric summarize(const std::vector<WeightedSample>& input,const Interval& interval,
  const std::string& unit,std::optional<double> threshold) {
  Metric m;m.unit=unit;
  if(interval.end<=interval.start){m.reason="empty_candidate_interval";return m;}
  m.candidate_s=static_cast<double>(interval.end-interval.start)/kSecond;
  auto samples=input;
  std::stable_sort(samples.begin(),samples.end(),[](const auto& a,const auto& b){return a.start<b.start;});
  long double weight=0,sum=0,absolute=0,squares=0;
  double maximum=-1,exceed=0,longest=0,run=0;
  TimestampNs previous_end=interval.start;
  std::vector<std::pair<double,long double>> distribution;
  for(const auto& s:samples){
    const auto begin=std::max({s.start,interval.start,previous_end});
    const auto end=std::min(s.end,interval.end);
    if(end<=begin) continue;
    if(begin>previous_end) run=0;
    previous_end=end;
    if(!s.valid||!std::isfinite(s.value)) {++m.excluded[s.reason.empty()?"invalid_sample":s.reason];run=0;continue;}
    const long double dt=static_cast<long double>(end-begin)/kSecond;
    weight+=dt;sum+=s.value*dt;absolute+=std::abs(s.value)*dt;squares+=s.value*s.value*dt;
    distribution.emplace_back(std::abs(s.value),dt);++m.count;
    if(std::abs(s.value)>maximum){maximum=std::abs(s.value);m.maximum_ns=begin;}
    if(threshold&&std::abs(s.value)>*threshold){exceed+=dt;run+=dt;longest=std::max(longest,run);}else run=0;
  }
  m.valid_s=static_cast<double>(weight);m.coverage=m.valid_s/m.candidate_s;
  if(weight<=0){m.reason="no_valid_data";return m;}
  m.mean=static_cast<double>(sum/weight);m.mae=static_cast<double>(absolute/weight);
  m.rmse=std::sqrt(static_cast<double>(squares/weight));m.maximum=maximum;
  std::sort(distribution.begin(),distribution.end());long double cumulative=0;
  for(const auto& point:distribution){cumulative+=point.second;if(cumulative>=weight*0.95L){m.p95=point.first;break;}}
  if(!m.p95)m.p95=distribution.back().first;
  if(threshold){m.exceedance_s=exceed;m.longest_exceedance_s=longest;}
  m.quality=*m.coverage>=1-1e-9?"complete":"partial";
  return m;
}
}  // namespace track_insight
