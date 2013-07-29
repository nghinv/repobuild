// Copyright 2013
// Author: Christopher Van Arsdale

#include <string>

namespace repobuild {

class TargetInfo {
 public:
  TargetInfo() {}  // for stl, do not use.
  explicit TargetInfo(const std::string& full_path);
  TargetInfo(const std::string& relative_path, const std::string& build_file);
  /*
  TargetInfo(const TargetInfo& other)
      : full_path_(other.full_path_),
        build_file_(other.build_file_),
        dir_(other.dir_) {
  }
  */

  ~TargetInfo() {}

  const std::string& full_path() const { return full_path_; }
  const std::string& build_file() const { return build_file_; }
  const std::string& dir() const { return dir_; }
  const std::string& local_path() const { return local_path_; }

 private:
  std::string full_path_, build_file_, dir_, local_path_;
};

}  // namespace repobuild