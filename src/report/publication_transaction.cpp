#include "publication_transaction.hpp"
#include "track_insight/report.hpp"
#include <cerrno>
#include <fcntl.h>
#include <regex>
#include <set>
#include <sys/file.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace track_insight {
namespace fs=std::filesystem;
namespace {
struct Descriptor {
  int fd;
  ~Descriptor(){if(fd>=0)::close(fd);}
};
[[noreturn]] void failed(const std::string& action,const fs::path& path) {
  throw std::system_error(errno,std::generic_category(),action+": "+path.string());
}
bool present(const fs::path& path) {return fs::exists(fs::symlink_status(path));}
void plain(const fs::path& path) {
  const auto status=fs::symlink_status(path);
  if(!fs::is_regular_file(status)&&!fs::is_directory(status))
    throw std::runtime_error("transaction contains a non-regular path: "+path.string());
}
void syncPath(const fs::path& path) {
  Descriptor fd{::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW)};
  if(fd.fd<0)failed("open for sync",path);
  if(::fsync(fd.fd)!=0)failed("sync",path);
}
void syncTree(const fs::path& path) {
  plain(path);
  if(fs::is_directory(path))for(const auto& entry:fs::directory_iterator(path))syncTree(entry.path());
  syncPath(path);
}
void checkTree(const fs::path& path) {
  plain(path);
  if(fs::is_directory(path))for(const auto& entry:fs::recursive_directory_iterator(path))plain(entry.path());
}
void move(const fs::path& from,const fs::path& to) {
  fs::rename(from,to);syncPath(from.parent_path());
  if(from.parent_path()!=to.parent_path())syncPath(to.parent_path());
}
void save(const fs::path& directory,const Json& state) {
  const auto temporary=directory/"state.tmp",target=directory/"state.json";
  if(present(temporary)&&!fs::is_regular_file(fs::symlink_status(temporary)))
    throw std::runtime_error("invalid transaction journal temporary file");
  Descriptor fd{::open(temporary.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600)};
  if(fd.fd<0)failed("write journal",temporary);
  const auto bytes=state.dump();std::size_t offset=0;
  while(offset<bytes.size()) {
    const auto written=::write(fd.fd,bytes.data()+offset,bytes.size()-offset);
    if(written<0&&errno==EINTR)continue;
    if(written<=0)failed("write journal",temporary);
    offset+=written;
  }
  if(::fsync(fd.fd)!=0)failed("sync journal",temporary);
  move(temporary,target);
}
void clean(const fs::path& directory) {
  // Retain the journal until every other artifact has been durably removed.
  checkTree(directory);
  for(const auto& entry:fs::directory_iterator(directory))
    if(entry.path().filename()!="state.json")fs::remove_all(entry.path());
  syncPath(directory);fs::remove(directory/"state.json");syncPath(directory);
  fs::remove(directory);syncPath(directory.parent_path());
}
Json initial(const fs::path& root,bool archive) {
  return {{"version",1},{"root",root.string()},{"archive",archive},{"phase","generating"},{"entries",Json::array()}};
}
void validate(const Json& state,const fs::path& root,bool archive) {
  if(state.at("version")!=1||state.at("root")!=root.string()||state.at("archive")!=archive||!state.at("entries").is_array())
    throw std::runtime_error("transaction journal identity mismatch");
  const auto phase=state.at("phase").get<std::string>();
  if(phase!="generating"&&phase!="publishing"&&phase!="committed"&&phase!="rolled_back")
    throw std::runtime_error("invalid transaction phase");
  const auto& entries=state.at("entries");
  if(phase=="generating") {if(!entries.empty())throw std::runtime_error("invalid generation journal");return;}
  if(entries.size()!=1&&!(archive&&entries.size()==3))throw std::runtime_error("invalid transaction targets");
  const std::regex run("runs/run_-?[0-9]+_[a-f0-9]{12}");
  for(std::size_t i=0;i<entries.size();++i) {
    const auto& e=entries[i];const auto target=e.at("target").get<std::string>();
    const auto source=e.at("source").get<std::string>();
    if(source.empty()||fs::path(source).filename()!=source||source=="."||source==".."||
       source=="state.json"||source=="state.tmp"||source.rfind("previous_",0)==0||!e.at("existed").is_boolean())
      throw std::runtime_error("invalid transaction source");
    for(std::size_t j=0;j<i;++j)if(entries[j].at("source")==source)throw std::runtime_error("duplicate transaction source");
    if(entries.size()==1?target!=".":i==0?!std::regex_match(target,run):target!=(i==1?"runs.json":"index.html"))
      throw std::runtime_error("transaction target outside owned layout");
  }
}
}
PublicationTransaction::PublicationTransaction(const fs::path& root,bool archive):
  root_(fs::absolute(root).lexically_normal()),archive_(archive) {
  if(root_!=root_.root_path()&&root_.filename().empty())root_=root_.parent_path();
  if(root_==root_.root_path())throw std::runtime_error("invalid transaction root");
  const auto prefix=root_.string()+(archive?".track_insight.archive":".track_insight");
  const fs::path lock=prefix+".lock";transaction_=prefix+".transaction";
  auto existing_parent=root_.parent_path();
  while(!present(existing_parent))existing_parent=existing_parent.parent_path();
  fs::create_directories(root_.parent_path());
  // Persist new ancestors too, so a committed transaction cannot lose its whole parent tree.
  for(auto parent=root_.parent_path();parent!=existing_parent;parent=parent.parent_path()) {
    syncPath(parent);syncPath(parent.parent_path());
  }
  if(fs::is_directory(fs::symlink_status(lock)))
    throw std::runtime_error("legacy directory lock has no owner information; verify no old generator is running before removing: "+lock.string());
  if(present(lock)&&!fs::is_regular_file(fs::symlink_status(lock)))throw std::runtime_error("invalid lock file: "+lock.string());
  lock_fd_=::open(lock.c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
  if(lock_fd_<0)failed("open lock",lock);
  try {
    struct stat status{};
    if(::fstat(lock_fd_,&status)!=0)failed("inspect lock",lock);
    if(!S_ISREG(status.st_mode)||status.st_nlink!=1)throw std::runtime_error("invalid lock file: "+lock.string());
    if(::flock(lock_fd_,LOCK_EX|LOCK_NB)!=0) {
      if(errno==EWOULDBLOCK)throw std::runtime_error("another generation holds the output/archive lock: "+lock.string());
      failed("acquire lock",lock);
    }
    // The kernel decides liveness. Never truncate an existing file or unlink its inode.
    recover();
  }catch(...) {::close(lock_fd_);lock_fd_=-1;throw;}
}
PublicationTransaction::~PublicationTransaction() {
  try{recover();}catch(...){} // Preserve recovery material if cleanup cannot be proven safe.
  if(lock_fd_>=0)::close(lock_fd_);
}
fs::path PublicationTransaction::stage() {
  if(present(transaction_))throw std::runtime_error("transaction is already active");
  fs::create_directory(transaction_);syncPath(transaction_.parent_path());
  save(transaction_,initial(root_,archive_));return transaction_;
}
void PublicationTransaction::recover() {
  if(!present(transaction_))return;
  checkTree(transaction_);
  if(!present(transaction_/"state.json")) {
    // Interrupted creation of the initial journal cannot have published any target.
    for(const auto& entry:fs::directory_iterator(transaction_))
      if(entry.path().filename()!="state.tmp")throw std::runtime_error("transaction journal missing; recovery files preserved: "+transaction_.string());
    clean(transaction_);return;
  }
  auto state=Json::parse(readText(transaction_/"state.json"));validate(state,root_,archive_);
  if(state["phase"]=="publishing") {
    // Validate every target before mutating any of them. Do not follow replaced parents.
    for(const auto& e:state["entries"]) {
      const auto target=e["target"]=="."?root_:root_/e["target"].get<std::string>();
      for(auto p=target;p!=root_.parent_path();p=p.parent_path())if(present(p))plain(p);
    }
    for(std::size_t n=state["entries"].size();n>0;--n) {
      const auto i=n-1;const auto& e=state["entries"][i];
      const auto target=e["target"]=="."?root_:root_/e["target"].get<std::string>();
      const auto source=transaction_/e["source"].get<std::string>(),backup=transaction_/("previous_"+std::to_string(i));
      if(present(backup)) {
        if(!e["existed"].get<bool>())throw std::runtime_error("unexpected transaction backup");
        if(present(target)) {
          if(present(source))throw std::runtime_error("ambiguous transaction target; backups preserved");
          move(target,source);
        }
        if(!present(source))throw std::runtime_error("missing new transaction artifact");
        move(backup,target);
      }else if(!present(source)) {
        if(e["existed"].get<bool>()||!present(target))throw std::runtime_error("missing transaction backup; files preserved");
        move(target,source);
      }else if(present(target)!=e["existed"].get<bool>())throw std::runtime_error("transaction target changed externally");
    }
    state["phase"]="rolled_back";save(transaction_,state);
  }
  clean(transaction_);
}
void PublicationTransaction::publish(const std::vector<Entry>& entries,const std::function<void()>& before_commit) {
  auto state=initial(root_,archive_);state["phase"]="publishing";
  for(const auto& [source,target]:entries) {
    if(source.parent_path()!=transaction_)throw std::runtime_error("source outside transaction");
    const auto relative=target==root_?fs::path("."):target.lexically_relative(root_);
    state["entries"].push_back({{"source",source.filename().string()},{"target",relative.generic_string()},{"existed",present(target)}});
  }
  validate(state,root_,archive_);
  for(const auto& [source,target]:entries) {syncTree(source);if(present(target))checkTree(target);}
  save(transaction_,state);
  try {
    for(std::size_t i=0;i<entries.size();++i) {
      const auto& [source,target]=entries[i];
      if(state["entries"][i]["existed"])move(target,transaction_/("previous_"+std::to_string(i)));
      move(source,target);
    }
    if(before_commit)before_commit();
    state["phase"]="committed";save(transaction_,state);
  }catch(...) {recover();throw;}
  recover();
}
}
