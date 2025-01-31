/**********************************************************************
 * Copyright (c) 2014, 2015 Gregory Maxwell                          *
 * Distributed under the MIT software license, see the accompanying   *
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/

#ifndef SECP256K1_PEDERSEN_H
#define SECP256K1_PEDERSEN_H

#include "../../ecmult_gen.h"
#include "../../group.h"
#include "../../scalar.h"

#include <stdint.h>

/** Multiply a small number with the generator: r = gn*G2 */
static void rustsecp256k1zkp_v0_8_1_pedersen_ecmult_small(rustsecp256k1zkp_v0_8_1_gej *r, uint64_t gn, const rustsecp256k1zkp_v0_8_1_ge* genp);

/* sec * G + value * G2. */
static void rustsecp256k1zkp_v0_8_1_pedersen_ecmult(const rustsecp256k1zkp_v0_8_1_ecmult_gen_context *ecmult_gen_ctx, rustsecp256k1zkp_v0_8_1_gej *rj, const rustsecp256k1zkp_v0_8_1_scalar *sec, uint64_t value, const rustsecp256k1zkp_v0_8_1_ge* genp);

#endif
