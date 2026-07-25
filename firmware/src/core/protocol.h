#pragma once
#include "core/types.h"

namespace core {

enum class Op { Hello, Schema, Get, GetAll, Set, Save, Defaults, Tlm, Unknown };

struct Request {
  uint32_t id      = 0;
  Op       op      = Op::Unknown;
  char     key[40] = {0};
  bool     hasNum  = false;
  int32_t  num     = 0;
  bool     hasStr  = false;
  char     str[kMaxStrLen + 1] = {0};
  bool     tlmOn   = false;
  bool     ok      = false;
  const char* err  = nullptr;   // "badjson" | "badop" when !ok
};

// Assembles bytes into newline-terminated lines with overflow detection.
class LineReader {
 public:
  bool feed(char c);                       // true when a line completed
  const char* line() const { return buf_; }
  bool overflowed() const { return lineOverflowed_; }
  void reset();

 private:
  char   buf_[kMaxLineIn + 1] = {0};
  size_t len_ = 0;
  bool   dropping_ = false;        // current line already too long
  bool   lineOverflowed_ = false;  // the COMPLETED line was too long
};

Request parseRequest(const char* line);

}  // namespace core
