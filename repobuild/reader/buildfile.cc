// Copyright 2013
// Author: Christopher Van Arsdale

#include <map>
#include <set>
#include <string>
#include <vector>
#include "common/log/log.h"
#include "common/file/fileutil.h"
#include "common/util/stl.h"
#include "common/strings/path.h"
#include "common/strings/strutil.h"
#include "common/strings/varmap.h"
#include "repobuild/distsource/dist_source.h"
#include "repobuild/env/resource.h"
#include "repobuild/reader/buildfile.h"
#include "repobuild/third_party/json/json.h"

using std::set;
using std::string;
using std::vector;
using std::map;

namespace repobuild {
namespace {
const Json::Value& GetValue(const BuildFileNode& input, const string& key) {
  const Json::Value* current = &input.object();
  for (const string& subkey : strings::SplitString(key, ".")) {
    if (current->isNull()) {
      break;
    }
    current = &(*current)[subkey];
  }
  return *current;
}
}  // anonymous namespace

BuildFileNode::BuildFileNode(const Json::Value& object) {
  Reset(object);
}

BuildFileNode::~BuildFileNode() {
}

void BuildFileNode::Reset(const Json::Value& object) {
  object_.reset(new Json::Value(object));
}

BuildFile::~BuildFile() {
  DeleteElements(&nodes_);
  DeleteElements(&owned_rewriters_);
}

void BuildFile::Parse(const string& input) {
  Json::Value root;
  Json::Reader reader;
  bool ok = reader.parse(input, root);
  if (!ok) {
    LOG(FATAL) << "BUILD file reader error\n\nIn "
               << filename()
               << ":\n "
               << reader.getFormattedErrorMessages()
               << "\n\n(check for missing/spurious commas).\n\n";
  }
  CHECK(root.isArray()) << root;

  for (int i = 0; i < root.size(); ++i) {
    const Json::Value& value = root[i];
    CHECK(value.isObject()) << "Unexpected: " << value;
    nodes_.push_back(new BuildFileNode(value));
  }
}

string BuildFile::NextName(const string& name_base) {
  int* counter = &name_counter_[name_base];
  return strings::Join(name_base, ".", (*counter)++);
}

TargetInfo BuildFile::ComputeTargetInfo(const std::string& dependency) const {
  VLOG(1) << "ComputeTargetInfo: " << dependency;
  TargetInfo base(dependency, filename());
  VLOG(1) << filename() << ": " << rewriters_.size();
  for (int i = rewriters_.size() - 1; i >= 0; --i) {
    if (rewriters_[i]->RewriteDependency(&base)) {
      break;
    }
  }
  return base;
}

void BuildFile::MergeParent(BuildFile* parent) {
  for (const string& dep : parent->base_dependencies()) {
    base_deps_.insert(dep);
  }
  for (BuildDependencyRewriter* rewriter : parent->rewriters_) {
    rewriters_.push_back(rewriter);
  }
}

void BuildFile::MergeDependency(BuildFile* dependency) {
  for (const auto& it : dependency->registered_keys_) {
    registered_keys_.insert(it);  // doesn't insert if already in map.
  }
}

const std::string BuildFile::GetKey(const std::string& key) const {
  const string kEmpty;
  return FindWithDefault(registered_keys_, key, kEmpty);
}

BuildFileNodeReader::BuildFileNodeReader(const BuildFileNode& node,
                                         DistSource* source)
    : input_(node),
      dist_source_(source),
      var_map_true_(new strings::VarMap),
      var_map_false_(new strings::VarMap),
      strict_file_mode_(true) {
}

BuildFileNodeReader::~BuildFileNodeReader() {
}

void BuildFileNodeReader::SetReplaceVariable(bool mode,
                                             const string& original,
                                             const string& replace) {
  strings::VarMap* var = (mode ? var_map_true_.get() : var_map_false_.get());
  var->Set("$" + original, replace);
  var->Set("$(" + original + ")", replace);
  var->Set("${" + original + "}", replace);
}

void BuildFileNodeReader::ParseRepeatedString(const string& key,
                                              bool mode,
                                              vector<string>* output) const {
  const Json::Value& array = GetValue(input_, key);
  if (!array.isNull()) {
    CHECK(array.isArray()) << "Expecting array for key " << key << ": "
                           << input_.object();
    for (int i = 0; i < array.size(); ++i) {
      const Json::Value& single = array[i];
      CHECK(single.isString()) << "Expecting string for item of " << key << ": "
                               << input_.object()
                               << ". Target: " << error_path_;
      output->push_back(RewriteSingleString(mode, single.asString()));
      VLOG(1) << "Parsing string: "
              << single.asString()
              << " (" << key << ", " << mode << ") => "
              << output->back();
    }
  }
}

void BuildFileNodeReader::ParseKeyValueStrings(
    const string& key,
    map<string, string>* output) const {
  const Json::Value& list = input_.object()[key];
  if (list.isNull()) {
    return;
  }
  CHECK(list.isObject())
      << "KeyValue list (\"" << key
      << "\") must be object in " << error_path_;
  for (const string& name : list.getMemberNames()) {
    const Json::Value& val = list[name];
    CHECK(val.isString()) << "Value var (\"" << name
                          << "\") must be string in " << error_path_;
    (*output)[name] = RewriteSingleString(false, val.asString());
  }
}

bool BuildFileNodeReader::ParseStringField(const string& key,
                                           string* field) const {
  return ParseStringField(key, false, field);
}

bool BuildFileNodeReader::ParseStringField(const string& key,
                                           bool mode,
                                           string* field) const {
  const Json::Value& json_field = GetValue(input_, key);
  if (!json_field.isString()) {
    return false;
  }
  *field = RewriteSingleString(mode, json_field.asString());
  return true;
}

void BuildFileNodeReader::ParseRepeatedFiles(const string& key,
                                             bool strict_file_mode,
                                             vector<Resource>* output) const {
  vector<string> temp;
  ParseRepeatedString(key, &temp);
  ParseFilesFromString(temp, strict_file_mode, output);
}

void BuildFileNodeReader::ParseFilesFromString(const vector<string>& input,
                                               bool strict_file_mode,
                                               vector<Resource>* output) const {
  for (const string& file : input) {
    // TODO(cvanarsdale): hacky. Probably better to make build file have more
    // complex syntax. E.g.:
    // build_file_list = [ "local.cc", { "gen": "generated.cc" }, ... ]
    string glob = strings::JoinPath(file_path_, file);
    for (const string& prefix : abs_prefix_) {
      if (strings::HasPrefix(file, prefix)) {
        glob = file;
        break;
      }
    }

    // Make sure we actually have this directory loaded in our system.
    vector<string> tmp;
    CHECK(dist_source_);
    dist_source_->InitializeForFile(glob, &tmp);
    if (tmp.empty()) {
      if (strict_file_mode) {
        LOG(FATAL) << "No matched files: " << file
                   << " for target " << error_path_
                   << "\n\nIf this file is generated during compilation, "
                   << "add to your BUILD rule:\n\"strict_file_mode\": false\n\n"
                   << "(there is a TODO to handle this more gracefully)";
      } else {
        output->push_back(Resource::FromRootPath(glob));
      }
    } else {
      for (const string& it : tmp) {
        output->push_back(Resource::FromRootPath(it));
      }
    }
  }
}

void BuildFileNodeReader::ParseSingleFile(const string& key,
                                          bool strict_file_mode,
                                          vector<Resource>* output) const {
  string tmp;
  if (ParseStringField(key, &tmp)) {
    vector<string> fake;
    fake.push_back(tmp);
    ParseFilesFromString(fake, strict_file_mode, output);
  }
}

string BuildFileNodeReader::ParseSingleDirectory(const string& key) const {
  return ParseSingleDirectory(strict_file_mode_, key);
}

string BuildFileNodeReader::ParseSingleDirectory(bool strict_file_mode,
                                                 const string& key) const {
  vector<Resource> dirs;
  ParseSingleFile(key, strict_file_mode, &dirs);
  if (!dirs.empty()) {
    if (dirs.size() > 1) {
      LOG(FATAL) << "Too many results for " << key << ", need 1: "
                 << error_path_;
    }
    return dirs[0].path();
  }
  return "";
}

bool BuildFileNodeReader::ParseBoolField(const string& key,
                                         bool* field) const {
  const Json::Value& json_field = GetValue(input_, key);
  if (!json_field.isBool()) {
    return false;
  }
  *field = json_field.asBool();
  return true;
}

string BuildFileNodeReader::RewriteSingleString(bool mode,
                                                const string& str) const {
  return (mode ? var_map_true_.get() : var_map_false_.get())->Replace(str);
}

}  // namespace repobuild
