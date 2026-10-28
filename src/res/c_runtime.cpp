#include "res/c_runtime.h"

#include <cstdlib>
#include <string>

namespace ragexe::res {

bool IsCSpace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

std::int32_t AtoI(std::string_view s) {
  std::size_t i = 0;
  while (i < s.size() && IsCSpace(s[i])) ++i;
  bool negative = false;
  if (i < s.size() && (s[i] == '-' || s[i] == '+')) negative = s[i++] == '-';
  std::uint32_t total = 0;
  for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i)
    total = total * 10 + static_cast<std::uint32_t>(s[i] - '0');
  return static_cast<std::int32_t>(negative ? 0u - total : total);
}

double AtoF(std::string_view s) {
  std::size_t i = 0;
  while (i < s.size() && IsCSpace(s[i])) ++i;
  std::string number;
  if (i < s.size() && (s[i] == '-' || s[i] == '+')) number += s[i++];
  const auto digits = [&] {
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') number += s[i++];
  };
  digits();
  if (i < s.size() && s[i] == '.') {
    number += s[i++];
    digits();
  }
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E' || s[i] == 'd' || s[i] == 'D')) {
    std::size_t j = i + 1;
    std::string exponent = "e";
    if (j < s.size() && (s[j] == '-' || s[j] == '+')) exponent += s[j++];
    if (j < s.size() && s[j] >= '0' && s[j] <= '9') {
      while (j < s.size() && s[j] >= '0' && s[j] <= '9') exponent += s[j++];
      number += exponent;
    }
  }
  if (number.find_first_of("0123456789") == std::string::npos) return 0.0;
  return std::strtod(number.c_str(), nullptr);
}

}  // namespace ragexe::res
