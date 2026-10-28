#include "core/crt_rand.h"

namespace ragexe::core {

std::int32_t CrtRand::Next() {
  state_ = state_ * 0x343fdu + 0x269ec3u;
  return static_cast<std::int32_t>((state_ >> 16) & 0x7fff);
}

}  // namespace ragexe::core
