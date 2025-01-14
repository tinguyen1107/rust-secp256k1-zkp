#ifndef SECP256K1_INT128_STRUCT_H
#define SECP256K1_INT128_STRUCT_H

#include <stdint.h>
#include "util.h"

typedef struct {
  uint64_t lo;
  uint64_t hi;
} rustsecp256k1zkp_v0_8_1_uint128;

typedef rustsecp256k1zkp_v0_8_1_uint128 rustsecp256k1zkp_v0_8_1_int128;

#endif
