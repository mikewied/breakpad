// Copyright (c) 2006, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdio.h>
#include <map>
#include <string.h>
#include <vector>
#include <utility>
#include "processor/source_line_resolver.h"
#include "google/stack_frame.h"
#include "processor/contained_range_map-inl.h"
#include "processor/linked_ptr.h"
#include "processor/range_map-inl.h"
#include "processor/stack_frame_info.h"

using std::map;
using std::vector;
using std::make_pair;
using __gnu_cxx::hash;

namespace google_airbag {

struct SourceLineResolver::Line {
  Line(MemAddr addr, MemAddr code_size, int file_id, int source_line)
      : address(addr)
      , size(code_size)
      , source_file_id(file_id)
      , line(source_line) { }

  MemAddr address;
  MemAddr size;
  int source_file_id;
  int line;
};

struct SourceLineResolver::Function {
  Function(const string &function_name,
           MemAddr function_address,
           MemAddr code_size)
      : name(function_name), address(function_address), size(code_size) { }

  string name;
  MemAddr address;
  MemAddr size;
  RangeMap<MemAddr, linked_ptr<Line> > lines;
};

class SourceLineResolver::Module {
 public:
  Module(const string &name) : name_(name) { }

  // Loads the given map file, returning true on success.
  bool LoadMap(const string &map_file);

  // Looks up the given relative address, and fills the StackFrame struct
  // with the result.  Additional debugging information, if available, is
  // placed in frame_info.
  void LookupAddress(MemAddr address,
                     StackFrame *frame, StackFrameInfo *frame_info) const;

 private:
  friend class SourceLineResolver;
  typedef hash_map<int, string> FileMap;

  // The types for stack_info_.  This is equivalent to MS DIA's
  // StackFrameTypeEnum.  Each identifies a different type of frame
  // information, although all are represented in the symbol file in the
  // same format.  These are used as indices to the stack_info_ array.
  enum StackInfoTypes {
    STACK_INFO_FPO = 0,
    STACK_INFO_TRAP,  // not used here
    STACK_INFO_TSS,   // not used here
    STACK_INFO_STANDARD,
    STACK_INFO_FRAME_DATA,
    STACK_INFO_LAST,  // must be the last sequentially-numbered item
    STACK_INFO_UNKNOWN = -1
  };

  // Splits line into at most max_tokens space-separated tokens, placing
  // them in the tokens vector.  line is a 0-terminated string that
  // optionally ends with a newline character or combination, which will
  // be removed.  line must not contain any embedded '\n' or '\r' characters.
  // If more tokens than max_tokens are present, the final token is placed
  // into the vector without splitting it up at all.  This modifies line as
  // a side effect.  Returns true if exactly max_tokens tokens are returned,
  // and false if fewer are returned.  This is not considered a failure of
  // Tokenize, but may be treated as a failure if the caller expects an
  // exact, as opposed to maximum, number of tokens.
  static bool Tokenize(char *line, int max_tokens, vector<char*> *tokens);

  // Parses a file declaration
  void ParseFile(char *file_line);

  // Parses a function declaration, returning a new Function object.
  Function* ParseFunction(char *function_line);

  // Parses a line declaration, returning a new Line object.
  Line* ParseLine(char *line_line);

  // Parses a stack frame info declaration, storing it in stack_info_.
  bool ParseStackInfo(char *stack_info_line);

  string name_;
  FileMap files_;
  RangeMap<MemAddr, linked_ptr<Function> > functions_;

  // Each element in the array is a ContainedRangeMap for a type listed in
  // StackInfoTypes.  These are split by type because there may be overlaps
  // between maps of different types, but some information is only available
  // as certain types.
  ContainedRangeMap<MemAddr, StackFrameInfo> stack_info_[STACK_INFO_LAST];
};

SourceLineResolver::SourceLineResolver() : modules_(new ModuleMap) {
}

SourceLineResolver::~SourceLineResolver() {
  ModuleMap::iterator it;
  for (it = modules_->begin(); it != modules_->end(); ++it) {
    delete it->second;
  }
  delete modules_;
}

bool SourceLineResolver::LoadModule(const string &module_name,
                                    const string &map_file) {
  // Make sure we don't already have a module with the given name.
  if (modules_->find(module_name) != modules_->end()) {
    return false;
  }

  Module *module = new Module(module_name);
  if (!module->LoadMap(map_file)) {
    delete module;
    return false;
  }

  modules_->insert(make_pair(module_name, module));
  return true;
}

bool SourceLineResolver::HasModule(const string &module_name) const {
  return modules_->find(module_name) != modules_->end();
}

void SourceLineResolver::FillSourceLineInfo(StackFrame *frame,
                                            StackFrameInfo *frame_info) const {
  ModuleMap::const_iterator it = modules_->find(frame->module_name);
  if (it != modules_->end()) {
    it->second->LookupAddress(frame->instruction - frame->module_base,
                              frame, frame_info);
  }
}

bool SourceLineResolver::Module::LoadMap(const string &map_file) {
  FILE *f = fopen(map_file.c_str(), "r");
  if (!f) {
    return false;
  }

  char buffer[1024];
  Function *cur_func = NULL;

  while (fgets(buffer, sizeof(buffer), f)) {
    if (strncmp(buffer, "FILE ", 5) == 0) {
      ParseFile(buffer);
    } else if (strncmp(buffer, "STACK ", 6) == 0) {
      if (!ParseStackInfo(buffer)) {
        return false;
      }
    } else if (strncmp(buffer, "FUNC ", 5) == 0) {
      cur_func = ParseFunction(buffer);
      if (!cur_func) {
        return false;
      }
      functions_.StoreRange(cur_func->address, cur_func->size,
                            linked_ptr<Function>(cur_func));
    } else {
      if (!cur_func) {
        return false;
      }
      Line *line = ParseLine(buffer);
      if (!line) {
        return false;
      }
      cur_func->lines.StoreRange(line->address, line->size,
                                 linked_ptr<Line>(line));
    }
  }

  fclose(f);
  return true;
}

void SourceLineResolver::Module::LookupAddress(
    MemAddr address, StackFrame *frame, StackFrameInfo *frame_info) const {
  if (frame_info) {
    // Check for debugging info first, before any possible early returns.
    // The caller will know that frame_info was filled in by checking its
    // valid field.
    //
    // We only know about STACK_INFO_FRAME_DATA and STACK_INFO_FPO.
    // STACK_INFO_STANDARD looks like it would do the right thing, too.
    // Prefer them in this order.
    if (!stack_info_[STACK_INFO_FRAME_DATA].RetrieveRange(address,
                                                          frame_info)) {
      if (!stack_info_[STACK_INFO_FPO].RetrieveRange(address, frame_info)) {
        stack_info_[STACK_INFO_STANDARD].RetrieveRange(address, frame_info);
      }
    }
  }

  linked_ptr<Function> func;
  if (!functions_.RetrieveRange(address, &func)) {
    return;
  }

  frame->function_name = func->name;
  linked_ptr<Line> line;
  if (!func->lines.RetrieveRange(address, &line)) {
    return;
  }

  FileMap::const_iterator it = files_.find(line->source_file_id);
  if (it != files_.end()) {
    frame->source_file_name = files_.find(line->source_file_id)->second;
  }
  frame->source_line = line->line;
}

// static
bool SourceLineResolver::Module::Tokenize(char *line, int max_tokens,
                                          vector<char*> *tokens) {
  tokens->clear();
  tokens->reserve(max_tokens);

  int remaining = max_tokens;

  // Split tokens on the space character.  Look for newlines too to
  // strip them out before exhausting max_tokens.
  char *token = strtok(line, " \r\n");
  while (token && --remaining > 0) {
    tokens->push_back(token);
    if (remaining > 1)
      token = strtok(NULL, " \r\n");
  }

  // If there's anything left, just add it as a single token.
  if (!remaining > 0) {
    if ((token = strtok(NULL, "\r\n"))) {
      tokens->push_back(token);
    }
  }

  return tokens->size() == static_cast<unsigned int>(max_tokens);
}

void SourceLineResolver::Module::ParseFile(char *file_line) {
  // FILE <id> <filename>
  file_line += 5;  // skip prefix

  vector<char*> tokens;
  if (!Tokenize(file_line, 2, &tokens)) {
    return;
  }

  int index = atoi(tokens[0]);
  if (index < 0) {
    return;
  }

  char *filename = tokens[1];
  if (filename) {
    files_.insert(make_pair(index, string(filename)));
  }
}

SourceLineResolver::Function* SourceLineResolver::Module::ParseFunction(
    char *function_line) {
  // FUNC <address> <name>
  function_line += 5;  // skip prefix

  vector<char*> tokens;
  if (!Tokenize(function_line, 3, &tokens)) {
    return NULL;
  }

  u_int64_t address = strtoull(tokens[0], NULL, 16);
  u_int64_t size    = strtoull(tokens[1], NULL, 16);
  char *name        = tokens[2];

  return new Function(name, address, size);
}

SourceLineResolver::Line* SourceLineResolver::Module::ParseLine(
    char *line_line) {
  // <address> <line number> <source file id>
  vector<char*> tokens;
  if (!Tokenize(line_line, 4, &tokens)) {
    return NULL;
  }

  u_int64_t address = strtoull(tokens[0], NULL, 16);
  u_int64_t size    = strtoull(tokens[1], NULL, 16);
  int line_number   = atoi(tokens[2]);
  int source_file   = atoi(tokens[3]);
  if (line_number <= 0) {
    return NULL;
  }

  return new Line(address, size, source_file, line_number);
}

bool SourceLineResolver::Module::ParseStackInfo(char *stack_info_line) {
  // STACK WIN <type> <rva> <code_size> <prolog_size> <epliog_size>
  // <parameter_size> <saved_register_size> <local_size> <max_stack_size>
  // <program_string>

  // Skip "STACK " prefix.
  stack_info_line += 6;

  vector<char*> tokens;
  if (!Tokenize(stack_info_line, 11, &tokens))
    return false;

  // Only MSVC stack frame info is understood for now.
  char *platform = tokens[0];
  if (strcmp(platform, "WIN") != 0)
    return false;

  int type = strtol(tokens[1], NULL, 16);
  if (type < 0 || type > STACK_INFO_LAST - 1)
    return false;

  u_int64_t rva                 = strtoull(tokens[2], NULL, 16);
  u_int64_t code_size           = strtoull(tokens[3], NULL, 16);
  u_int32_t prolog_size         =  strtoul(tokens[4], NULL, 16);
  u_int32_t epilog_size         =  strtoul(tokens[5], NULL, 16);
  u_int32_t parameter_size      =  strtoul(tokens[6], NULL, 16);
  u_int32_t saved_register_size =  strtoul(tokens[7], NULL, 16);
  u_int32_t local_size          =  strtoul(tokens[8], NULL, 16);
  u_int32_t max_stack_size      =  strtoul(tokens[9], NULL, 16);
  char *program_string          = tokens[10];

  // TODO(mmentovai): I wanted to use StoreRange's return value as this
  // method's return value, but MSVC infrequently outputs stack info that
  // violates the containment rules.  This happens with a section of code
  // in strncpy_s in test_app.cc (testdata/minidump2).  There, problem looks
  // like this:
  //   STACK WIN 4 4242 1a a 0 ...  (STACK WIN 4 base size prolog 0 ...)
  //   STACK WIN 4 4243 2e 9 0 ...
  // ContainedRangeMap treats these two blocks as conflicting.  In reality,
  // when the prolog lengths are taken into account, the actual code of
  // these blocks doesn't conflict.  However, we can't take the prolog lengths
  // into account directly here because we'd wind up with a different set
  // of range conflicts when MSVC outputs stack info like this:
  //   STACK WIN 4 1040 73 33 0 ...
  //   STACK WIN 4 105a 59 19 0 ...
  // because in both of these entries, the beginning of the code after the
  // prolog is at 0x1073, and the last byte of contained code is at 0x10b2.
  // Perhaps we could get away with storing ranges by rva + prolog_size
  // if ContainedRangeMap were modified to allow replacement of
  // already-stored values.

  stack_info_[type].StoreRange(rva, code_size,
                               StackFrameInfo(prolog_size,
                                              epilog_size,
                                              parameter_size,
                                              saved_register_size,
                                              local_size,
                                              max_stack_size,
                                              program_string));

  return true;
}

size_t SourceLineResolver::HashString::operator()(const string &s) const {
  return hash<const char*>()(s.c_str());
}

} // namespace google_airbag
