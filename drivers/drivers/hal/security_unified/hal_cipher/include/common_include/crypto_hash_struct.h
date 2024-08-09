/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
 *
 * Description: crypto hash struct header. \n
 *
 * History: \n
 * 2023-03-22, Create file. \n
 */
#ifndef CRYPTO_HASH_STRUCT_H
#define CRYPTO_HASH_STRUCT_H

#include "crypto_common_struct.h"

/*
 * Component of crypto_hash_type.
 * is_hmac(4 bits) | alg(4 bits) | mode(4 bits) | max_message_length(4 bits) | block_size(4 bits) | result_size(12 bits)
 * is_hmac: h'0 - hash
 *          h'1 - hmac
 * alg:     h'0 - sha1
 *          h'1 - sha2
 *          h'2 - sm3
 * mode:    h'0 - 224
 *          h'1 - 256
 *          h'2 - 384
 *          h'3 - 512
 * max_message_length:
 *          h'6 - 2**6 -> 64, 最大消息 2**64 Bits. For SHA256.
 *          h'7 - 2**7 -> 128, 最大消息 2**128 Bits. For SHA384/SHA512.
 * block_size:
 *          h'9 - 2**9 -> 512, Block Size 为 512 Bits. For SHA256.
 *          h'a - 2**10 -> 1024, Block Size 为 1024 Bits. For SHA384/SHA512.
 * result_size:
 *          h'100 - 256, Result Size 为 256 Bits. For SHA256.
 *          h'180 - 384, Result Size 为 384 Bits. For SHA384.
 *          h'200 - 512, Result Size 为 512 Bits. For SHA512.
 */
#define compat_hash_type(is_hmac, alg, mode, max_message_length, block_size, result_size)          \
    ((((is_hmac) & 0xF) << 28) | (((alg) & 0xF) << 24) | (((mode) & 0xF) << 20) |                  \
    (((max_message_length) & 0xF) << 16) | (((block_size) & 0xF) << 12) | ((result_size) & 0xFFF))

#define CRYPTO_HASH_TYPE        0
#define CRYPTO_HMAC_TYPE        1
#define CRYPTO_IS_HMAC_MASK     0xF0000000
#define CRYPTO_IS_HMAC_SHIFT    28

#define CRYPTO_HASH_ALG_SHA1    0
#define CRYPTO_HASH_ALG_SHA2    1
#define CRYPTO_HASH_ALG_SM3     2
#define CRYPTO_HASH_ALG_MASK    0x0F000000
#define CRYPTO_HASH_ALG_SHIFT   24

#define CRYPTO_HASH_MODE_224    0
#define CRYPTO_HASH_MODE_256    1
#define CRYPTO_HASH_MODE_384    2
#define CRYPTO_HASH_MODE_512    3
#define CRYPTO_HASH_MODE_UNDEF  0xf
#define CRYPTO_HASH_MODE_MASK   0x00F00000
#define CRYPTO_HASH_MODE_SHIFT  20

#define CRYPTO_HASH_MAX_MESSAGE_LEN_64BIT   0x6
#define CRYPTO_HASH_MAX_MESSAGE_LEN_128BIT  0x7
#define CRYPTO_HASH_MAX_MESSAGE_LEN_MASK    0x000F0000
#define CRYPTO_HASH_MAX_MESSAGE_LEN_SHIFT   16

#define CRYPTO_HASH_BLOCK_SIZE_512BIT       0x9
#define CRYPTO_HASH_BLOCK_SIZE_1024BIT      0xa
#define CRYPTO_HASH_BLOCK_SIZE_MASK         0x0000F000
#define CRYPTO_HASH_BLOCK_SIZE_SHIFT        12

#define CRYPTO_HASH_RESULT_SIZE_160BIT      0xa0
#define CRYPTO_HASH_RESULT_SIZE_224BIT      0xe0
#define CRYPTO_HASH_RESULT_SIZE_256BIT      0x100
#define CRYPTO_HASH_RESULT_SIZE_384BIT      0x180
#define CRYPTO_HASH_RESULT_SIZE_512BIT      0x200
#define CRYPTO_HASH_RESULT_SIZE_MASK        0x00000FFF
#define CRYPTO_HASH_RESULT_SIZE_SHIFT       0

#define CRYPTO_HASH_BLOCK_SIZE_20BYTE       0x14
#define CRYPTO_HASH_BLOCK_SIZE_32BYTE       0x20
#define CRYPTO_HASH_BLOCK_SIZE_64BYTE       0x40

#define crypto_hash_get_attr(value, mask, shift)         (((td_u32)(value) & (td_u32)(mask)) >> (shift))
#define crypto_hash_macth(value, mask, target, shift)    (crypto_hash_get_attr(value, mask, shift) == (target))

#define crypto_hash_get_alg(hash_type)              \
    crypto_hash_get_attr(hash_type, CRYPTO_HASH_ALG_MASK, CRYPTO_HASH_ALG_SHIFT)
#define crypto_hash_get_mode(hash_type)             \
    crypto_hash_get_attr(hash_type, CRYPTO_HASH_MODE_MASK, CRYPTO_HASH_MODE_SHIFT)
#define crypto_hash_is_hmac(hash_type)              \
    crypto_hash_macth(hash_type, CRYPTO_IS_HMAC_MASK, CRYPTO_HMAC_TYPE, CRYPTO_IS_HMAC_SHIFT)
#define crypto_hash_get_message_len(hash_type)      \
    (1 << crypto_hash_get_attr(hash_type, CRYPTO_HASH_MAX_MESSAGE_LEN_MASK, CRYPTO_HASH_MAX_MESSAGE_LEN_SHIFT))
#define crypto_hash_get_block_size(hash_type)       \
    (1 << crypto_hash_get_attr(hash_type, CRYPTO_HASH_BLOCK_SIZE_MASK, CRYPTO_HASH_BLOCK_SIZE_SHIFT))
#define crypto_hash_get_result_size(hash_type)      \
    crypto_hash_get_attr(hash_type, CRYPTO_HASH_RESULT_SIZE_MASK, CRYPTO_HASH_RESULT_SIZE_SHIFT)

typedef enum {
    /* Hash. */
    CRYPTO_HASH_TYPE_SHA1 = compat_hash_type(
        CRYPTO_HASH_TYPE, CRYPTO_HASH_ALG_SHA1, CRYPTO_HASH_MODE_UNDEF, CRYPTO_HASH_MAX_MESSAGE_LEN_64BIT,
        CRYPTO_HASH_BLOCK_SIZE_512BIT, CRYPTO_HASH_RESULT_SIZE_160BIT
    ),
    CRYPTO_HASH_TYPE_SHA224 = compat_hash_type(
        CRYPTO_HASH_TYPE, CRYPTO_HASH_ALG_SHA2, CRYPTO_HASH_MODE_224, CRYPTO_HASH_MAX_MESSAGE_LEN_64BIT,
        CRYPTO_HASH_BLOCK_SIZE_512BIT, CRYPTO_HASH_RESULT_SIZE_224BIT
    ),
    CRYPTO_HASH_TYPE_SHA256 = compat_hash_type(
        CRYPTO_HASH_TYPE, CRYPTO_HASH_ALG_SHA2, CRYPTO_HASH_MODE_256, CRYPTO_HASH_MAX_MESSAGE_LEN_64BIT,
        CRYPTO_HASH_BLOCK_SIZE_512BIT, CRYPTO_HASH_RESULT_SIZE_256BIT
    ),
    CRYPTO_HASH_TYPE_SHA384 = compat_hash_type(
        CRYPTO_HASH_TYPE, CRYPTO_HASH_ALG_SHA2, CRYPTO_HASH_MODE_384, CRYPTO_HASH_MAX_MESSAGE_LEN_128BIT,
        CRYPTO_HASH_BLOCK_SIZE_1024BIT, CRYPTO_HASH_RESULT_SIZE_384BIT
    ),
    CRYPTO_HASH_TYPE_SHA512 = compat_hash_type(
        CRYPTO_HASH_TYPE, CRYPTO_HASH_ALG_SHA2, CRYPTO_HASH_MODE_512, CRYPTO_HASH_MAX_MESSAGE_LEN_128BIT,
        CRYPTO_HASH_BLOCK_SIZE_1024BIT, CRYPTO_HASH_RESULT_SIZE_512BIT
    ),
    CRYPTO_HASH_TYPE_SM3 = compat_hash_type(
        CRYPTO_HASH_TYPE, CRYPTO_HASH_ALG_SM3, CRYPTO_HASH_MODE_256, CRYPTO_HASH_MAX_MESSAGE_LEN_64BIT,
        CRYPTO_HASH_BLOCK_SIZE_512BIT, CRYPTO_HASH_RESULT_SIZE_256BIT
    ),

    /* HMAC. */
    CRYPTO_HASH_TYPE_HMAC_SHA1 = compat_hash_type(
        CRYPTO_HMAC_TYPE, CRYPTO_HASH_ALG_SHA1, CRYPTO_HASH_MODE_UNDEF, CRYPTO_HASH_MAX_MESSAGE_LEN_64BIT,
        CRYPTO_HASH_BLOCK_SIZE_512BIT, CRYPTO_HASH_RESULT_SIZE_160BIT
    ),
    CRYPTO_HASH_TYPE_HMAC_SHA224 = compat_hash_type(
        CRYPTO_HMAC_TYPE, CRYPTO_HASH_ALG_SHA2, CRYPTO_HASH_MODE_224, CRYPTO_HASH_MAX_MESSAGE_LEN_64BIT,
        CRYPTO_HASH_BLOCK_SIZE_512BIT, CRYPTO_HASH_RESULT_SIZE_224BIT
    ),
    CRYPTO_HASH_TYPE_HMAC_SHA256 = compat_hash_type(
        CRYPTO_HMAC_TYPE, CRYPTO_HASH_ALG_SHA2, CRYPTO_HASH_MODE_256, CRYPTO_HASH_MAX_MESSAGE_LEN_64BIT,
        CRYPTO_HASH_BLOCK_SIZE_512BIT, CRYPTO_HASH_RESULT_SIZE_256BIT
    ),
    CRYPTO_HASH_TYPE_HMAC_SHA384 = compat_hash_type(
        CRYPTO_HMAC_TYPE, CRYPTO_HASH_ALG_SHA2, CRYPTO_HASH_MODE_384, CRYPTO_HASH_MAX_MESSAGE_LEN_128BIT,
        CRYPTO_HASH_BLOCK_SIZE_1024BIT, CRYPTO_HASH_RESULT_SIZE_384BIT
    ),
    CRYPTO_HASH_TYPE_HMAC_SHA512 = compat_hash_type(
        CRYPTO_HMAC_TYPE, CRYPTO_HASH_ALG_SHA2, CRYPTO_HASH_MODE_512, CRYPTO_HASH_MAX_MESSAGE_LEN_128BIT,
        CRYPTO_HASH_BLOCK_SIZE_1024BIT, CRYPTO_HASH_RESULT_SIZE_512BIT
    ),
    CRYPTO_HASH_TYPE_HMAC_SM3 = compat_hash_type(
        CRYPTO_HMAC_TYPE, CRYPTO_HASH_ALG_SM3, CRYPTO_HASH_MODE_256, CRYPTO_HASH_MAX_MESSAGE_LEN_64BIT,
        CRYPTO_HASH_BLOCK_SIZE_512BIT, CRYPTO_HASH_RESULT_SIZE_256BIT
    ),

    CRYPTO_HASH_TYPE_INVALID = 0xffffffff,
} crypto_hash_type;

typedef struct {
    td_u8 *key;
    td_u32 key_len;
    td_handle drv_keyslot_handle;
    crypto_hash_type hash_type;
    td_bool is_keyslot;
    td_bool is_long_term;
} crypto_hash_attr;

/* Structure for HASH. */
#define CRYPTO_HASH_RESULT_SIZE_MAX 64              // for SHA-512
#define CRYPTO_HASH_RESULT_SIZE_MAX_IN_WORD 16      // for SHA-512
#define CRYPTO_HASH_BLOCK_SIZE_MAX 128              // for SHA-512

typedef struct {
    td_u32 length[2];
    td_u32 state[CRYPTO_HASH_RESULT_SIZE_MAX_IN_WORD];
    td_u32 tail_len;
    crypto_hash_type hash_type;
#if defined(CRYPTO_SOFT_HMAC_SUPPORT)
    td_u8 o_key_pad[CRYPTO_HASH_BLOCK_SIZE_MAX];
    td_u8 i_key_pad[CRYPTO_HASH_BLOCK_SIZE_MAX];
#endif
    td_u8 tail[CRYPTO_HASH_BLOCK_SIZE_MAX];
} crypto_hash_clone_ctx;

typedef struct {
    crypto_hash_type hmac_type;
    td_u8 *salt;
    td_u32 salt_length;
    td_u8 *ikm;
    td_u32 ikm_length;
} crypto_hkdf_extract_t;

typedef struct {
    crypto_hash_type hmac_type;
    td_u8 *prk;
    td_u32 prk_length;
    td_u8 *info;
    td_u32 info_length;
} crypto_hkdf_expand_t;

typedef struct {
    crypto_hash_type hmac_type;
    td_u8 *salt;
    td_u32 salt_length;
    td_u8 *ikm;
    td_u32 ikm_length;
    td_u8 *info;
    td_u32 info_length;
} crypto_hkdf_t;

#endif
