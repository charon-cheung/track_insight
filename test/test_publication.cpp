#include "publication_transaction.hpp"
#include "track_insight/report.hpp"
#include <gtest/gtest.h>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

using namespace track_insight;
namespace fs=std::filesystem;
namespace {
struct PublicationTest:testing::Test {
  fs::path directory,root;
  void SetUp() override {
    auto pattern=(fs::temp_directory_path()/"track_publication_XXXXXX").string();
    std::vector<char> chars(pattern.begin(),pattern.end());chars.push_back(0);
    directory=mkdtemp(chars.data());root=directory/"report";
  }
  void TearDown() override {fs::remove_all(directory);}
  void killed(const std::function<void()>& work) {
    const auto pid=fork();ASSERT_GE(pid,0);
    if(pid==0) {try{work();raise(SIGKILL);}catch(...){_exit(91);}_exit(92);}
    int status=0;ASSERT_EQ(waitpid(pid,&status,0),pid);
    ASSERT_TRUE(WIFSIGNALED(status));EXPECT_EQ(WTERMSIG(status),SIGKILL);
  }
  fs::path transaction(bool archive)const {
    return root.string()+(archive?".track_insight.archive.transaction":".track_insight.transaction");
  }
};
TEST_F(PublicationTest, DeadOwnerReleasesLockAndUnpublishedGenerationIsDiscarded) {
  writeText(root,"old");
  killed([&]{PublicationTransaction t(root,false);writeText(t.stage()/"new","partial");});
  {PublicationTransaction recovered(root,false);EXPECT_EQ(readText(root),"old");
    EXPECT_FALSE(fs::exists(transaction(false)));
    EXPECT_THROW(PublicationTransaction competing(root,false),std::runtime_error);
    const auto s=recovered.stage();writeText(s/"new","complete");recovered.publish({{s/"new",root}});}
  EXPECT_EQ(readText(root),"complete");
  {PublicationTransaction again(root,false);EXPECT_EQ(readText(root),"complete");}
}
TEST_F(PublicationTest, LiveOwnerCannotBeTakenOverEvenWithWrongPidMetadata) {
  PublicationTransaction owner(root,false);
  writeText(root.string()+".track_insight.lock","pid=99999999\n");
  const auto pid=fork();ASSERT_GE(pid,0);
  if(pid==0){try{PublicationTransaction contender(root,false);_exit(1);}catch(...){_exit(0);}}
  int status=0;ASSERT_EQ(waitpid(pid,&status,0),pid);ASSERT_TRUE(WIFEXITED(status));EXPECT_EQ(WEXITSTATUS(status),0);
}
TEST_F(PublicationTest, EveryPublicationAndRollbackBoundaryRecoversAllOldTargets) {
  // Persist the filesystem states at each rename boundary, then kill the owner.
  // Covers a replacement and an added round; recovery must be idempotent too.
  for(bool added:{false,true})for(int cut=0;cut<=6;++cut)for(int rollback_steps:{0,1,2}) {
    SCOPED_TRACE(std::to_string(added)+"/"+std::to_string(cut)+"/"+std::to_string(rollback_steps));
    fs::remove_all(root);fs::create_directories(root/"runs");
    const std::vector<std::string> names{"runs/run_1_aaaaaaaaaaaa","runs.json","index.html"};
    for(std::size_t i=0;i<names.size();++i)if(i||!added)writeText(root/names[i],"old"+std::to_string(i));
    killed([&]{
      PublicationTransaction t(root,true);const auto s=t.stage();Json entries=Json::array();
      for(std::size_t i=0;i<names.size();++i) {
        writeText(s/("new"+std::to_string(i)),"new"+std::to_string(i));
        entries.push_back({{"source","new"+std::to_string(i)},{"target",names[i]},{"existed",bool(i||!added)}});
      }
      auto state=Json::parse(readText(s/"state.json"));state["phase"]="publishing";state["entries"]=entries;writeText(s/"state.json",state.dump());
      int step=0;
      for(std::size_t i=0;i<names.size();++i) {
        if(i||!added) {if(step++==cut)break;fs::rename(root/names[i],s/("previous_"+std::to_string(i)));}
        if(step++==cut)break;
        fs::rename(s/("new"+std::to_string(i)),root/names[i]);
      }
      if(rollback_steps)for(int i=2;i>=0;--i) {
        const auto backup=s/("previous_"+std::to_string(i)),source=s/("new"+std::to_string(i));
        if(fs::exists(backup)&&fs::exists(root/names[i])) {
          fs::rename(root/names[i],source);
          if(rollback_steps==2)fs::rename(backup,root/names[i]);
          break;
        }
      }
    });
    for(int repeat=0;repeat<2;++repeat) {
      PublicationTransaction recovered(root,true);
      for(std::size_t i=0;i<names.size();++i) {
        if(i==0&&added)EXPECT_FALSE(fs::exists(root/names[i]));
        else EXPECT_EQ(readText(root/names[i]),"old"+std::to_string(i));
      }
      EXPECT_FALSE(fs::exists(transaction(true)));
    }
  }
}
TEST_F(PublicationTest, CommitKeepsNewVersionAndCleanupCanResume) {
  for(bool cleanup_started:{false,true}) {
    writeText(root,"old");
    killed([&]{
      PublicationTransaction t(root,false);const auto s=t.stage();writeText(s/"new","new");
      auto state=Json::parse(readText(s/"state.json"));
      state["phase"]="committed";state["entries"]={{{"source","new"},{"target","."},{"existed",true}}};
      fs::rename(root,s/"previous_0");fs::rename(s/"new",root);writeText(s/"state.json",state.dump());
      if(cleanup_started)fs::remove(s/"previous_0");
    });
    PublicationTransaction recovered(root,false);EXPECT_EQ(readText(root),"new");EXPECT_FALSE(fs::exists(transaction(false)));
  }
}
TEST_F(PublicationTest, CallbackFailureRollsBackAndFutureGenerationSucceeds) {
  writeText(root,"old");
  {PublicationTransaction t(root,false);const auto s=t.stage();writeText(s/"new","new");
    EXPECT_THROW(t.publish({{s/"new",root}},[]{throw std::runtime_error("interrupted");}),std::runtime_error);}
  EXPECT_EQ(readText(root),"old");
  {PublicationTransaction t(root,false);const auto s=t.stage();writeText(s/"new","new");t.publish({{s/"new",root}});}
  EXPECT_EQ(readText(root),"new");
}
TEST_F(PublicationTest, UnsafeJournalAndSymlinksArePreservedWithoutFollowingThem) {
  const auto other=directory/"personal";writeText(other,"keep");writeText(root,"old");
  killed([&]{PublicationTransaction t(root,false);const auto s=t.stage();
    auto state=Json::parse(readText(s/"state.json"));state["phase"]="publishing";
    state["entries"]={{{"source","new"},{"target","../personal"},{"existed",true}}};writeText(s/"state.json",state.dump());});
  EXPECT_THROW(PublicationTransaction t(root,false),std::runtime_error);
  EXPECT_EQ(readText(other),"keep");EXPECT_EQ(readText(root),"old");EXPECT_TRUE(fs::exists(transaction(false)));
  fs::remove_all(transaction(false));fs::create_directory_symlink(directory,transaction(false));
  EXPECT_THROW(PublicationTransaction t(root,false),std::runtime_error);EXPECT_EQ(readText(other),"keep");
  fs::remove(transaction(false));fs::remove(root.string()+".track_insight.lock");
  fs::create_symlink(other,root.string()+".track_insight.lock");
  EXPECT_THROW(PublicationTransaction t(root,false),std::runtime_error);EXPECT_EQ(readText(other),"keep");
}
TEST_F(PublicationTest, InterruptedJournalCreationAndRollbackCleanupCanResume) {
  writeText(root,"old");
  for(bool temporary:{false,true}) {
    fs::create_directory(transaction(false));
    if(temporary)writeText(transaction(false)/"state.tmp","partial json");
    PublicationTransaction recovered(root,false);
    EXPECT_EQ(readText(root),"old");EXPECT_FALSE(fs::exists(transaction(false)));
  }
  killed([&]{PublicationTransaction t(root,false);const auto s=t.stage();
    auto state=Json::parse(readText(s/"state.json"));state["phase"]="rolled_back";
    state["entries"]={{{"source","new"},{"target","."},{"existed",true}}};
    writeText(s/"state.json",state.dump()); // New artifacts already removed; journal remains last.
  });
  PublicationTransaction recovered(root,false);EXPECT_EQ(readText(root),"old");EXPECT_FALSE(fs::exists(transaction(false)));
}
TEST_F(PublicationTest, InvalidJournalPreservesRecoveryMaterial) {
  writeText(root,"old");fs::create_directory(transaction(false));
  writeText(transaction(false)/"new","new");
  EXPECT_THROW(PublicationTransaction t(root,false),std::runtime_error);
  writeText(transaction(false)/"state.json","invalid json");
  EXPECT_THROW(PublicationTransaction t(root,false),std::exception);
  EXPECT_EQ(readText(root),"old");EXPECT_EQ(readText(transaction(false)/"new"),"new");
}
TEST_F(PublicationTest, ExistingLockContentsAndHardLinkedFilesAreNeverModified) {
  const fs::path lock=root.string()+".track_insight.lock",other=directory/"personal";
  writeText(lock,"user data");
  {PublicationTransaction t(root,false);EXPECT_EQ(readText(lock),"user data");}
  fs::remove(lock);writeText(other,"keep");fs::create_hard_link(other,lock);
  EXPECT_THROW(PublicationTransaction t(root,false),std::runtime_error);
  EXPECT_EQ(readText(lock),"keep");EXPECT_EQ(readText(other),"keep");
}
TEST_F(PublicationTest, NewlyCreatedParentDirectoriesSupportRecoveryAndPublication) {
  root=directory/"new"/"deep"/"report";
  killed([&]{PublicationTransaction t(root,false);const auto s=t.stage();writeText(s/"new","new");
    t.publish({{s/"new",root}},[]{raise(SIGKILL);});});
  {PublicationTransaction t(root,false);EXPECT_FALSE(fs::exists(root));
    const auto s=t.stage();writeText(s/"new","complete");t.publish({{s/"new",root}});}
  EXPECT_EQ(readText(root),"complete");
}
TEST_F(PublicationTest, LegacyLockIsNeverBlindlyRemoved) {
  const auto lock=root.string()+".track_insight.lock";fs::create_directory(lock);
  EXPECT_THROW(PublicationTransaction t(root,false),std::runtime_error);EXPECT_TRUE(fs::is_directory(lock));
}
}
