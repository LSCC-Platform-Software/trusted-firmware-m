/*
 * Copyright (c) 2024, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef __PLATFORM_ERROR_CODES_H__
#define __PLATFORM_ERROR_CODES_H__

#include <limits.h>

#if TFM_UNIQUE_ERROR_CODES == 1
#include "error_codes_mapping.h"
#else
#define TFM_PLAT_ERROR_BASE 0x1u
#endif /* TFM_UNIQUE_ERROR_CODES */

enum tfm_plat_err_t {
    TFM_PLAT_ERR_SUCCESS = 0,
    /* RSE OTP LCM error codes */
    TFM_PLAT_ERR_OTP_EMULATION_UNSUPPORTED = TFM_PLAT_ERROR_BASE,
    TFM_PLAT_ERR_OTP_READ_UNSUPPORTED,
    TFM_PLAT_ERR_OTP_READ_ENCRYPTED_UNSUPPORTED,
    TFM_PLAT_ERR_OTP_READ_ENCRYPTED_INVALID_INPUT,
    TFM_PLAT_ERR_OTP_WRITE_INVALID_INPUT,
    TFM_PLAT_ERR_OTP_WRITE_ENCRYPTED_INVALID_INPUT,
    TFM_PLAT_ERR_OTP_READ_LCS_INVALID_INPUT,
    TFM_PLAT_ERR_OTP_WRITE_LCS_INVALID_INPUT,
    TFM_PLAT_ERR_OTP_WRITE_LCS_SYSTEM_ERR,
    TFM_PLAT_ERR_OTP_INIT_SYSTEM_ERR,
    TFM_PLAT_ERR_PLAT_OTP_READ_INVALID_INPUT,
    TFM_PLAT_ERR_PLAT_OTP_READ_UNSUPPORTED,
    TFM_PLAT_ERR_PLAT_OTP_READ_MFG_DATA_UNSUPPORTED,
    TFM_PLAT_ERR_PLAT_OTP_WRITE_INVALID_INPUT,
    TFM_PLAT_ERR_PLAT_OTP_WRITE_UNSUPPORTED,
    TFM_PLAT_ERR_PLAT_OTP_GET_SIZE_INVALID_INPUT,
    TFM_PLAT_ERR_PLAT_OTP_GET_SIZE_UNSUPPORTED,
    /* RSE BL1 provisioning error codes */
    TFM_PLAT_ERR_BL1_PROVISIONING_INVALID_TP_MODE,
    /* RSE key derivation error codes */
    TFM_PLAT_ERR_KEY_DERIVATION_BOOT_STATE_BUFFER_TOO_SMALL,
    TFM_PLAT_ERR_KEY_DERIVATION_INVALID_TP_MODE,
    /* RSE NV counters */
    TFM_PLAT_ERR_READ_OTP_COUNTER_SYSTEM_ERR,
    TFM_PLAT_ERR_READ_NV_COUNTER_INVALID_COUNTER_SIZE,
    TFM_PLAT_ERR_READ_NV_COUNTER_UNSUPPORTED,
    TFM_PLAT_ERR_SET_OTP_COUNTER_MAX_VALUE,
    TFM_PLAT_ERR_SET_NV_COUNTER_UNSUPPORTED,
    /* RSE GPT parser error codes */
    TFM_PLAT_ERR_GPT_STRNCMP_INVALID_INPUT,
    TFM_PLAT_ERR_GPT_STRNCMP_COMPARISON_FAILED,
    TFM_PLAT_ERR_GPT_HEADER_INVALID_SIZE,
    TFM_PLAT_ERR_GPT_HEADER_INVALID_READ,
    TFM_PLAT_ERR_GPT_HEADER_INVALID_SIGNATURE,
    TFM_PLAT_ERR_GPT_ENTRY_INVALID_SIZE,
    TFM_PLAT_ERR_GPT_ENTRY_OVERFLOW,
    TFM_PLAT_ERR_GPT_ENTRY_INVALID_READ,
    TFM_PLAT_ERR_GPT_ENTRY_NOT_FOUND,
    /* RSE FIP parser error codes */
    TFM_PLAT_ERR_FIP_TOC_HEADER_INVALID_READ,
    TFM_PLAT_ERR_FIP_TOC_HEADER_INVALID_NAME,
    TFM_PLAT_ERR_FIP_TOC_ENTRY_OVERFLOW,
    TFM_PLAT_ERR_FIP_TOC_ENTRY_INVALID_READ,
    TFM_PLAT_ERR_FIP_TOC_ENTRY_INVALID_SIZE,
    TFM_PLAT_ERR_GPT_TOC_ENTRY_NOT_FOUND,
    /* RSE Host Flash ATU error codes */
    TFM_PLAT_ERR_HOST_FLASH_SETUP_ATU_SLOT_INVALID_INPUT,
    TFM_PLAT_ERR_HOST_FLASH_SETUP_IMAGE_SLOT_NO_FIP_FOUND,
    TFM_PLAT_ERR_HOST_FLASH_SETUP_IMAGE_SLOT_NO_FIP_MAPPED,
    /* RSE handshake error codes */
    TFM_PLAT_ERR_RSE_HANDSHAKE_CLIENT_SESSION_INVALID_HEADER,
    TFM_PLAT_ERR_RSE_HANDSHAKE_CLIENT_VHUK_INVALID_HEADER,
    TFM_PLAT_ERR_RSE_HANDSHAKE_SERVER_SESSION_INVALID_HEADER,
    TFM_PLAT_ERR_RSE_HANDSHAKE_SERVER_VHUK_INVALID_HEADER,
    /* RSE ROM crypto error codes */
    TFM_PLAT_ERR_ROM_CRYPTO_SHA256_COMPUTE_INVALID_INPUT,
    TFM_PLAT_ERR_ROM_CRYPTO_AES256_CTR_DECRYPT_INVALID_INPUT,
    TFM_PLAT_ERR_ROM_CRYPTO_AES256_CTR_DECRYPT_INVALID_ALIGNMENT,
    /* RSE ROM trng error codes */
    TFM_PLAT_ERR_ROM_TRNG_GENERATE_RANDOM_INVALID_INPUT,
    /* RSE BL1_2 read image error codes */
    TFM_PLAT_ERR_READ_BL1_2_IMAGE_FLASH_INVALID_READ,
    /* RSE BL2 error codes */
    TFM_PLAT_ERR_PRE_LOAD_IMG_BY_BL2_FAIL,
    TFM_PLAT_ERR_POST_LOAD_IMG_BY_BL2_FAIL,
    /* Generic errors */
    TFM_PLAT_ERR_SYSTEM_ERR,
    TFM_PLAT_ERR_MAX_VALUE,
    TFM_PLAT_ERR_INVALID_INPUT,
    TFM_PLAT_ERR_UNSUPPORTED,
    TFM_PLAT_ERR_NOT_PERMITTED,
    /* Following entry is only to ensure the error code of int size */
    TFM_PLAT_ERR_FORCE_UINT_SIZE = UINT_MAX
};

#endif /* __PLATFORM_ERROR_CODES_H__ */
