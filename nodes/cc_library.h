// Copyright 2013
// Author: Christopher Van Arsdale

#ifndef _REPOBUILD_NODES_CC_LIBRARY_H__
#define _REPOBUILD_NODES_CC_LIBRARY_H__

#include <string>
#include "nodes/node.h"

namespace repobuild {

class CCLibraryNode : public Node {
 public:
  CCLibraryNode(const TargetInfo& t) : Node(t) {}
  virtual ~CCLibraryNode() {}
  virtual std::string Name() const { return "cc_library"; }
  virtual void Parse(const BuildFile& file, const BuildFileNode& input);

 protected:
  std::vector<std::string> sources_;
  std::vector<std::string> headers_;
  std::vector<std::string> cc_compile_args_;
};

}  // namespace repobuild

# endif  // _REPOBUILD_NODES_CC_LIBRARY_H__
