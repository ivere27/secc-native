#pragma once

#include <iostream>
#include <algorithm>

using namespace std;

std::string _exec(const char* cmd);
std::string _basename(const std::string &path, bool removeExt);
std::string _dirname(const std::string &path);

static inline std::string &ltrim(std::string &s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
  return s;
}
static inline std::string &rtrim(std::string &s) {
  s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
  return s;
}
static inline std::string &trim(std::string &s) {
  return ltrim(rtrim(s));
}
