#include "track_insight/input.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace track_insight {
namespace fs=std::filesystem;
namespace {
using Digest=std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)>;
std::string finish(EVP_MD_CTX* context) {
  unsigned char digest[EVP_MAX_MD_SIZE];unsigned int size=0;
  if(EVP_DigestFinal_ex(context,digest,&size)!=1)throw std::runtime_error("hash finalization failed");
  std::ostringstream text;for(unsigned i=0;i<size;++i)text<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(digest[i]);
  return text.str();
}
}
std::string hashText(const std::string& text) {
  Digest context(EVP_MD_CTX_new(),EVP_MD_CTX_free);
  if(!context||EVP_DigestInit_ex(context.get(),EVP_sha256(),nullptr)!=1)throw std::runtime_error("hash initialization failed");
  EVP_DigestUpdate(context.get(),text.data(),text.size());return finish(context.get());
}
std::string hashInput(const fs::path& path) {
  std::vector<fs::path> files;
  if(fs::is_directory(path)) {
    for(const auto& entry:fs::directory_iterator(path))
      if(entry.is_regular_file()&&(entry.path().extension()==".mcap"||entry.path().filename()=="metadata.yaml"))files.push_back(entry.path());
  } else files.push_back(path);
  std::sort(files.begin(),files.end());
  if(files.empty())throw std::runtime_error("no readable bag files");
  Digest context(EVP_MD_CTX_new(),EVP_MD_CTX_free);
  if(!context||EVP_DigestInit_ex(context.get(),EVP_sha256(),nullptr)!=1)throw std::runtime_error("hash initialization failed");
  std::array<char,1024*1024> buffer{};
  for(const auto& file:files) {
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("cannot hash input "+file.string());
    const auto name=file.filename().string();
    if(files.size()>1)EVP_DigestUpdate(context.get(),name.data(),name.size());
    while(input){input.read(buffer.data(),buffer.size());EVP_DigestUpdate(context.get(),buffer.data(),input.gcount());}
    if(!input.eof())throw std::runtime_error("input read error while hashing");
  }
  return finish(context.get());
}
InputPackage::InputPackage(const fs::path& input,const Config& config) {
  if(!fs::exists(input))throw std::runtime_error("input does not exist: "+input.string());
  if(input.extension()!=".zip") {bag_=fs::absolute(input);return;}
  auto pattern=(fs::temp_directory_path()/"track_insight_zip_XXXXXX").string();
  std::vector<char> name(pattern.begin(),pattern.end());name.push_back('\0');
  char* created=mkdtemp(name.data());if(!created)throw std::runtime_error("cannot create ZIP extraction directory");
  temporary_=created;
  try {
    std::unique_ptr<archive,decltype(&archive_read_free)> reader(archive_read_new(),archive_read_free);
    archive_read_support_filter_all(reader.get());archive_read_support_format_zip(reader.get());
    if(archive_read_open_filename(reader.get(),input.c_str(),10240)!=ARCHIVE_OK)throw std::runtime_error(archive_error_string(reader.get()));
    std::uint64_t total=0;std::size_t count=0;archive_entry* entry=nullptr;int status;
    std::array<char,65536> buffer{};
    while((status=archive_read_next_header(reader.get(),&entry))==ARCHIVE_OK) {
      if(++count>config.max_archive_files)throw std::runtime_error("ZIP file-count limit exceeded");
      const char* raw=archive_entry_pathname(entry);if(!raw)throw std::runtime_error("ZIP entry without path");
      fs::path relative(raw);
      if(relative.is_absolute()||archive_entry_symlink(entry)||archive_entry_hardlink(entry))throw std::runtime_error("unsafe ZIP entry");
      for(const auto& part:relative)if(part=="..")throw std::runtime_error("ZIP path traversal rejected");
      const auto target=temporary_/relative;
      if(archive_entry_filetype(entry)==AE_IFDIR){fs::create_directories(target);continue;}
      if(archive_entry_filetype(entry)!=AE_IFREG)throw std::runtime_error("non-regular ZIP entry rejected");
      if(fs::exists(target))throw std::runtime_error("duplicate ZIP file rejected");
      fs::create_directories(target.parent_path());std::ofstream output(target,std::ios::binary);
      if(!output)throw std::runtime_error("cannot extract ZIP entry");
      la_ssize_t size;
      while((size=archive_read_data(reader.get(),buffer.data(),buffer.size()))>0) {
        total+=static_cast<std::uint64_t>(size);
        if(total>config.max_archive_bytes || static_cast<long double>(total)>fs::file_size(input)*config.max_expansion_ratio)
          throw std::runtime_error("ZIP expanded-size limit exceeded");
        output.write(buffer.data(),size);if(!output)throw std::runtime_error("ZIP extraction write failed");
      }
      if(size<0)throw std::runtime_error(archive_error_string(reader.get()));
    }
    if(status!=ARCHIVE_EOF)throw std::runtime_error("invalid or truncated ZIP");
    std::vector<fs::path> roots,mcaps;
    for(const auto& entry:fs::recursive_directory_iterator(temporary_)) {
      if(entry.path().filename()=="metadata.yaml")roots.push_back(entry.path().parent_path());
      if(entry.path().extension()==".mcap")mcaps.push_back(entry.path());
    }
    if(roots.size()==1)bag_=roots.front();
    else if(roots.empty()&&mcaps.size()==1)bag_=mcaps.front();
    else throw std::runtime_error("ZIP must identify exactly one bag");
  }catch(...){fs::remove_all(temporary_);temporary_.clear();throw;}
}
InputPackage::~InputPackage(){if(!temporary_.empty()){std::error_code error;fs::remove_all(temporary_,error);}}
}  // namespace track_insight
