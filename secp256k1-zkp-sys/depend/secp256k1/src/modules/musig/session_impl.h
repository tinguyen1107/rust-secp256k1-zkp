/***********************************************************************
 * Copyright (c) 2021 Jonas Nick                                       *
 * Distributed under the MIT software license, see the accompanying    *
 * file COPYING or https://www.opensource.org/licenses/mit-license.php.*
 ***********************************************************************/

#ifndef SECP256K1_MODULE_MUSIG_SESSION_IMPL_H
#define SECP256K1_MODULE_MUSIG_SESSION_IMPL_H

#include <string.h>

#include "../../../include/secp256k1.h"
#include "../../../include/secp256k1_extrakeys.h"
#include "../../../include/secp256k1_musig.h"

#include "keyagg.h"
#include "session.h"
#include "../../eckey.h"
#include "../../hash.h"
#include "../../scalar.h"
#include "../../util.h"

static const unsigned char rustsecp256k1zkp_v0_8_1_musig_secnonce_magic[4] = { 0x22, 0x0e, 0xdc, 0xf1 };

static void rustsecp256k1zkp_v0_8_1_musig_secnonce_save(rustsecp256k1zkp_v0_8_1_musig_secnonce *secnonce, const rustsecp256k1zkp_v0_8_1_scalar *k, rustsecp256k1zkp_v0_8_1_ge *pk) {
    memcpy(&secnonce->data[0], rustsecp256k1zkp_v0_8_1_musig_secnonce_magic, 4);
    rustsecp256k1zkp_v0_8_1_scalar_get_b32(&secnonce->data[4], &k[0]);
    rustsecp256k1zkp_v0_8_1_scalar_get_b32(&secnonce->data[36], &k[1]);
    rustsecp256k1zkp_v0_8_1_point_save(&secnonce->data[68], pk);
}

static int rustsecp256k1zkp_v0_8_1_musig_secnonce_load(const rustsecp256k1zkp_v0_8_1_context* ctx, rustsecp256k1zkp_v0_8_1_scalar *k, rustsecp256k1zkp_v0_8_1_ge *pk, rustsecp256k1zkp_v0_8_1_musig_secnonce *secnonce) {
    int is_zero;
    ARG_CHECK(rustsecp256k1zkp_v0_8_1_memcmp_var(&secnonce->data[0], rustsecp256k1zkp_v0_8_1_musig_secnonce_magic, 4) == 0);
    rustsecp256k1zkp_v0_8_1_scalar_set_b32(&k[0], &secnonce->data[4], NULL);
    rustsecp256k1zkp_v0_8_1_scalar_set_b32(&k[1], &secnonce->data[36], NULL);
    rustsecp256k1zkp_v0_8_1_point_load(pk, &secnonce->data[68]);
    /* We make very sure that the nonce isn't invalidated by checking the values
     * in addition to the magic. */
    is_zero = rustsecp256k1zkp_v0_8_1_scalar_is_zero(&k[0]) & rustsecp256k1zkp_v0_8_1_scalar_is_zero(&k[1]);
    rustsecp256k1zkp_v0_8_1_declassify(ctx, &is_zero, sizeof(is_zero));
    ARG_CHECK(!is_zero);
    return 1;
}

/* If flag is true, invalidate the secnonce; otherwise leave it. Constant-time. */
static void rustsecp256k1zkp_v0_8_1_musig_secnonce_invalidate(const rustsecp256k1zkp_v0_8_1_context* ctx, rustsecp256k1zkp_v0_8_1_musig_secnonce *secnonce, int flag) {
    rustsecp256k1zkp_v0_8_1_memczero(secnonce->data, sizeof(secnonce->data), flag);
    /* The flag argument is usually classified. So, the line above makes the
     * magic and public key classified. However, we need both to be
     * declassified. Note that we don't declassify the entire object, because if
     * flag is 0, then k[0] and k[1] have not been zeroed. */
    rustsecp256k1zkp_v0_8_1_declassify(ctx, secnonce->data, sizeof(rustsecp256k1zkp_v0_8_1_musig_secnonce_magic));
    rustsecp256k1zkp_v0_8_1_declassify(ctx, &secnonce->data[68], 64);
}

static const unsigned char rustsecp256k1zkp_v0_8_1_musig_pubnonce_magic[4] = { 0xf5, 0x7a, 0x3d, 0xa0 };

/* Saves two group elements into a pubnonce. Requires that none of the provided
 * group elements is infinity. */
static void rustsecp256k1zkp_v0_8_1_musig_pubnonce_save(rustsecp256k1zkp_v0_8_1_musig_pubnonce* nonce, rustsecp256k1zkp_v0_8_1_ge* ge) {
    int i;
    memcpy(&nonce->data[0], rustsecp256k1zkp_v0_8_1_musig_pubnonce_magic, 4);
    for (i = 0; i < 2; i++) {
        rustsecp256k1zkp_v0_8_1_point_save(nonce->data + 4+64*i, &ge[i]);
    }
}

/* Loads two group elements from a pubnonce. Returns 1 unless the nonce wasn't
 * properly initialized */
static int rustsecp256k1zkp_v0_8_1_musig_pubnonce_load(const rustsecp256k1zkp_v0_8_1_context* ctx, rustsecp256k1zkp_v0_8_1_ge* ge, const rustsecp256k1zkp_v0_8_1_musig_pubnonce* nonce) {
    int i;

    ARG_CHECK(rustsecp256k1zkp_v0_8_1_memcmp_var(&nonce->data[0], rustsecp256k1zkp_v0_8_1_musig_pubnonce_magic, 4) == 0);
    for (i = 0; i < 2; i++) {
        rustsecp256k1zkp_v0_8_1_point_load(&ge[i], nonce->data + 4 + 64*i);
    }
    return 1;
}

static const unsigned char rustsecp256k1zkp_v0_8_1_musig_aggnonce_magic[4] = { 0xa8, 0xb7, 0xe4, 0x67 };

static void rustsecp256k1zkp_v0_8_1_musig_aggnonce_save(rustsecp256k1zkp_v0_8_1_musig_aggnonce* nonce, rustsecp256k1zkp_v0_8_1_ge* ge) {
    int i;
    memcpy(&nonce->data[0], rustsecp256k1zkp_v0_8_1_musig_aggnonce_magic, 4);
    for (i = 0; i < 2; i++) {
        rustsecp256k1zkp_v0_8_1_point_save_ext(&nonce->data[4 + 64*i], &ge[i]);
    }
}

static int rustsecp256k1zkp_v0_8_1_musig_aggnonce_load(const rustsecp256k1zkp_v0_8_1_context* ctx, rustsecp256k1zkp_v0_8_1_ge* ge, const rustsecp256k1zkp_v0_8_1_musig_aggnonce* nonce) {
    int i;

    ARG_CHECK(rustsecp256k1zkp_v0_8_1_memcmp_var(&nonce->data[0], rustsecp256k1zkp_v0_8_1_musig_aggnonce_magic, 4) == 0);
    for (i = 0; i < 2; i++) {
        rustsecp256k1zkp_v0_8_1_point_load_ext(&ge[i], &nonce->data[4 + 64*i]);
    }
    return 1;
}

static const unsigned char rustsecp256k1zkp_v0_8_1_musig_session_cache_magic[4] = { 0x9d, 0xed, 0xe9, 0x17 };

/* A session consists of
 * - 4 byte session cache magic
 * - 1 byte the parity of the final nonce
 * - 32 byte serialized x-only final nonce
 * - 32 byte nonce coefficient b
 * - 32 byte signature challenge hash e
 * - 32 byte scalar s that is added to the partial signatures of the signers
 */
static void rustsecp256k1zkp_v0_8_1_musig_session_save(rustsecp256k1zkp_v0_8_1_musig_session *session, const rustsecp256k1zkp_v0_8_1_musig_session_internal *session_i) {
    unsigned char *ptr = session->data;

    memcpy(ptr, rustsecp256k1zkp_v0_8_1_musig_session_cache_magic, 4);
    ptr += 4;
    *ptr = session_i->fin_nonce_parity;
    ptr += 1;
    memcpy(ptr, session_i->fin_nonce, 32);
    ptr += 32;
    rustsecp256k1zkp_v0_8_1_scalar_get_b32(ptr, &session_i->noncecoef);
    ptr += 32;
    rustsecp256k1zkp_v0_8_1_scalar_get_b32(ptr, &session_i->challenge);
    ptr += 32;
    rustsecp256k1zkp_v0_8_1_scalar_get_b32(ptr, &session_i->s_part);
}

static int rustsecp256k1zkp_v0_8_1_musig_session_load(const rustsecp256k1zkp_v0_8_1_context* ctx, rustsecp256k1zkp_v0_8_1_musig_session_internal *session_i, const rustsecp256k1zkp_v0_8_1_musig_session *session) {
    const unsigned char *ptr = session->data;

    ARG_CHECK(rustsecp256k1zkp_v0_8_1_memcmp_var(ptr, rustsecp256k1zkp_v0_8_1_musig_session_cache_magic, 4) == 0);
    ptr += 4;
    session_i->fin_nonce_parity = *ptr;
    ptr += 1;
    memcpy(session_i->fin_nonce, ptr, 32);
    ptr += 32;
    rustsecp256k1zkp_v0_8_1_scalar_set_b32(&session_i->noncecoef, ptr, NULL);
    ptr += 32;
    rustsecp256k1zkp_v0_8_1_scalar_set_b32(&session_i->challenge, ptr, NULL);
    ptr += 32;
    rustsecp256k1zkp_v0_8_1_scalar_set_b32(&session_i->s_part, ptr, NULL);
    return 1;
}

static const unsigned char rustsecp256k1zkp_v0_8_1_musig_partial_sig_magic[4] = { 0xeb, 0xfb, 0x1a, 0x32 };

static void rustsecp256k1zkp_v0_8_1_musig_partial_sig_save(rustsecp256k1zkp_v0_8_1_musig_partial_sig* sig, rustsecp256k1zkp_v0_8_1_scalar *s) {
    memcpy(&sig->data[0], rustsecp256k1zkp_v0_8_1_musig_partial_sig_magic, 4);
    rustsecp256k1zkp_v0_8_1_scalar_get_b32(&sig->data[4], s);
}

static int rustsecp256k1zkp_v0_8_1_musig_partial_sig_load(const rustsecp256k1zkp_v0_8_1_context* ctx, rustsecp256k1zkp_v0_8_1_scalar *s, const rustsecp256k1zkp_v0_8_1_musig_partial_sig* sig) {
    int overflow;

    ARG_CHECK(rustsecp256k1zkp_v0_8_1_memcmp_var(&sig->data[0], rustsecp256k1zkp_v0_8_1_musig_partial_sig_magic, 4) == 0);
    rustsecp256k1zkp_v0_8_1_scalar_set_b32(s, &sig->data[4], &overflow);
    /* Parsed signatures can not overflow */
    VERIFY_CHECK(!overflow);
    return 1;
}

int rustsecp256k1zkp_v0_8_1_musig_pubnonce_serialize(const rustsecp256k1zkp_v0_8_1_context* ctx, unsigned char *out66, const rustsecp256k1zkp_v0_8_1_musig_pubnonce* nonce) {
    rustsecp256k1zkp_v0_8_1_ge ge[2];
    int i;

    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(out66 != NULL);
    memset(out66, 0, 66);
    ARG_CHECK(nonce != NULL);

    if (!rustsecp256k1zkp_v0_8_1_musig_pubnonce_load(ctx, ge, nonce)) {
        return 0;
    }
    for (i = 0; i < 2; i++) {
        int ret;
        size_t size = 33;
        ret = rustsecp256k1zkp_v0_8_1_eckey_pubkey_serialize(&ge[i], &out66[33*i], &size, 1);
        /* serialize must succeed because the point was just loaded */
        VERIFY_CHECK(ret && size == 33);
    }
    return 1;
}

int rustsecp256k1zkp_v0_8_1_musig_pubnonce_parse(const rustsecp256k1zkp_v0_8_1_context* ctx, rustsecp256k1zkp_v0_8_1_musig_pubnonce* nonce, const unsigned char *in66) {
    rustsecp256k1zkp_v0_8_1_ge ge[2];
    int i;

    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(nonce != NULL);
    ARG_CHECK(in66 != NULL);

    for (i = 0; i < 2; i++) {
        if (!rustsecp256k1zkp_v0_8_1_eckey_pubkey_parse(&ge[i], &in66[33*i], 33)) {
            return 0;
        }
        if (!rustsecp256k1zkp_v0_8_1_ge_is_in_correct_subgroup(&ge[i])) {
            return 0;
        }
    }
    rustsecp256k1zkp_v0_8_1_musig_pubnonce_save(nonce, ge);
    return 1;
}

int rustsecp256k1zkp_v0_8_1_musig_aggnonce_serialize(const rustsecp256k1zkp_v0_8_1_context* ctx, unsigned char *out66, const rustsecp256k1zkp_v0_8_1_musig_aggnonce* nonce) {
    rustsecp256k1zkp_v0_8_1_ge ge[2];
    int i;

    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(out66 != NULL);
    memset(out66, 0, 66);
    ARG_CHECK(nonce != NULL);

    if (!rustsecp256k1zkp_v0_8_1_musig_aggnonce_load(ctx, ge, nonce)) {
        return 0;
    }
    for (i = 0; i < 2; i++) {
        rustsecp256k1zkp_v0_8_1_ge_serialize_ext(&out66[33*i], &ge[i]);
    }
    return 1;
}

int rustsecp256k1zkp_v0_8_1_musig_aggnonce_parse(const rustsecp256k1zkp_v0_8_1_context* ctx, rustsecp256k1zkp_v0_8_1_musig_aggnonce* nonce, const unsigned char *in66) {
    rustsecp256k1zkp_v0_8_1_ge ge[2];
    int i;

    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(nonce != NULL);
    ARG_CHECK(in66 != NULL);

    for (i = 0; i < 2; i++) {
        if (!rustsecp256k1zkp_v0_8_1_ge_parse_ext(&ge[i], &in66[33*i])) {
            return 0;
        }
    }
    rustsecp256k1zkp_v0_8_1_musig_aggnonce_save(nonce, ge);
    return 1;
}

int rustsecp256k1zkp_v0_8_1_musig_partial_sig_serialize(const rustsecp256k1zkp_v0_8_1_context* ctx, unsigned char *out32, const rustsecp256k1zkp_v0_8_1_musig_partial_sig* sig) {
    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(out32 != NULL);
    ARG_CHECK(sig != NULL);
    memcpy(out32, &sig->data[4], 32);
    return 1;
}

int rustsecp256k1zkp_v0_8_1_musig_partial_sig_parse(const rustsecp256k1zkp_v0_8_1_context* ctx, rustsecp256k1zkp_v0_8_1_musig_partial_sig* sig, const unsigned char *in32) {
    rustsecp256k1zkp_v0_8_1_scalar tmp;
    int overflow;
    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(sig != NULL);
    ARG_CHECK(in32 != NULL);

    rustsecp256k1zkp_v0_8_1_scalar_set_b32(&tmp, in32, &overflow);
    if (overflow) {
        return 0;
    }
    rustsecp256k1zkp_v0_8_1_musig_partial_sig_save(sig, &tmp);
    return 1;
}

/* Normalizes the x-coordinate of the given group element. */
static int rustsecp256k1zkp_v0_8_1_xonly_ge_serialize(unsigned char *output32, rustsecp256k1zkp_v0_8_1_ge *ge) {
    if (rustsecp256k1zkp_v0_8_1_ge_is_infinity(ge)) {
        return 0;
    }
    rustsecp256k1zkp_v0_8_1_fe_normalize_var(&ge->x);
    rustsecp256k1zkp_v0_8_1_fe_get_b32(output32, &ge->x);
    return 1;
}

/* Write optional inputs into the hash */
static void rustsecp256k1zkp_v0_8_1_nonce_function_musig_helper(rustsecp256k1zkp_v0_8_1_sha256 *sha, unsigned int prefix_size, const unsigned char *data, unsigned char len) {
    unsigned char zero[7] = { 0 };
    /* The spec requires length prefixes to be between 1 and 8 bytes
     * (inclusive) */
    VERIFY_CHECK(prefix_size <= 8);
    /* Since the length of all input data fits in a byte, we can always pad the
     * length prefix with prefix_size - 1 zero bytes. */
    rustsecp256k1zkp_v0_8_1_sha256_write(sha, zero, prefix_size - 1);
    if (data != NULL) {
        rustsecp256k1zkp_v0_8_1_sha256_write(sha, &len, 1);
        rustsecp256k1zkp_v0_8_1_sha256_write(sha, data, len);
    } else {
        len = 0;
        rustsecp256k1zkp_v0_8_1_sha256_write(sha, &len, 1);
    }
}

static void rustsecp256k1zkp_v0_8_1_nonce_function_musig(rustsecp256k1zkp_v0_8_1_scalar *k, const unsigned char *session_id, const unsigned char *msg32, const unsigned char *seckey32, const unsigned char *pk33, const unsigned char *agg_pk32, const unsigned char *extra_input32) {
    rustsecp256k1zkp_v0_8_1_sha256 sha;
    unsigned char rand[32];
    unsigned char i;
    unsigned char msg_present;

    if (seckey32 != NULL) {
        rustsecp256k1zkp_v0_8_1_sha256_initialize_tagged(&sha, (unsigned char*)"MuSig/aux", sizeof("MuSig/aux") - 1);
        rustsecp256k1zkp_v0_8_1_sha256_write(&sha, session_id, 32);
        rustsecp256k1zkp_v0_8_1_sha256_finalize(&sha, rand);
        for (i = 0; i < 32; i++) {
            rand[i] ^= seckey32[i];
        }
    } else {
        memcpy(rand, session_id, sizeof(rand));
    }

    /* Subtract one from `sizeof` to avoid hashing the implicit null byte */
    rustsecp256k1zkp_v0_8_1_sha256_initialize_tagged(&sha, (unsigned char*)"MuSig/nonce", sizeof("MuSig/nonce") - 1);
    rustsecp256k1zkp_v0_8_1_sha256_write(&sha, rand, sizeof(rand));
    rustsecp256k1zkp_v0_8_1_nonce_function_musig_helper(&sha, 1, pk33, 33);
    rustsecp256k1zkp_v0_8_1_nonce_function_musig_helper(&sha, 1, agg_pk32, 32);
    msg_present = msg32 != NULL;
    rustsecp256k1zkp_v0_8_1_sha256_write(&sha, &msg_present, 1);
    if (msg_present) {
        rustsecp256k1zkp_v0_8_1_nonce_function_musig_helper(&sha, 8, msg32, 32);
    }
    rustsecp256k1zkp_v0_8_1_nonce_function_musig_helper(&sha, 4, extra_input32, 32);

    for (i = 0; i < 2; i++) {
        unsigned char buf[32];
        rustsecp256k1zkp_v0_8_1_sha256 sha_tmp = sha;
        rustsecp256k1zkp_v0_8_1_sha256_write(&sha_tmp, &i, 1);
        rustsecp256k1zkp_v0_8_1_sha256_finalize(&sha_tmp, buf);
        rustsecp256k1zkp_v0_8_1_scalar_set_b32(&k[i], buf, NULL);
    }
}

int rustsecp256k1zkp_v0_8_1_musig_nonce_gen(const rustsecp256k1zkp_v0_8_1_context* ctx, rustsecp256k1zkp_v0_8_1_musig_secnonce *secnonce, rustsecp256k1zkp_v0_8_1_musig_pubnonce *pubnonce, const unsigned char *session_id32, const unsigned char *seckey, const rustsecp256k1zkp_v0_8_1_pubkey *pubkey, const unsigned char *msg32, const rustsecp256k1zkp_v0_8_1_musig_keyagg_cache *keyagg_cache, const unsigned char *extra_input32) {
    rustsecp256k1zkp_v0_8_1_keyagg_cache_internal cache_i;
    rustsecp256k1zkp_v0_8_1_scalar k[2];
    rustsecp256k1zkp_v0_8_1_ge nonce_pt[2];
    int i;
    unsigned char pk_ser[33];
    size_t pk_ser_len = sizeof(pk_ser);
    unsigned char aggpk_ser[32];
    unsigned char *aggpk_ser_ptr = NULL;
    rustsecp256k1zkp_v0_8_1_ge pk;
    int pk_serialize_success;
    int ret = 1;

    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(secnonce != NULL);
    memset(secnonce, 0, sizeof(*secnonce));
    ARG_CHECK(pubnonce != NULL);
    memset(pubnonce, 0, sizeof(*pubnonce));
    ARG_CHECK(session_id32 != NULL);
    ARG_CHECK(pubkey != NULL);
    ARG_CHECK(rustsecp256k1zkp_v0_8_1_ecmult_gen_context_is_built(&ctx->ecmult_gen_ctx));
    if (seckey == NULL) {
        /* Check in constant time that the session_id is not 0 as a
         * defense-in-depth measure that may protect against a faulty RNG. */
        unsigned char acc = 0;
        for (i = 0; i < 32; i++) {
            acc |= session_id32[i];
        }
        ret &= !!acc;
        memset(&acc, 0, sizeof(acc));
    }

    /* Check that the seckey is valid to be able to sign for it later. */
    if (seckey != NULL) {
        rustsecp256k1zkp_v0_8_1_scalar sk;
        ret &= rustsecp256k1zkp_v0_8_1_scalar_set_b32_seckey(&sk, seckey);
        rustsecp256k1zkp_v0_8_1_scalar_clear(&sk);
    }

    if (keyagg_cache != NULL) {
        int ret_tmp;
        if (!rustsecp256k1zkp_v0_8_1_keyagg_cache_load(ctx, &cache_i, keyagg_cache)) {
            return 0;
        }
        ret_tmp = rustsecp256k1zkp_v0_8_1_xonly_ge_serialize(aggpk_ser, &cache_i.pk);
        /* Serialization can not fail because the loaded point can not be infinity. */
        VERIFY_CHECK(ret_tmp);
        aggpk_ser_ptr = aggpk_ser;
    }
    if (!rustsecp256k1zkp_v0_8_1_pubkey_load(ctx, &pk, pubkey)) {
        return 0;
    }
    pk_serialize_success = rustsecp256k1zkp_v0_8_1_eckey_pubkey_serialize(&pk, pk_ser, &pk_ser_len, SECP256K1_EC_COMPRESSED);
    /* A pubkey cannot be the point at infinity */
    VERIFY_CHECK(pk_serialize_success);
    VERIFY_CHECK(pk_ser_len == sizeof(pk_ser));

    rustsecp256k1zkp_v0_8_1_nonce_function_musig(k, session_id32, msg32, seckey, pk_ser, aggpk_ser_ptr, extra_input32);
    VERIFY_CHECK(!rustsecp256k1zkp_v0_8_1_scalar_is_zero(&k[0]));
    VERIFY_CHECK(!rustsecp256k1zkp_v0_8_1_scalar_is_zero(&k[1]));
    VERIFY_CHECK(!rustsecp256k1zkp_v0_8_1_scalar_eq(&k[0], &k[1]));
    rustsecp256k1zkp_v0_8_1_musig_secnonce_save(secnonce, k, &pk);
    rustsecp256k1zkp_v0_8_1_musig_secnonce_invalidate(ctx, secnonce, !ret);

    for (i = 0; i < 2; i++) {
        rustsecp256k1zkp_v0_8_1_gej nonce_ptj;
        rustsecp256k1zkp_v0_8_1_ecmult_gen(&ctx->ecmult_gen_ctx, &nonce_ptj, &k[i]);
        rustsecp256k1zkp_v0_8_1_ge_set_gej(&nonce_pt[i], &nonce_ptj);
        rustsecp256k1zkp_v0_8_1_declassify(ctx, &nonce_pt[i], sizeof(nonce_pt));
        rustsecp256k1zkp_v0_8_1_scalar_clear(&k[i]);
    }
    /* nonce_pt won't be infinity because k != 0 with overwhelming probability */
    rustsecp256k1zkp_v0_8_1_musig_pubnonce_save(pubnonce, nonce_pt);
    return ret;
}

static int rustsecp256k1zkp_v0_8_1_musig_sum_nonces(const rustsecp256k1zkp_v0_8_1_context* ctx, rustsecp256k1zkp_v0_8_1_gej *summed_nonces, const rustsecp256k1zkp_v0_8_1_musig_pubnonce * const* pubnonces, size_t n_pubnonces) {
    size_t i;
    int j;

    rustsecp256k1zkp_v0_8_1_gej_set_infinity(&summed_nonces[0]);
    rustsecp256k1zkp_v0_8_1_gej_set_infinity(&summed_nonces[1]);

    for (i = 0; i < n_pubnonces; i++) {
        rustsecp256k1zkp_v0_8_1_ge nonce_pt[2];
        if (!rustsecp256k1zkp_v0_8_1_musig_pubnonce_load(ctx, nonce_pt, pubnonces[i])) {
            return 0;
        }
        for (j = 0; j < 2; j++) {
            rustsecp256k1zkp_v0_8_1_gej_add_ge_var(&summed_nonces[j], &summed_nonces[j], &nonce_pt[j], NULL);
        }
    }
    return 1;
}

int rustsecp256k1zkp_v0_8_1_musig_nonce_agg(const rustsecp256k1zkp_v0_8_1_context* ctx, rustsecp256k1zkp_v0_8_1_musig_aggnonce  *aggnonce, const rustsecp256k1zkp_v0_8_1_musig_pubnonce * const* pubnonces, size_t n_pubnonces) {
    rustsecp256k1zkp_v0_8_1_gej aggnonce_ptj[2];
    rustsecp256k1zkp_v0_8_1_ge aggnonce_pt[2];
    int i;
    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(aggnonce != NULL);
    ARG_CHECK(pubnonces != NULL);
    ARG_CHECK(n_pubnonces > 0);

    if (!rustsecp256k1zkp_v0_8_1_musig_sum_nonces(ctx, aggnonce_ptj, pubnonces, n_pubnonces)) {
        return 0;
    }
    for (i = 0; i < 2; i++) {
        rustsecp256k1zkp_v0_8_1_ge_set_gej(&aggnonce_pt[i], &aggnonce_ptj[i]);
    }
    rustsecp256k1zkp_v0_8_1_musig_aggnonce_save(aggnonce, aggnonce_pt);
    return 1;
}

/* tagged_hash(aggnonce[0], aggnonce[1], agg_pk, msg) */
static int rustsecp256k1zkp_v0_8_1_musig_compute_noncehash(unsigned char *noncehash, rustsecp256k1zkp_v0_8_1_ge *aggnonce, const unsigned char *agg_pk32, const unsigned char *msg) {
    unsigned char buf[33];
    rustsecp256k1zkp_v0_8_1_sha256 sha;
    int i;

    rustsecp256k1zkp_v0_8_1_sha256_initialize_tagged(&sha, (unsigned char*)"MuSig/noncecoef", sizeof("MuSig/noncecoef") - 1);
    for (i = 0; i < 2; i++) {
        rustsecp256k1zkp_v0_8_1_ge_serialize_ext(buf, &aggnonce[i]);
        rustsecp256k1zkp_v0_8_1_sha256_write(&sha, buf, sizeof(buf));
    }
    rustsecp256k1zkp_v0_8_1_sha256_write(&sha, agg_pk32, 32);
    rustsecp256k1zkp_v0_8_1_sha256_write(&sha, msg, 32);
    rustsecp256k1zkp_v0_8_1_sha256_finalize(&sha, noncehash);
    return 1;
}

static int rustsecp256k1zkp_v0_8_1_musig_nonce_process_internal(int *fin_nonce_parity, unsigned char *fin_nonce, rustsecp256k1zkp_v0_8_1_scalar *b, rustsecp256k1zkp_v0_8_1_gej *aggnoncej, const unsigned char *agg_pk32, const unsigned char *msg) {
    unsigned char noncehash[32];
    rustsecp256k1zkp_v0_8_1_ge fin_nonce_pt;
    rustsecp256k1zkp_v0_8_1_gej fin_nonce_ptj;
    rustsecp256k1zkp_v0_8_1_ge aggnonce[2];
    int ret;

    rustsecp256k1zkp_v0_8_1_ge_set_gej(&aggnonce[0], &aggnoncej[0]);
    rustsecp256k1zkp_v0_8_1_ge_set_gej(&aggnonce[1], &aggnoncej[1]);
    if (!rustsecp256k1zkp_v0_8_1_musig_compute_noncehash(noncehash, aggnonce, agg_pk32, msg)) {
        return 0;
    }
    /* fin_nonce = aggnonce[0] + b*aggnonce[1] */
    rustsecp256k1zkp_v0_8_1_scalar_set_b32(b, noncehash, NULL);
    rustsecp256k1zkp_v0_8_1_gej_set_infinity(&fin_nonce_ptj);
    rustsecp256k1zkp_v0_8_1_ecmult(&fin_nonce_ptj, &aggnoncej[1], b, NULL);
    rustsecp256k1zkp_v0_8_1_gej_add_ge_var(&fin_nonce_ptj, &fin_nonce_ptj, &aggnonce[0], NULL);
    rustsecp256k1zkp_v0_8_1_ge_set_gej(&fin_nonce_pt, &fin_nonce_ptj);
    if (rustsecp256k1zkp_v0_8_1_ge_is_infinity(&fin_nonce_pt)) {
        fin_nonce_pt = rustsecp256k1zkp_v0_8_1_ge_const_g;
    }
    ret = rustsecp256k1zkp_v0_8_1_xonly_ge_serialize(fin_nonce, &fin_nonce_pt);
    /* Can't fail since fin_nonce_pt is not infinity */
    VERIFY_CHECK(ret);
    rustsecp256k1zkp_v0_8_1_fe_normalize_var(&fin_nonce_pt.y);
    *fin_nonce_parity = rustsecp256k1zkp_v0_8_1_fe_is_odd(&fin_nonce_pt.y);
    return 1;
}

int rustsecp256k1zkp_v0_8_1_musig_nonce_process(const rustsecp256k1zkp_v0_8_1_context* ctx, rustsecp256k1zkp_v0_8_1_musig_session *session, const rustsecp256k1zkp_v0_8_1_musig_aggnonce  *aggnonce, const unsigned char *msg32, const rustsecp256k1zkp_v0_8_1_musig_keyagg_cache *keyagg_cache, const rustsecp256k1zkp_v0_8_1_pubkey *adaptor) {
    rustsecp256k1zkp_v0_8_1_keyagg_cache_internal cache_i;
    rustsecp256k1zkp_v0_8_1_ge aggnonce_pt[2];
    rustsecp256k1zkp_v0_8_1_gej aggnonce_ptj[2];
    unsigned char fin_nonce[32];
    rustsecp256k1zkp_v0_8_1_musig_session_internal session_i;
    unsigned char agg_pk32[32];

    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(session != NULL);
    ARG_CHECK(aggnonce != NULL);
    ARG_CHECK(msg32 != NULL);
    ARG_CHECK(keyagg_cache != NULL);

    if (!rustsecp256k1zkp_v0_8_1_keyagg_cache_load(ctx, &cache_i, keyagg_cache)) {
        return 0;
    }
    rustsecp256k1zkp_v0_8_1_fe_get_b32(agg_pk32, &cache_i.pk.x);

    if (!rustsecp256k1zkp_v0_8_1_musig_aggnonce_load(ctx, aggnonce_pt, aggnonce)) {
        return 0;
    }
    rustsecp256k1zkp_v0_8_1_gej_set_ge(&aggnonce_ptj[0], &aggnonce_pt[0]);
    rustsecp256k1zkp_v0_8_1_gej_set_ge(&aggnonce_ptj[1], &aggnonce_pt[1]);
    /* Add public adaptor to nonce */
    if (adaptor != NULL) {
        rustsecp256k1zkp_v0_8_1_ge adaptorp;
        if (!rustsecp256k1zkp_v0_8_1_pubkey_load(ctx, &adaptorp, adaptor)) {
            return 0;
        }
        rustsecp256k1zkp_v0_8_1_gej_add_ge_var(&aggnonce_ptj[0], &aggnonce_ptj[0], &adaptorp, NULL);
    }
    if (!rustsecp256k1zkp_v0_8_1_musig_nonce_process_internal(&session_i.fin_nonce_parity, fin_nonce, &session_i.noncecoef, aggnonce_ptj, agg_pk32, msg32)) {
        return 0;
    }

    rustsecp256k1zkp_v0_8_1_schnorrsig_challenge(&session_i.challenge, fin_nonce, msg32, 32, agg_pk32);

    /* If there is a tweak then set `challenge` times `tweak` to the `s`-part.*/
    rustsecp256k1zkp_v0_8_1_scalar_set_int(&session_i.s_part, 0);
    if (!rustsecp256k1zkp_v0_8_1_scalar_is_zero(&cache_i.tweak)) {
        rustsecp256k1zkp_v0_8_1_scalar e_tmp;
        rustsecp256k1zkp_v0_8_1_scalar_mul(&e_tmp, &session_i.challenge, &cache_i.tweak);
        if (rustsecp256k1zkp_v0_8_1_fe_is_odd(&cache_i.pk.y)) {
            rustsecp256k1zkp_v0_8_1_scalar_negate(&e_tmp, &e_tmp);
        }
        rustsecp256k1zkp_v0_8_1_scalar_add(&session_i.s_part, &session_i.s_part, &e_tmp);
    }
    memcpy(session_i.fin_nonce, fin_nonce, sizeof(session_i.fin_nonce));
    rustsecp256k1zkp_v0_8_1_musig_session_save(session, &session_i);
    return 1;
}

static int rustsecp256k1zkp_v0_8_1_blinded_musig_nonce_process_internal(
    int *fin_nonce_parity, 
    unsigned char *fin_nonce, 
    rustsecp256k1zkp_v0_8_1_scalar *b, 
    rustsecp256k1zkp_v0_8_1_gej *aggnoncej, 
    const unsigned char *agg_pk32, 
    const unsigned char *msg,
    const rustsecp256k1zkp_v0_8_1_ge *blinding_pk) 
{
    unsigned char noncehash[32];
    rustsecp256k1zkp_v0_8_1_ge fin_nonce_pt;
    rustsecp256k1zkp_v0_8_1_gej fin_nonce_ptj;
    rustsecp256k1zkp_v0_8_1_ge aggnonce[2];
    int ret;


    rustsecp256k1zkp_v0_8_1_ge_set_gej(&aggnonce[0], &aggnoncej[0]);
    rustsecp256k1zkp_v0_8_1_ge_set_gej(&aggnonce[1], &aggnoncej[1]);
    if (!rustsecp256k1zkp_v0_8_1_musig_compute_noncehash(noncehash, aggnonce, agg_pk32, msg)) {
        return 0;
    }
    /* fin_nonce = aggnonce[0] + b*aggnonce[1] */
    rustsecp256k1zkp_v0_8_1_scalar_set_b32(b, noncehash, NULL);
    rustsecp256k1zkp_v0_8_1_gej_set_infinity(&fin_nonce_ptj);
    rustsecp256k1zkp_v0_8_1_ecmult(&fin_nonce_ptj, &aggnoncej[1], b, NULL);
    rustsecp256k1zkp_v0_8_1_gej_add_ge_var(&fin_nonce_ptj, &fin_nonce_ptj, &aggnonce[0], NULL);
    rustsecp256k1zkp_v0_8_1_gej_add_ge_var(&fin_nonce_ptj, &fin_nonce_ptj, blinding_pk, NULL);
    rustsecp256k1zkp_v0_8_1_ge_set_gej(&fin_nonce_pt, &fin_nonce_ptj);
    if (rustsecp256k1zkp_v0_8_1_ge_is_infinity(&fin_nonce_pt)) {
        fin_nonce_pt = rustsecp256k1zkp_v0_8_1_ge_const_g;
    }

    ret = rustsecp256k1zkp_v0_8_1_xonly_ge_serialize(fin_nonce, &fin_nonce_pt);
    /* Can't fail since fin_nonce_pt is not infinity */
    VERIFY_CHECK(ret);
    rustsecp256k1zkp_v0_8_1_fe_normalize_var(&fin_nonce_pt.y);
    *fin_nonce_parity = rustsecp256k1zkp_v0_8_1_fe_is_odd(&fin_nonce_pt.y);
    return 1;
}

int rustsecp256k1zkp_v0_8_1_blinded_musig_nonce_process(
    const rustsecp256k1zkp_v0_8_1_context* ctx, 
    rustsecp256k1zkp_v0_8_1_musig_session *session, 
    const rustsecp256k1zkp_v0_8_1_musig_aggnonce  *aggnonce, 
    const unsigned char *msg32, 
    const rustsecp256k1zkp_v0_8_1_musig_keyagg_cache *keyagg_cache, 
    const rustsecp256k1zkp_v0_8_1_pubkey *adaptor,
    unsigned char* blinding_factor) 
{
    rustsecp256k1zkp_v0_8_1_keyagg_cache_internal cache_i;
    rustsecp256k1zkp_v0_8_1_ge aggnonce_pt[2];
    rustsecp256k1zkp_v0_8_1_gej aggnonce_ptj[2];
    unsigned char fin_nonce[32];
    rustsecp256k1zkp_v0_8_1_musig_session_internal session_i;
    unsigned char agg_pk32[32];

    int overflow = 0;

    rustsecp256k1zkp_v0_8_1_scalar blinding_factor_s;
    rustsecp256k1zkp_v0_8_1_ge blinding_pk;

    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(session != NULL);
    ARG_CHECK(aggnonce != NULL);
    ARG_CHECK(msg32 != NULL);
    ARG_CHECK(keyagg_cache != NULL);
    ARG_CHECK(blinding_factor != NULL);

    memset(&session_i, 0, sizeof(session_i));

    rustsecp256k1zkp_v0_8_1_ge_set_infinity(&blinding_pk);

    if (!rustsecp256k1zkp_v0_8_1_keyagg_cache_load(ctx, &cache_i, keyagg_cache)) {
        return 0;
    }
    rustsecp256k1zkp_v0_8_1_fe_get_b32(agg_pk32, &cache_i.pk.x);

    memcpy(&blinding_pk, &cache_i.pk, sizeof(rustsecp256k1zkp_v0_8_1_ge));

    rustsecp256k1zkp_v0_8_1_scalar_set_b32(&blinding_factor_s, blinding_factor, &overflow);
    if(overflow) {
        return 0;
    }

    if(!rustsecp256k1zkp_v0_8_1_eckey_pubkey_tweak_mul(&blinding_pk, &blinding_factor_s)) {
        return 0;
    }

    if (!rustsecp256k1zkp_v0_8_1_musig_aggnonce_load(ctx, aggnonce_pt, aggnonce)) {
        return 0;
    }
    rustsecp256k1zkp_v0_8_1_gej_set_ge(&aggnonce_ptj[0], &aggnonce_pt[0]);
    rustsecp256k1zkp_v0_8_1_gej_set_ge(&aggnonce_ptj[1], &aggnonce_pt[1]);
    /* Add public adaptor to nonce */
    if (adaptor != NULL) {
        rustsecp256k1zkp_v0_8_1_ge adaptorp;
        if (!rustsecp256k1zkp_v0_8_1_pubkey_load(ctx, &adaptorp, adaptor)) {
            return 0;
        }
        rustsecp256k1zkp_v0_8_1_gej_add_ge_var(&aggnonce_ptj[0], &aggnonce_ptj[0], &adaptorp, NULL);
    }
    if (!rustsecp256k1zkp_v0_8_1_blinded_musig_nonce_process_internal(&session_i.fin_nonce_parity, fin_nonce, &session_i.noncecoef, aggnonce_ptj, agg_pk32, msg32, &blinding_pk)) {
        return 0;
    }

    rustsecp256k1zkp_v0_8_1_schnorrsig_challenge(&session_i.challenge, fin_nonce, msg32, 32, agg_pk32);

    if (rustsecp256k1zkp_v0_8_1_fe_is_odd(&cache_i.pk.y) != session_i.fin_nonce_parity) {
        rustsecp256k1zkp_v0_8_1_scalar_negate(&blinding_factor_s, &blinding_factor_s);
    }

    rustsecp256k1zkp_v0_8_1_scalar_add(&session_i.challenge, &session_i.challenge, &blinding_factor_s);

    /* If there is a tweak then set `challenge` times `tweak` to the `s`-part.*/
    rustsecp256k1zkp_v0_8_1_scalar_set_int(&session_i.s_part, 0);
    if (!rustsecp256k1zkp_v0_8_1_scalar_is_zero(&cache_i.tweak)) {
        rustsecp256k1zkp_v0_8_1_scalar e_tmp;
        rustsecp256k1zkp_v0_8_1_scalar_mul(&e_tmp, &session_i.challenge, &cache_i.tweak);
        if (rustsecp256k1zkp_v0_8_1_fe_is_odd(&cache_i.pk.y)) {
            rustsecp256k1zkp_v0_8_1_scalar_negate(&e_tmp, &e_tmp);
        }
        rustsecp256k1zkp_v0_8_1_scalar_add(&session_i.s_part, &session_i.s_part, &e_tmp);
    }
    memcpy(session_i.fin_nonce, fin_nonce, sizeof(session_i.fin_nonce));
    rustsecp256k1zkp_v0_8_1_musig_session_save(session, &session_i);
    return 1;
}

int rustsecp256k1zkp_v0_8_1_blinded_musig_nonce_process_without_keyaggcoeff(
    const rustsecp256k1zkp_v0_8_1_context* ctx, 
    rustsecp256k1zkp_v0_8_1_musig_session *session, 
    const rustsecp256k1zkp_v0_8_1_musig_aggnonce  *aggnonce, 
    const unsigned char *msg32, 
    const rustsecp256k1zkp_v0_8_1_pubkey *aggregate_pubkey, 
    const rustsecp256k1zkp_v0_8_1_pubkey *adaptor,
    unsigned char* blinding_factor,
    const unsigned char *tweak32) 
{
    rustsecp256k1zkp_v0_8_1_ge aggnonce_pt[2];
    rustsecp256k1zkp_v0_8_1_gej aggnonce_ptj[2];
    unsigned char fin_nonce[32];
    rustsecp256k1zkp_v0_8_1_musig_session_internal session_i;
    unsigned char agg_pk32[32];

    rustsecp256k1zkp_v0_8_1_scalar tweak;

    int overflow = 0;

    rustsecp256k1zkp_v0_8_1_ge aggregate_pubkey_ge;

    rustsecp256k1zkp_v0_8_1_scalar blinding_factor_s;
    rustsecp256k1zkp_v0_8_1_ge blinding_pk;

    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(session != NULL);
    ARG_CHECK(aggnonce != NULL);
    ARG_CHECK(msg32 != NULL);
    ARG_CHECK(blinding_factor != NULL);

    memset(&session_i, 0, sizeof(session_i));

    rustsecp256k1zkp_v0_8_1_ge_set_infinity(&blinding_pk);

    /* convert aggregate_pubkey to rustsecp256k1zkp_v0_8_1_ge*/
    if (!rustsecp256k1zkp_v0_8_1_pubkey_load(ctx, &aggregate_pubkey_ge, aggregate_pubkey)) {
        return 0;
    }

    rustsecp256k1zkp_v0_8_1_scalar_set_b32(&tweak, tweak32, &overflow);
    if (overflow) {
        return 0;
    }

    rustsecp256k1zkp_v0_8_1_fe_get_b32(agg_pk32, &aggregate_pubkey_ge.x);

    memcpy(&blinding_pk, &aggregate_pubkey_ge, sizeof(rustsecp256k1zkp_v0_8_1_ge));

    rustsecp256k1zkp_v0_8_1_scalar_set_b32(&blinding_factor_s, blinding_factor, &overflow);
    if(overflow) {
        return 0;
    }

    if(!rustsecp256k1zkp_v0_8_1_eckey_pubkey_tweak_mul(&blinding_pk, &blinding_factor_s)) {
        return 0;
    }

    if (!rustsecp256k1zkp_v0_8_1_musig_aggnonce_load(ctx, aggnonce_pt, aggnonce)) {
        return 0;
    }
    rustsecp256k1zkp_v0_8_1_gej_set_ge(&aggnonce_ptj[0], &aggnonce_pt[0]);
    rustsecp256k1zkp_v0_8_1_gej_set_ge(&aggnonce_ptj[1], &aggnonce_pt[1]);
    /* Add public adaptor to nonce */
    if (adaptor != NULL) {
        rustsecp256k1zkp_v0_8_1_ge adaptorp;
        if (!rustsecp256k1zkp_v0_8_1_pubkey_load(ctx, &adaptorp, adaptor)) {
            return 0;
        }
        rustsecp256k1zkp_v0_8_1_gej_add_ge_var(&aggnonce_ptj[0], &aggnonce_ptj[0], &adaptorp, NULL);
    }
    if (!rustsecp256k1zkp_v0_8_1_blinded_musig_nonce_process_internal(&session_i.fin_nonce_parity, fin_nonce, &session_i.noncecoef, aggnonce_ptj, agg_pk32, msg32, &blinding_pk)) {
        return 0;
    }

    rustsecp256k1zkp_v0_8_1_schnorrsig_challenge(&session_i.challenge, fin_nonce, msg32, 32, agg_pk32);

    if (rustsecp256k1zkp_v0_8_1_fe_is_odd(&aggregate_pubkey_ge.y) != session_i.fin_nonce_parity) {
        rustsecp256k1zkp_v0_8_1_scalar_negate(&blinding_factor_s, &blinding_factor_s);
    }

    rustsecp256k1zkp_v0_8_1_scalar_add(&session_i.challenge, &session_i.challenge, &blinding_factor_s);

    /* If there is a tweak then set `challenge` times `tweak` to the `s`-part.*/
    rustsecp256k1zkp_v0_8_1_scalar_set_int(&session_i.s_part, 0);
    /* this part is not yet supported in blinded scheme. It assumes cache_i.tweak is zero */
    if (!rustsecp256k1zkp_v0_8_1_scalar_is_zero(&tweak)) {
        rustsecp256k1zkp_v0_8_1_scalar e_tmp;
        rustsecp256k1zkp_v0_8_1_scalar_mul(&e_tmp, &session_i.challenge, &tweak);
        if (rustsecp256k1zkp_v0_8_1_fe_is_odd(&aggregate_pubkey_ge.y)) {
            rustsecp256k1zkp_v0_8_1_scalar_negate(&e_tmp, &e_tmp);
        }
        rustsecp256k1zkp_v0_8_1_scalar_add(&session_i.s_part, &session_i.s_part, &e_tmp);
    }
    
    memcpy(session_i.fin_nonce, fin_nonce, sizeof(session_i.fin_nonce));
    rustsecp256k1zkp_v0_8_1_musig_session_save(session, &session_i);
    return 1;
}

static void rustsecp256k1zkp_v0_8_1_musig_partial_sign_clear(rustsecp256k1zkp_v0_8_1_scalar *sk, rustsecp256k1zkp_v0_8_1_scalar *k) {
    rustsecp256k1zkp_v0_8_1_scalar_clear(sk);
    rustsecp256k1zkp_v0_8_1_scalar_clear(&k[0]);
    rustsecp256k1zkp_v0_8_1_scalar_clear(&k[1]);
}

int rustsecp256k1zkp_v0_8_1_musig_get_keyaggcoef_and_negation_seckey(
    const rustsecp256k1zkp_v0_8_1_context* ctx,
    unsigned char *keyaggcoef, 
    int *negate_seckey,
    const rustsecp256k1zkp_v0_8_1_musig_keyagg_cache *keyagg_cache, 
    const rustsecp256k1zkp_v0_8_1_pubkey *pubkey
) {
    rustsecp256k1zkp_v0_8_1_scalar r;
    rustsecp256k1zkp_v0_8_1_ge pk;
    rustsecp256k1zkp_v0_8_1_keyagg_cache_internal cache_i;

    if (!rustsecp256k1zkp_v0_8_1_pubkey_load(ctx, &pk, pubkey)) {
        return 0;
    }

    rustsecp256k1zkp_v0_8_1_fe_normalize_var(&pk.y);
    rustsecp256k1zkp_v0_8_1_fe_normalize_var(&pk.x);

    if (!rustsecp256k1zkp_v0_8_1_keyagg_cache_load(ctx, &cache_i, keyagg_cache)) {
        return 0;
    }

    rustsecp256k1zkp_v0_8_1_musig_keyaggcoef(&r, &cache_i, &pk);
    rustsecp256k1zkp_v0_8_1_scalar_get_b32(keyaggcoef, &r);

    /* Negate sk if rustsecp256k1zkp_v0_8_1_fe_is_odd(&cache_i.pk.y)) XOR cache_i.parity_acc.
     * This corresponds to the line "Let d = g⋅gacc⋅d' mod n" in the
     * specification. */
    *negate_seckey = (rustsecp256k1zkp_v0_8_1_fe_is_odd(&cache_i.pk.y) != cache_i.parity_acc);

    return 1;
}

int rustsecp256k1zkp_v0_8_1_musig_negate_seckey(
    const rustsecp256k1zkp_v0_8_1_context* ctx,
    const rustsecp256k1zkp_v0_8_1_pubkey *aggregate_pubkey, 
    const int parity_acc,
    int *negate_seckey
){
    rustsecp256k1zkp_v0_8_1_ge aggregate_pubkey_ge;

    if (!rustsecp256k1zkp_v0_8_1_pubkey_load(ctx, &aggregate_pubkey_ge, aggregate_pubkey)) {
        return 0;
    }

    *negate_seckey = (rustsecp256k1zkp_v0_8_1_fe_is_odd(&aggregate_pubkey_ge.y) != parity_acc);

    return 1;
}

int rustsecp256k1zkp_v0_8_1_blinded_musig_pubkey_xonly_tweak_add(
    const rustsecp256k1zkp_v0_8_1_context* ctx, 
    rustsecp256k1zkp_v0_8_1_pubkey *output_pubkey, 
    int *parity_acc,
    const rustsecp256k1zkp_v0_8_1_pubkey *aggregate_pubkey, 
    const unsigned char *tweak32, 
    unsigned char *out_tweak32
){
    rustsecp256k1zkp_v0_8_1_ge aggregated_pubkey_ge;
    rustsecp256k1zkp_v0_8_1_scalar tweak;
    rustsecp256k1zkp_v0_8_1_scalar out_tweak;
    int overflow = 0;

    VERIFY_CHECK(ctx != NULL);
    if (output_pubkey != NULL) {
        memset(output_pubkey, 0, sizeof(*output_pubkey));
    }
    ARG_CHECK(aggregate_pubkey != NULL);
    ARG_CHECK(tweak32 != NULL);

    rustsecp256k1zkp_v0_8_1_scalar_set_b32(&tweak, tweak32, &overflow);
    if (overflow) {
        return 0;
    }

    rustsecp256k1zkp_v0_8_1_scalar_set_b32(&out_tweak, out_tweak32, &overflow);
    if (overflow) {
        return 0;
    }

    if (!rustsecp256k1zkp_v0_8_1_pubkey_load(ctx, &aggregated_pubkey_ge, aggregate_pubkey)) {
        return 0;
    }

    if (rustsecp256k1zkp_v0_8_1_extrakeys_ge_even_y(&aggregated_pubkey_ge)) {
        *parity_acc ^= 1;
        rustsecp256k1zkp_v0_8_1_scalar_negate(&out_tweak, &out_tweak);
    }

    rustsecp256k1zkp_v0_8_1_scalar_add(&out_tweak, &out_tweak, &tweak);
    if (!rustsecp256k1zkp_v0_8_1_eckey_pubkey_tweak_add(&aggregated_pubkey_ge, &out_tweak)) {
        return 0;
    }

    /* eckey_pubkey_tweak_add fails if cache_i.pk is infinity */
    VERIFY_CHECK(!rustsecp256k1zkp_v0_8_1_ge_is_infinity(&aggregated_pubkey_ge));
    rustsecp256k1zkp_v0_8_1_scalar_get_b32 (out_tweak32, &out_tweak);
    if (output_pubkey != NULL) {
        rustsecp256k1zkp_v0_8_1_pubkey_save(output_pubkey, &aggregated_pubkey_ge);
    }
    return 1;
}

int rustsecp256k1zkp_v0_8_1_musig_partial_sign(const rustsecp256k1zkp_v0_8_1_context* ctx, rustsecp256k1zkp_v0_8_1_musig_partial_sig *partial_sig, rustsecp256k1zkp_v0_8_1_musig_secnonce *secnonce, const rustsecp256k1zkp_v0_8_1_keypair *keypair, const rustsecp256k1zkp_v0_8_1_musig_keyagg_cache *keyagg_cache, const rustsecp256k1zkp_v0_8_1_musig_session *session) {
    rustsecp256k1zkp_v0_8_1_scalar sk;
    rustsecp256k1zkp_v0_8_1_ge pk, keypair_pk;
    rustsecp256k1zkp_v0_8_1_scalar k[2];
    rustsecp256k1zkp_v0_8_1_scalar mu, s;
    rustsecp256k1zkp_v0_8_1_keyagg_cache_internal cache_i;
    rustsecp256k1zkp_v0_8_1_musig_session_internal session_i;
    int ret;

    VERIFY_CHECK(ctx != NULL);

    ARG_CHECK(secnonce != NULL);
    /* Fails if the magic doesn't match */
    ret = rustsecp256k1zkp_v0_8_1_musig_secnonce_load(ctx, k, &pk, secnonce);
    /* Set nonce to zero to avoid nonce reuse. This will cause subsequent calls
     * of this function to fail */
    memset(secnonce, 0, sizeof(*secnonce));
    if (!ret) {
        rustsecp256k1zkp_v0_8_1_musig_partial_sign_clear(&sk, k);
        return 0;
    }

    ARG_CHECK(partial_sig != NULL);
    ARG_CHECK(keypair != NULL);
    ARG_CHECK(keyagg_cache != NULL);
    ARG_CHECK(session != NULL);

    if (!rustsecp256k1zkp_v0_8_1_keypair_load(ctx, &sk, &keypair_pk, keypair)) {
        rustsecp256k1zkp_v0_8_1_musig_partial_sign_clear(&sk, k);
        return 0;
    }
    ARG_CHECK(rustsecp256k1zkp_v0_8_1_fe_equal_var(&pk.x, &keypair_pk.x)
              && rustsecp256k1zkp_v0_8_1_fe_equal_var(&pk.y, &keypair_pk.y));
    if (!rustsecp256k1zkp_v0_8_1_keyagg_cache_load(ctx, &cache_i, keyagg_cache)) {
        rustsecp256k1zkp_v0_8_1_musig_partial_sign_clear(&sk, k);
        return 0;
    }
    rustsecp256k1zkp_v0_8_1_fe_normalize_var(&pk.y);

    /* Negate sk if rustsecp256k1zkp_v0_8_1_fe_is_odd(&cache_i.pk.y)) XOR cache_i.parity_acc.
     * This corresponds to the line "Let d = g⋅gacc⋅d' mod n" in the
     * specification. */
    if ((rustsecp256k1zkp_v0_8_1_fe_is_odd(&cache_i.pk.y)
         != cache_i.parity_acc)) {
        rustsecp256k1zkp_v0_8_1_scalar_negate(&sk, &sk);
    }

    /* Multiply KeyAgg coefficient */
    rustsecp256k1zkp_v0_8_1_fe_normalize_var(&pk.x);
    /* TODO Cache mu */
    rustsecp256k1zkp_v0_8_1_musig_keyaggcoef(&mu, &cache_i, &pk);
    rustsecp256k1zkp_v0_8_1_scalar_mul(&sk, &sk, &mu);

    if (!rustsecp256k1zkp_v0_8_1_musig_session_load(ctx, &session_i, session)) {
        rustsecp256k1zkp_v0_8_1_musig_partial_sign_clear(&sk, k);
        return 0;
    }

    if (session_i.fin_nonce_parity) {
        rustsecp256k1zkp_v0_8_1_scalar_negate(&k[0], &k[0]);
        rustsecp256k1zkp_v0_8_1_scalar_negate(&k[1], &k[1]);
    }

    /* Sign */
    rustsecp256k1zkp_v0_8_1_scalar_mul(&s, &session_i.challenge, &sk);
    rustsecp256k1zkp_v0_8_1_scalar_mul(&k[1], &session_i.noncecoef, &k[1]);
    rustsecp256k1zkp_v0_8_1_scalar_add(&k[0], &k[0], &k[1]);
    rustsecp256k1zkp_v0_8_1_scalar_add(&s, &s, &k[0]);
    rustsecp256k1zkp_v0_8_1_musig_partial_sig_save(partial_sig, &s);
    rustsecp256k1zkp_v0_8_1_musig_partial_sign_clear(&sk, k);
    return 1;
}

SECP256K1_API int rustsecp256k1zkp_v0_8_1_get_challenge_from_session(
    const rustsecp256k1zkp_v0_8_1_context* ctx, 
    const rustsecp256k1zkp_v0_8_1_musig_session *session,
    unsigned char* challenge
) {
    rustsecp256k1zkp_v0_8_1_musig_session_internal session_i;
    if (!rustsecp256k1zkp_v0_8_1_musig_session_load(ctx, &session_i, session)) {
        return 0;
    }
    rustsecp256k1zkp_v0_8_1_scalar_get_b32(challenge, &session_i.challenge);
    return 1;
}

SECP256K1_API int rustsecp256k1zkp_v0_8_1_blinded_musig_remove_fin_nonce_from_session(
    const rustsecp256k1zkp_v0_8_1_context *ctx,
    rustsecp256k1zkp_v0_8_1_musig_session *session
) {
    rustsecp256k1zkp_v0_8_1_musig_session_internal session_i;

    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(session != NULL);

    if (!rustsecp256k1zkp_v0_8_1_musig_session_load(ctx, &session_i, session)) {
        return 0;
    }

    memset(session_i.fin_nonce, 0, sizeof(session_i.fin_nonce));
    rustsecp256k1zkp_v0_8_1_musig_session_save(session, &session_i);
    return 1;
}

SECP256K1_API int rustsecp256k1zkp_v0_8_1_blinded_musig_partial_sign(
    const rustsecp256k1zkp_v0_8_1_context *ctx,
    rustsecp256k1zkp_v0_8_1_musig_partial_sig *partial_sig,
    rustsecp256k1zkp_v0_8_1_musig_secnonce *secnonce,
    const rustsecp256k1zkp_v0_8_1_keypair *keypair,
    const rustsecp256k1zkp_v0_8_1_musig_session *session,
    const unsigned char *keyaggcoef,
    const int negate_seckey
)
{
    rustsecp256k1zkp_v0_8_1_scalar sk;
    rustsecp256k1zkp_v0_8_1_ge pk, keypair_pk;
    rustsecp256k1zkp_v0_8_1_scalar k[2];
    rustsecp256k1zkp_v0_8_1_scalar mu, s;
    rustsecp256k1zkp_v0_8_1_musig_session_internal session_i;
    int ret;

    VERIFY_CHECK(ctx != NULL);

    ARG_CHECK(secnonce != NULL);
    /* Fails if the magic doesn't match */
    ret = rustsecp256k1zkp_v0_8_1_musig_secnonce_load(ctx, k, &pk, secnonce);
    /* Set nonce to zero to avoid nonce reuse. This will cause subsequent calls
     * of this function to fail */
    memset(secnonce, 0, sizeof(*secnonce));
    if (!ret) {
        rustsecp256k1zkp_v0_8_1_musig_partial_sign_clear(&sk, k);
        return 0;
    }

    ARG_CHECK(partial_sig != NULL);
    ARG_CHECK(keypair != NULL);
    ARG_CHECK(session != NULL);

    if (!rustsecp256k1zkp_v0_8_1_keypair_load(ctx, &sk, &keypair_pk, keypair)) {
        rustsecp256k1zkp_v0_8_1_musig_partial_sign_clear(&sk, k);
        return 0;
    }
    ARG_CHECK(rustsecp256k1zkp_v0_8_1_fe_equal_var(&pk.x, &keypair_pk.x)
              && rustsecp256k1zkp_v0_8_1_fe_equal_var(&pk.y, &keypair_pk.y));

    rustsecp256k1zkp_v0_8_1_fe_normalize_var(&pk.y);

    /* Negate sk if rustsecp256k1zkp_v0_8_1_fe_is_odd(&cache_i.pk.y)) XOR cache_i.parity_acc.
     * This corresponds to the line "Let d = g⋅gacc⋅d' mod n" in the
     * specification. */
    if (negate_seckey) {
        rustsecp256k1zkp_v0_8_1_scalar_negate(&sk, &sk);
    }

    /* Multiply KeyAgg coefficient */
    rustsecp256k1zkp_v0_8_1_fe_normalize_var(&pk.x);

    rustsecp256k1zkp_v0_8_1_scalar_set_b32(&mu, keyaggcoef, NULL);
    rustsecp256k1zkp_v0_8_1_scalar_mul(&sk, &sk, &mu);

    if (!rustsecp256k1zkp_v0_8_1_musig_session_load(ctx, &session_i, session)) {
        rustsecp256k1zkp_v0_8_1_musig_partial_sign_clear(&sk, k);
        return 0;
    }

    if (session_i.fin_nonce_parity) {
        rustsecp256k1zkp_v0_8_1_scalar_negate(&k[0], &k[0]);
        rustsecp256k1zkp_v0_8_1_scalar_negate(&k[1], &k[1]);
    }

    /* Sign */
    rustsecp256k1zkp_v0_8_1_scalar_mul(&s, &session_i.challenge, &sk);
    rustsecp256k1zkp_v0_8_1_scalar_mul(&k[1], &session_i.noncecoef, &k[1]);
    rustsecp256k1zkp_v0_8_1_scalar_add(&k[0], &k[0], &k[1]);
    rustsecp256k1zkp_v0_8_1_scalar_add(&s, &s, &k[0]);
    rustsecp256k1zkp_v0_8_1_musig_partial_sig_save(partial_sig, &s);
    rustsecp256k1zkp_v0_8_1_musig_partial_sign_clear(&sk, k);
    return 1;
}

SECP256K1_API int rustsecp256k1zkp_v0_8_1_blinded_musig_partial_sign_without_keyaggcoeff(
    const rustsecp256k1zkp_v0_8_1_context *ctx,
    rustsecp256k1zkp_v0_8_1_musig_partial_sig *partial_sig,
    rustsecp256k1zkp_v0_8_1_musig_secnonce *secnonce,
    const rustsecp256k1zkp_v0_8_1_keypair *keypair,
    const rustsecp256k1zkp_v0_8_1_musig_session *session,
    const int negate_seckey
)
{
    rustsecp256k1zkp_v0_8_1_scalar sk;
    rustsecp256k1zkp_v0_8_1_ge pk, keypair_pk;
    rustsecp256k1zkp_v0_8_1_scalar k[2];
    rustsecp256k1zkp_v0_8_1_scalar mu, s;
    rustsecp256k1zkp_v0_8_1_musig_session_internal session_i;
    int ret;

    VERIFY_CHECK(ctx != NULL);

    ARG_CHECK(secnonce != NULL);
    /* Fails if the magic doesn't match */
    ret = rustsecp256k1zkp_v0_8_1_musig_secnonce_load(ctx, k, &pk, secnonce);
    /* Set nonce to zero to avoid nonce reuse. This will cause subsequent calls
     * of this function to fail */
    memset(secnonce, 0, sizeof(*secnonce));
    if (!ret) {
        rustsecp256k1zkp_v0_8_1_musig_partial_sign_clear(&sk, k);
        return 0;
    }

    ARG_CHECK(partial_sig != NULL);
    ARG_CHECK(keypair != NULL);
    ARG_CHECK(session != NULL);

    if (!rustsecp256k1zkp_v0_8_1_keypair_load(ctx, &sk, &keypair_pk, keypair)) {
        rustsecp256k1zkp_v0_8_1_musig_partial_sign_clear(&sk, k);
        return 0;
    }
    ARG_CHECK(rustsecp256k1zkp_v0_8_1_fe_equal_var(&pk.x, &keypair_pk.x)
              && rustsecp256k1zkp_v0_8_1_fe_equal_var(&pk.y, &keypair_pk.y));

    rustsecp256k1zkp_v0_8_1_fe_normalize_var(&pk.y);

    /* Negate sk if rustsecp256k1zkp_v0_8_1_fe_is_odd(&cache_i.pk.y)) XOR cache_i.parity_acc.
     * This corresponds to the line "Let d = g⋅gacc⋅d' mod n" in the
     * specification. */
    if (negate_seckey) {
        rustsecp256k1zkp_v0_8_1_scalar_negate(&sk, &sk);
    }

    /* Multiply KeyAgg coefficient */
    rustsecp256k1zkp_v0_8_1_fe_normalize_var(&pk.x);

    rustsecp256k1zkp_v0_8_1_scalar_set_int(&mu, 1);
    rustsecp256k1zkp_v0_8_1_scalar_mul(&sk, &sk, &mu);

    if (!rustsecp256k1zkp_v0_8_1_musig_session_load(ctx, &session_i, session)) {
        rustsecp256k1zkp_v0_8_1_musig_partial_sign_clear(&sk, k);
        return 0;
    }

    if (session_i.fin_nonce_parity) {
        rustsecp256k1zkp_v0_8_1_scalar_negate(&k[0], &k[0]);
        rustsecp256k1zkp_v0_8_1_scalar_negate(&k[1], &k[1]);
    }

    /* Sign */
    rustsecp256k1zkp_v0_8_1_scalar_mul(&s, &session_i.challenge, &sk);
    rustsecp256k1zkp_v0_8_1_scalar_mul(&k[1], &session_i.noncecoef, &k[1]);
    rustsecp256k1zkp_v0_8_1_scalar_add(&k[0], &k[0], &k[1]);
    rustsecp256k1zkp_v0_8_1_scalar_add(&s, &s, &k[0]);
    rustsecp256k1zkp_v0_8_1_musig_partial_sig_save(partial_sig, &s);
    rustsecp256k1zkp_v0_8_1_musig_partial_sign_clear(&sk, k);
    return 1;
}

int rustsecp256k1zkp_v0_8_1_musig_partial_sig_verify(const rustsecp256k1zkp_v0_8_1_context* ctx, const rustsecp256k1zkp_v0_8_1_musig_partial_sig *partial_sig, const rustsecp256k1zkp_v0_8_1_musig_pubnonce *pubnonce, const rustsecp256k1zkp_v0_8_1_pubkey *pubkey, const rustsecp256k1zkp_v0_8_1_musig_keyagg_cache *keyagg_cache, const rustsecp256k1zkp_v0_8_1_musig_session *session) {
    rustsecp256k1zkp_v0_8_1_keyagg_cache_internal cache_i;
    rustsecp256k1zkp_v0_8_1_musig_session_internal session_i;
    rustsecp256k1zkp_v0_8_1_scalar mu, e, s;
    rustsecp256k1zkp_v0_8_1_gej pkj;
    rustsecp256k1zkp_v0_8_1_ge nonce_pt[2];
    rustsecp256k1zkp_v0_8_1_gej rj;
    rustsecp256k1zkp_v0_8_1_gej tmp;
    rustsecp256k1zkp_v0_8_1_ge pkp;

    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(partial_sig != NULL);
    ARG_CHECK(pubnonce != NULL);
    ARG_CHECK(pubkey != NULL);
    ARG_CHECK(keyagg_cache != NULL);
    ARG_CHECK(session != NULL);

    if (!rustsecp256k1zkp_v0_8_1_musig_session_load(ctx, &session_i, session)) {
        return 0;
    }

    /* Compute "effective" nonce rj = aggnonce[0] + b*aggnonce[1] */
    /* TODO: use multiexp to compute -s*G + e*mu*pubkey + aggnonce[0] + b*aggnonce[1] */
    if (!rustsecp256k1zkp_v0_8_1_musig_pubnonce_load(ctx, nonce_pt, pubnonce)) {
        return 0;
    }
    rustsecp256k1zkp_v0_8_1_gej_set_ge(&rj, &nonce_pt[1]);
    rustsecp256k1zkp_v0_8_1_ecmult(&rj, &rj, &session_i.noncecoef, NULL);
    rustsecp256k1zkp_v0_8_1_gej_add_ge_var(&rj, &rj, &nonce_pt[0], NULL);

    if (!rustsecp256k1zkp_v0_8_1_pubkey_load(ctx, &pkp, pubkey)) {
        return 0;
    }
    if (!rustsecp256k1zkp_v0_8_1_keyagg_cache_load(ctx, &cache_i, keyagg_cache)) {
        return 0;
    }
    /* Multiplying the challenge by the KeyAgg coefficient is equivalent
     * to multiplying the signer's public key by the coefficient, except
     * much easier to do. */
    rustsecp256k1zkp_v0_8_1_musig_keyaggcoef(&mu, &cache_i, &pkp);
    rustsecp256k1zkp_v0_8_1_scalar_mul(&e, &session_i.challenge, &mu);

    /* Negate e if rustsecp256k1zkp_v0_8_1_fe_is_odd(&cache_i.pk.y)) XOR cache_i.parity_acc.
     * This corresponds to the line "Let g' = g⋅gacc mod n" and the multiplication "g'⋅e"
     * in the specification. */
    if (rustsecp256k1zkp_v0_8_1_fe_is_odd(&cache_i.pk.y)
            != cache_i.parity_acc) {
        rustsecp256k1zkp_v0_8_1_scalar_negate(&e, &e);
    }

    if (!rustsecp256k1zkp_v0_8_1_musig_partial_sig_load(ctx, &s, partial_sig)) {
        return 0;
    }
    /* Compute -s*G + e*pkj + rj (e already includes the keyagg coefficient mu) */
    rustsecp256k1zkp_v0_8_1_scalar_negate(&s, &s);
    rustsecp256k1zkp_v0_8_1_gej_set_ge(&pkj, &pkp);
    rustsecp256k1zkp_v0_8_1_ecmult(&tmp, &pkj, &e, &s);
    if (session_i.fin_nonce_parity) {
        rustsecp256k1zkp_v0_8_1_gej_neg(&rj, &rj);
    }
    rustsecp256k1zkp_v0_8_1_gej_add_var(&tmp, &tmp, &rj, NULL);

    return rustsecp256k1zkp_v0_8_1_gej_is_infinity(&tmp);
}

int rustsecp256k1zkp_v0_8_1_blinded_musig_partial_sig_verify(
    const rustsecp256k1zkp_v0_8_1_context* ctx, 
    const rustsecp256k1zkp_v0_8_1_musig_partial_sig *partial_sig, 
    const rustsecp256k1zkp_v0_8_1_musig_pubnonce *pubnonce, 
    const rustsecp256k1zkp_v0_8_1_pubkey *pubkey, 
    const rustsecp256k1zkp_v0_8_1_pubkey *aggregate_pubkey, 
    const rustsecp256k1zkp_v0_8_1_musig_session *session,
    const int parity_acc
) {
    rustsecp256k1zkp_v0_8_1_musig_session_internal session_i;
    rustsecp256k1zkp_v0_8_1_scalar mu, e, s;
    rustsecp256k1zkp_v0_8_1_gej pkj;
    rustsecp256k1zkp_v0_8_1_ge nonce_pt[2];
    rustsecp256k1zkp_v0_8_1_gej rj;
    rustsecp256k1zkp_v0_8_1_gej tmp;
    rustsecp256k1zkp_v0_8_1_ge pkp;
    rustsecp256k1zkp_v0_8_1_ge aggregate_pubkey_ge;

    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(partial_sig != NULL);
    ARG_CHECK(pubnonce != NULL);
    ARG_CHECK(pubkey != NULL);
    ARG_CHECK(session != NULL);

    if (!rustsecp256k1zkp_v0_8_1_musig_session_load(ctx, &session_i, session)) {
        return 0;
    }

    /* convert aggregate_pubkey to rustsecp256k1zkp_v0_8_1_ge*/
    if (!rustsecp256k1zkp_v0_8_1_pubkey_load(ctx, &aggregate_pubkey_ge, aggregate_pubkey)) {
        return 0;
    }

    /* Compute "effective" nonce rj = aggnonce[0] + b*aggnonce[1] */
    /* TODO: use multiexp to compute -s*G + e*mu*pubkey + aggnonce[0] + b*aggnonce[1] */
    if (!rustsecp256k1zkp_v0_8_1_musig_pubnonce_load(ctx, nonce_pt, pubnonce)) {
        return 0;
    }
    rustsecp256k1zkp_v0_8_1_gej_set_ge(&rj, &nonce_pt[1]);
    rustsecp256k1zkp_v0_8_1_ecmult(&rj, &rj, &session_i.noncecoef, NULL);
    rustsecp256k1zkp_v0_8_1_gej_add_ge_var(&rj, &rj, &nonce_pt[0], NULL);

    if (!rustsecp256k1zkp_v0_8_1_pubkey_load(ctx, &pkp, pubkey)) {
        return 0;
    }
    /* Multiplying the challenge by the KeyAgg coefficient is equivalent
     * to multiplying the signer's public key by the coefficient, except
     * much easier to do. */
    rustsecp256k1zkp_v0_8_1_scalar_set_int(&mu, 1);
    rustsecp256k1zkp_v0_8_1_scalar_mul(&e, &session_i.challenge, &mu);

    /* Negate e if rustsecp256k1zkp_v0_8_1_fe_is_odd(&cache_i.pk.y)) XOR cache_i.parity_acc.
     * This corresponds to the line "Let g' = g⋅gacc mod n" and the multiplication "g'⋅e"
     * in the specification. */
    /* if (rustsecp256k1zkp_v0_8_1_fe_is_odd(&cache_i.pk.y)
            != cache_i.parity_acc) {
        rustsecp256k1zkp_v0_8_1_scalar_negate(&e, &e);
    }*/ 
    if (rustsecp256k1zkp_v0_8_1_fe_is_odd(&aggregate_pubkey_ge.y) != parity_acc) {
        rustsecp256k1zkp_v0_8_1_scalar_negate(&e, &e);
    }

    if (!rustsecp256k1zkp_v0_8_1_musig_partial_sig_load(ctx, &s, partial_sig)) {
        return 0;
    }
    /* Compute -s*G + e*pkj + rj (e already includes the keyagg coefficient mu) */
    rustsecp256k1zkp_v0_8_1_scalar_negate(&s, &s);
    rustsecp256k1zkp_v0_8_1_gej_set_ge(&pkj, &pkp);
    rustsecp256k1zkp_v0_8_1_ecmult(&tmp, &pkj, &e, &s);
    if (session_i.fin_nonce_parity) {
        rustsecp256k1zkp_v0_8_1_gej_neg(&rj, &rj);
    }
    rustsecp256k1zkp_v0_8_1_gej_add_var(&tmp, &tmp, &rj, NULL);

    return rustsecp256k1zkp_v0_8_1_gej_is_infinity(&tmp);
}

int rustsecp256k1zkp_v0_8_1_musig_partial_sig_agg(const rustsecp256k1zkp_v0_8_1_context* ctx, unsigned char *sig64, const rustsecp256k1zkp_v0_8_1_musig_session *session, const rustsecp256k1zkp_v0_8_1_musig_partial_sig * const* partial_sigs, size_t n_sigs) {
    size_t i;
    rustsecp256k1zkp_v0_8_1_musig_session_internal session_i;

    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(sig64 != NULL);
    ARG_CHECK(session != NULL);
    ARG_CHECK(partial_sigs != NULL);
    ARG_CHECK(n_sigs > 0);

    if (!rustsecp256k1zkp_v0_8_1_musig_session_load(ctx, &session_i, session)) {
        return 0;
    }
    for (i = 0; i < n_sigs; i++) {
        rustsecp256k1zkp_v0_8_1_scalar term;
        if (!rustsecp256k1zkp_v0_8_1_musig_partial_sig_load(ctx, &term, partial_sigs[i])) {
            return 0;
        }
        rustsecp256k1zkp_v0_8_1_scalar_add(&session_i.s_part, &session_i.s_part, &term);
    }
    rustsecp256k1zkp_v0_8_1_scalar_get_b32(&sig64[32], &session_i.s_part);
    memcpy(&sig64[0], session_i.fin_nonce, 32);
    return 1;
}

#endif
