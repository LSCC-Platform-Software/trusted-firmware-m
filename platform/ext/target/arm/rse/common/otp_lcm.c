/*
 * Copyright (c) 2021-2024, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "tfm_plat_otp.h"

#include "region_defs.h"
#include "cmsis_compiler.h"
#include "device_definition.h"
#include "lcm_drv.h"
#include "lcm_otp_layout.h"
#include "tfm_hal_device_header.h"
#include "uart_stdout.h"
#include "tfm_hal_platform.h"
#include "rse_memory_sizes.h"
#ifdef RSE_ENCRYPTED_OTP_KEYS
#include "cc3xx_drv.h"
#include "kmu_drv.h"
#endif /* RSE_ENCRYPTED_OTP_KEYS */
#ifdef RSE_BRINGUP_OTP_EMULATION
#include "rse_otp_emulation.h"
#endif /* RSE_BRINGUP_OTP_EMULATION */

#ifdef MCUBOOT_SIGN_EC384
/* The sizes are in 4 byte words */
#define BL2_ROTPK_HASH_SIZE (12)
#define BL2_ROTPK_SIZE      (25)
#else
#define BL2_ROTPK_HASH_SIZE (8)
#define BL2_ROTPK_SIZE      (17)
#endif /* MCUBOOT_SIGN_EC384 */

#ifdef MCUBOOT_BUILTIN_KEY
#define PROV_ROTPK_DATA_SIZE    BL2_ROTPK_SIZE
#else
#define PROV_ROTPK_DATA_SIZE    BL2_ROTPK_HASH_SIZE
#endif /* MCUBOOT_BUILTIN_KEY */


#define OTP_OFFSET(x)        (offsetof(struct lcm_otp_layout_t, x))
#define OTP_SIZE(x)          (sizeof(((struct lcm_otp_layout_t *)0)->x))
#define USER_AREA_OFFSET(x)  (OTP_OFFSET(user_data) + \
                              offsetof(struct plat_user_area_layout_t, x))
#define USER_AREA_SIZE(x)    (sizeof(((struct plat_user_area_layout_t *)0)->x))

#define OTP_ADDRESS(x)       ((LCM_BASE_S) + 0x1000 + OTP_OFFSET(x))
#define USER_AREA_ADDRESS(x) ((void *)((LCM_BASE_S) + 0x1000 + USER_AREA_OFFSET(x)))

#define OTP_ROM_ENCRYPTION_KEY KMU_HW_SLOT_KCE_CM
#define OTP_RUNTIME_ENCRYPTION_KEY KMU_HW_SLOT_KCE_DM

#ifndef RSE_HAS_MANUFACTURING_DATA
#undef OTP_MANUFACTURING_DATA_MAX_SIZE
#define OTP_MANUFACTURING_DATA_MAX_SIZE 0
#endif /* !RSE_HAS_MANUFACTURING_DATA */

__PACKED_STRUCT plat_user_area_layout_t {
    __PACKED_UNION {
        __PACKED_STRUCT {
            uint64_t cm_locked_size;
            uint32_t cm_locked_size_zero_count;
            uint32_t cm_zero_count;

            uint64_t dm_locked_size;
            uint32_t dm_locked_size_zero_count;
            uint32_t dm_zero_count;

            uint32_t attack_tracking_bits[4];

            __PACKED_STRUCT {
                uint32_t bl1_2_image_len;

            /* Things after this point are not touched by BL1_1, and hence are
             * modifiable by new provisioning code.
             */
                uint32_t cca_system_properties;

                uint32_t cm_config_flags;
                uint32_t _pad0;
            } cm_locked;

            __PACKED_STRUCT {
                uint32_t bl1_rotpk_0[14];

                uint32_t bl2_rotpk[MCUBOOT_IMAGE_NUMBER][PROV_ROTPK_DATA_SIZE];

                uint32_t iak_len;
                uint32_t iak_type;
                uint32_t iak_id[8];
                uint32_t implementation_id[8];
                uint32_t verification_service_url[8];
                uint32_t profile_definition[12];

                uint32_t secure_debug_pk[8];

                uint32_t host_rotpk_s[24];
                uint32_t host_rotpk_ns[24];
                uint32_t host_rotpk_cca[24];

                uint32_t dm_config_flags;

                uint32_t rse_id;
#if RSE_AMOUNT > 1
                uint32_t rse_to_rse_sender_routing_table[RSE_AMOUNT];
                uint32_t rse_to_rse_receiver_routing_table[RSE_AMOUNT];
#endif /* RSE_AMOUNT > 1 */

                __PACKED_STRUCT {
                    uint32_t bl2_encryption_key[8];
                    uint32_t s_image_encryption_key[8];
                    uint32_t ns_image_encryption_key[8];
#if LCM_VERSION == 0
                    uint32_t runtime_otp_encryption_key[8];
#endif /* LCM_VERSION == 0 */
                } dm_encrypted;
            } dm_locked;

            __PACKED_STRUCT {
                uint32_t bl1_nv_counter[16];
                uint32_t bl2_nv_counter[MCUBOOT_IMAGE_NUMBER][16];
#ifdef PLATFORM_HAS_PS_NV_OTP_COUNTERS
                uint32_t ps_nv_counter[3][16];
#endif /* PLATFORM_HAS_PS_NV_OTP_COUNTERS */
                uint32_t host_nv_counter[3][16];
                uint32_t reprovisioning_bits;
            } unlocked_area;
        };
        uint8_t _pad0[OTP_TOTAL_SIZE - OTP_DMA_ICS_SIZE - BL1_2_CODE_SIZE
                      - OTP_MANUFACTURING_DATA_MAX_SIZE - sizeof(struct lcm_otp_layout_t)];
    };

    uint32_t bl1_2_image[BL1_2_CODE_SIZE / sizeof(uint32_t)];

#ifdef RSE_HAS_MANUFACTURING_DATA
    __PACKED_STRUCT {
        uint32_t data[(OTP_MANUFACTURING_DATA_MAX_SIZE - 4 * sizeof(uint32_t)) / sizeof(uint32_t)];
        /* Things before this point are not touched by BL1_1, and hence are
         * modifiable by new provisioning code. Things after this point have
         * fixed addresses which are used by BL1_1 and cannot be changed by
         * new provisioning code.
         */
        __PACKED_STRUCT {
            uint32_t _pad0;
            uint32_t size;
            uint32_t _pad1;
            uint32_t zero_count;
        } header;
    } manufacturing_data;
#endif /* RSE_HAS_MANUFACTURING_DATA */

    __PACKED_UNION {
        __PACKED_STRUCT {
            uint32_t crc;
            uint32_t dma_commands[];
        };
        uint8_t _pad2[OTP_DMA_ICS_SIZE];
    } dma_initial_command_sequence;
};

uint32_t * const attack_tracking_bits_ptr = USER_AREA_ADDRESS(attack_tracking_bits);
const uint32_t attack_tracking_bits_word_size = USER_AREA_SIZE(attack_tracking_bits) / sizeof(uint32_t);

static const uint16_t otp_offsets[PLAT_OTP_ID_MAX] = {
    [PLAT_OTP_ID_HUK] = OTP_OFFSET(huk),
    [PLAT_OTP_ID_GUK] = OTP_OFFSET(guk),

    [PLAT_OTP_ID_IAK_LEN] = USER_AREA_OFFSET(dm_locked.iak_len),
    [PLAT_OTP_ID_IAK_TYPE] = USER_AREA_OFFSET(dm_locked.iak_type),
    [PLAT_OTP_ID_IAK_ID] = USER_AREA_OFFSET(dm_locked.iak_id),

    [PLAT_OTP_ID_IMPLEMENTATION_ID] = USER_AREA_OFFSET(dm_locked.implementation_id),
    [PLAT_OTP_ID_VERIFICATION_SERVICE_URL] = USER_AREA_OFFSET(dm_locked.verification_service_url),
    [PLAT_OTP_ID_PROFILE_DEFINITION] = USER_AREA_OFFSET(dm_locked.profile_definition),

    [PLAT_OTP_ID_BL2_ROTPK_0] = USER_AREA_OFFSET(dm_locked.bl2_rotpk[0]),
#if (MCUBOOT_IMAGE_NUMBER > 1)
    [PLAT_OTP_ID_BL2_ROTPK_1] = USER_AREA_OFFSET(dm_locked.bl2_rotpk[1]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 2)
    [PLAT_OTP_ID_BL2_ROTPK_2] = USER_AREA_OFFSET(dm_locked.bl2_rotpk[2]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 3)
    [PLAT_OTP_ID_BL2_ROTPK_3] = USER_AREA_OFFSET(dm_locked.bl2_rotpk[3]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 4)
    [PLAT_OTP_ID_BL2_ROTPK_4] = USER_AREA_OFFSET(dm_locked.bl2_rotpk[4]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 5)
    [PLAT_OTP_ID_BL2_ROTPK_5] = USER_AREA_OFFSET(dm_locked.bl2_rotpk[5]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 6)
    [PLAT_OTP_ID_BL2_ROTPK_6] = USER_AREA_OFFSET(dm_locked.bl2_rotpk[6]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 7)
    [PLAT_OTP_ID_BL2_ROTPK_7] = USER_AREA_OFFSET(dm_locked.bl2_rotpk[7]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 8)
    [PLAT_OTP_ID_BL2_ROTPK_8] = USER_AREA_OFFSET(dm_locked.bl2_rotpk[8]),
#endif

    [PLAT_OTP_ID_NV_COUNTER_BL2_0] = USER_AREA_OFFSET(unlocked_area.bl2_nv_counter[0]),
#if (MCUBOOT_IMAGE_NUMBER > 1)
    [PLAT_OTP_ID_NV_COUNTER_BL2_1] = USER_AREA_OFFSET(unlocked_area.bl2_nv_counter[1]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 2)
    [PLAT_OTP_ID_NV_COUNTER_BL2_2] = USER_AREA_OFFSET(unlocked_area.bl2_nv_counter[2]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 3)
    [PLAT_OTP_ID_NV_COUNTER_BL2_3] = USER_AREA_OFFSET(unlocked_area.bl2_nv_counter[3]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 4)
    [PLAT_OTP_ID_NV_COUNTER_BL2_4] = USER_AREA_OFFSET(unlocked_area.bl2_nv_counter[4]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 5)
    [PLAT_OTP_ID_NV_COUNTER_BL2_5] = USER_AREA_OFFSET(unlocked_area.bl2_nv_counter[5]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 6)
    [PLAT_OTP_ID_NV_COUNTER_BL2_6] = USER_AREA_OFFSET(unlocked_area.bl2_nv_counter[6]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 7)
    [PLAT_OTP_ID_NV_COUNTER_BL2_7] = USER_AREA_OFFSET(unlocked_area.bl2_nv_counter[7]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 8)
    [PLAT_OTP_ID_NV_COUNTER_BL2_8] = USER_AREA_OFFSET(unlocked_area.bl2_nv_counter[8]),
#endif

#ifdef PLATFORM_HAS_PS_NV_OTP_COUNTERS
    [PLAT_OTP_ID_NV_COUNTER_PS_0] = USER_AREA_OFFSET(unlocked_area.ps_nv_counter[0]),
    [PLAT_OTP_ID_NV_COUNTER_PS_1] = USER_AREA_OFFSET(unlocked_area.ps_nv_counter[1]),
    [PLAT_OTP_ID_NV_COUNTER_PS_2] = USER_AREA_OFFSET(unlocked_area.ps_nv_counter[2]),
#endif /* PLATFORM_HAS_PS_NV_OTP_COUNTERS */

    [PLAT_OTP_ID_NV_COUNTER_NS_0] = USER_AREA_OFFSET(unlocked_area.host_nv_counter[0]),
    [PLAT_OTP_ID_NV_COUNTER_NS_1] = USER_AREA_OFFSET(unlocked_area.host_nv_counter[1]),
    [PLAT_OTP_ID_NV_COUNTER_NS_2] = USER_AREA_OFFSET(unlocked_area.host_nv_counter[2]),

    [PLAT_OTP_ID_KEY_BL2_ENCRYPTION] = USER_AREA_OFFSET(dm_locked.dm_encrypted.bl2_encryption_key),
    [PLAT_OTP_ID_KEY_SECURE_ENCRYPTION] = USER_AREA_OFFSET(dm_locked.dm_encrypted.s_image_encryption_key),
    [PLAT_OTP_ID_KEY_NON_SECURE_ENCRYPTION] = USER_AREA_OFFSET(dm_locked.dm_encrypted.ns_image_encryption_key),

    [PLAT_OTP_ID_BL1_2_IMAGE_LEN] = USER_AREA_OFFSET(cm_locked.bl1_2_image_len),
    [PLAT_OTP_ID_BL1_2_IMAGE_HASH] = OTP_OFFSET(rotpk),
    [PLAT_OTP_ID_BL1_ROTPK_0] = USER_AREA_OFFSET(dm_locked.bl1_rotpk_0),

    [PLAT_OTP_ID_NV_COUNTER_BL1_0] = USER_AREA_OFFSET(unlocked_area.bl1_nv_counter),

    [PLAT_OTP_ID_SECURE_DEBUG_PK] = USER_AREA_OFFSET(dm_locked.secure_debug_pk),

    [PLAT_OTP_ID_HOST_ROTPK_S] = USER_AREA_OFFSET(dm_locked.host_rotpk_s),
    [PLAT_OTP_ID_HOST_ROTPK_NS] = USER_AREA_OFFSET(dm_locked.host_rotpk_ns),
    [PLAT_OTP_ID_HOST_ROTPK_CCA] = USER_AREA_OFFSET(dm_locked.host_rotpk_cca),

    [PLAT_OTP_ID_CCA_SYSTEM_PROPERTIES] = USER_AREA_OFFSET(cm_locked.cca_system_properties),

    [PLAT_OTP_ID_REPROVISIONING_BITS] = USER_AREA_OFFSET(unlocked_area.reprovisioning_bits),
    [PLAT_OTP_ID_RSE_ID] = USER_AREA_OFFSET(dm_locked.rse_id),

    [PLAT_OTP_ID_DMA_ICS] = USER_AREA_OFFSET(dma_initial_command_sequence),

#ifdef RSE_HAS_MANUFACTURING_DATA
    [PLAT_OTP_ID_MANUFACTURING_DATA_LEN] = USER_AREA_OFFSET(manufacturing_data.header.size),
#endif /* RSE_HAS_MANUFACTURING_DATA */

    [PLAT_OTP_ID_ROM_OTP_ENCRYPTION_KEY] = OTP_OFFSET(kce_cm),
#if LCM_VERSION == 0
    [PLAT_OTP_ID_RUNTIME_OTP_ENCRYPTION_KEY] = USER_AREA_OFFSET(dm_locked.dm_encrypted.runtime_otp_encryption_key),
#else
    [PLAT_OTP_ID_RUNTIME_OTP_ENCRYPTION_KEY] = OTP_OFFSET(kce_dm),
#endif

    [PLAT_OTP_ID_CM_CONFIG_FLAGS] = USER_AREA_OFFSET(cm_locked.cm_config_flags),
    [PLAT_OTP_ID_DM_CONFIG_FLAGS] = USER_AREA_OFFSET(dm_locked.dm_config_flags),

#if RSE_AMOUNT > 1
    [PLAT_OTP_ID_RSE_TO_RSE_SENDER_ROUTING_TABLE] = USER_AREA_OFFSET(dm_locked.rse_to_rse_sender_routing_table),
    [PLAT_OTP_ID_RSE_TO_RSE_RECEIVER_ROUTING_TABLE] = USER_AREA_OFFSET(dm_locked.rse_to_rse_receiver_routing_table),
#endif /* RSE_AMOUNT > 1 */

    [PLAT_OTP_ID_ATTACK_TRACKING_BITS] = USER_AREA_OFFSET(attack_tracking_bits),
};

static const uint16_t otp_sizes[PLAT_OTP_ID_MAX] = {
    [PLAT_OTP_ID_HUK] = OTP_SIZE(huk),
    [PLAT_OTP_ID_GUK] = OTP_SIZE(guk),

    [PLAT_OTP_ID_LCS] = sizeof(uint32_t),

    [PLAT_OTP_ID_IAK_LEN] = USER_AREA_SIZE(dm_locked.iak_len),
    [PLAT_OTP_ID_IAK_TYPE] = USER_AREA_SIZE(dm_locked.iak_type),
    [PLAT_OTP_ID_IAK_ID] = USER_AREA_SIZE(dm_locked.iak_id),

    [PLAT_OTP_ID_IMPLEMENTATION_ID] = USER_AREA_SIZE(dm_locked.implementation_id),
    [PLAT_OTP_ID_VERIFICATION_SERVICE_URL] = USER_AREA_SIZE(dm_locked.verification_service_url),
    [PLAT_OTP_ID_PROFILE_DEFINITION] = USER_AREA_SIZE(dm_locked.profile_definition),

    [PLAT_OTP_ID_BL2_ROTPK_0] = USER_AREA_SIZE(dm_locked.bl2_rotpk[0]),
#if (MCUBOOT_IMAGE_NUMBER > 1)
    [PLAT_OTP_ID_BL2_ROTPK_1] = USER_AREA_SIZE(dm_locked.bl2_rotpk[1]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 2)
    [PLAT_OTP_ID_BL2_ROTPK_2] = USER_AREA_SIZE(dm_locked.bl2_rotpk[2]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 3)
    [PLAT_OTP_ID_BL2_ROTPK_3] = USER_AREA_SIZE(dm_locked.bl2_rotpk[3]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 4)
    [PLAT_OTP_ID_BL2_ROTPK_4] = USER_AREA_SIZE(dm_locked.bl2_rotpk[4]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 5)
    [PLAT_OTP_ID_BL2_ROTPK_5] = USER_AREA_SIZE(dm_locked.bl2_rotpk[5]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 6)
    [PLAT_OTP_ID_BL2_ROTPK_6] = USER_AREA_SIZE(dm_locked.bl2_rotpk[6]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 7)
    [PLAT_OTP_ID_BL2_ROTPK_7] = USER_AREA_SIZE(dm_locked.bl2_rotpk[7]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 8)
    [PLAT_OTP_ID_BL2_ROTPK_8] = USER_AREA_SIZE(dm_locked.bl2_rotpk[8]),
#endif

    [PLAT_OTP_ID_NV_COUNTER_BL2_0] = USER_AREA_SIZE(unlocked_area.bl2_nv_counter[0]),
#if (MCUBOOT_IMAGE_NUMBER > 1)
    [PLAT_OTP_ID_NV_COUNTER_BL2_1] = USER_AREA_SIZE(unlocked_area.bl2_nv_counter[1]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 2)
    [PLAT_OTP_ID_NV_COUNTER_BL2_2] = USER_AREA_SIZE(unlocked_area.bl2_nv_counter[2]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 3)
    [PLAT_OTP_ID_NV_COUNTER_BL2_3] = USER_AREA_SIZE(unlocked_area.bl2_nv_counter[3]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 4)
    [PLAT_OTP_ID_NV_COUNTER_BL2_4] = USER_AREA_SIZE(unlocked_area.bl2_nv_counter[4]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 5)
    [PLAT_OTP_ID_NV_COUNTER_BL2_5] = USER_AREA_SIZE(unlocked_area.bl2_nv_counter[5]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 6)
    [PLAT_OTP_ID_NV_COUNTER_BL2_6] = USER_AREA_SIZE(unlocked_area.bl2_nv_counter[6]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 7)
    [PLAT_OTP_ID_NV_COUNTER_BL2_7] = USER_AREA_SIZE(unlocked_area.bl2_nv_counter[7]),
#endif
#if (MCUBOOT_IMAGE_NUMBER > 8)
    [PLAT_OTP_ID_NV_COUNTER_BL2_8] = USER_AREA_SIZE(unlocked_area.bl2_nv_counter[8]),
#endif

#ifdef PLATFORM_HAS_PS_NV_OTP_COUNTERS
    [PLAT_OTP_ID_NV_COUNTER_PS_0] = USER_AREA_SIZE(unlocked_area.ps_nv_counter[0]),
    [PLAT_OTP_ID_NV_COUNTER_PS_1] = USER_AREA_SIZE(unlocked_area.ps_nv_counter[1]),
    [PLAT_OTP_ID_NV_COUNTER_PS_2] = USER_AREA_SIZE(unlocked_area.ps_nv_counter[2]),
#endif /* PLATFORM_HAS_PS_NV_OTP_COUNTERS */

    [PLAT_OTP_ID_NV_COUNTER_NS_0] = USER_AREA_SIZE(unlocked_area.host_nv_counter[0]),
    [PLAT_OTP_ID_NV_COUNTER_NS_1] = USER_AREA_SIZE(unlocked_area.host_nv_counter[1]),
    [PLAT_OTP_ID_NV_COUNTER_NS_2] = USER_AREA_SIZE(unlocked_area.host_nv_counter[2]),

    [PLAT_OTP_ID_KEY_BL2_ENCRYPTION] = USER_AREA_SIZE(dm_locked.dm_encrypted.bl2_encryption_key),
    [PLAT_OTP_ID_KEY_SECURE_ENCRYPTION] = USER_AREA_SIZE(dm_locked.dm_encrypted.s_image_encryption_key),
    [PLAT_OTP_ID_KEY_NON_SECURE_ENCRYPTION] = USER_AREA_SIZE(dm_locked.dm_encrypted.ns_image_encryption_key),

    [PLAT_OTP_ID_BL1_2_IMAGE] = USER_AREA_SIZE(bl1_2_image),
    [PLAT_OTP_ID_BL1_2_IMAGE_LEN] = USER_AREA_SIZE(cm_locked.bl1_2_image_len),
    [PLAT_OTP_ID_BL1_2_IMAGE_HASH] = OTP_SIZE(rotpk),
    [PLAT_OTP_ID_BL1_ROTPK_0] = USER_AREA_SIZE(dm_locked.bl1_rotpk_0),

    [PLAT_OTP_ID_NV_COUNTER_BL1_0] = USER_AREA_SIZE(unlocked_area.bl1_nv_counter),

    [PLAT_OTP_ID_SECURE_DEBUG_PK] = USER_AREA_SIZE(dm_locked.secure_debug_pk),

    [PLAT_OTP_ID_HOST_ROTPK_S] = USER_AREA_SIZE(dm_locked.host_rotpk_s),
    [PLAT_OTP_ID_HOST_ROTPK_NS] = USER_AREA_SIZE(dm_locked.host_rotpk_ns),
    [PLAT_OTP_ID_HOST_ROTPK_CCA] = USER_AREA_SIZE(dm_locked.host_rotpk_cca),

    [PLAT_OTP_ID_CCA_SYSTEM_PROPERTIES] = USER_AREA_SIZE(cm_locked.cca_system_properties),

    [PLAT_OTP_ID_REPROVISIONING_BITS] = USER_AREA_SIZE(unlocked_area.reprovisioning_bits),
    [PLAT_OTP_ID_RSE_ID] = USER_AREA_SIZE(dm_locked.rse_id),

    [PLAT_OTP_ID_DMA_ICS] = USER_AREA_SIZE(dma_initial_command_sequence),

#ifdef RSE_HAS_MANUFACTURING_DATA
    [PLAT_OTP_ID_MANUFACTURING_DATA_LEN] = USER_AREA_SIZE(manufacturing_data.header.size),
#endif /* RSE_HAS_MANUFACTURING_DATA */

    [PLAT_OTP_ID_ROM_OTP_ENCRYPTION_KEY] = OTP_SIZE(kce_cm),
#if LCM_VERSION == 0
    [PLAT_OTP_ID_RUNTIME_OTP_ENCRYPTION_KEY] = USER_AREA_SIZE(dm_locked.dm_encrypted.runtime_otp_encryption_key),
#else
    [PLAT_OTP_ID_RUNTIME_OTP_ENCRYPTION_KEY] = OTP_SIZE(kce_dm),
#endif

    [PLAT_OTP_ID_CM_CONFIG_FLAGS] = USER_AREA_SIZE(cm_locked.cm_config_flags),
    [PLAT_OTP_ID_DM_CONFIG_FLAGS] = USER_AREA_SIZE(dm_locked.dm_config_flags),

#if RSE_AMOUNT > 1
    [PLAT_OTP_ID_RSE_TO_RSE_SENDER_ROUTING_TABLE] = USER_AREA_SIZE(dm_locked.rse_to_rse_sender_routing_table),
    [PLAT_OTP_ID_RSE_TO_RSE_RECEIVER_ROUTING_TABLE] = USER_AREA_SIZE(dm_locked.rse_to_rse_receiver_routing_table),
#endif /* RSE_AMOUNT > 1 */

    [PLAT_OTP_ID_ATTACK_TRACKING_BITS] = USER_AREA_SIZE(attack_tracking_bits),
};

#ifdef RSE_BRINGUP_OTP_EMULATION
static enum tfm_plat_err_t check_if_otp_is_emulated(uint32_t offset, uint32_t len)
{
    enum lcm_error_t lcm_err;
    enum lcm_tp_mode_t tp_mode;

    lcm_get_tp_mode(&LCM_DEV_S, &tp_mode);

    /* If the OTP is outside the emulated region, and the emulation is enabled,
     * then return UNSUPPORTED.
     */
    if (tp_mode != LCM_TP_MODE_PCI &&
        rse_otp_emulation_is_enabled() &&
        offset + len > RSE_BRINGUP_OTP_EMULATION_SIZE) {
        return TFM_PLAT_ERR_OTP_EMULATION_UNSUPPORTED;
    }

    return TFM_PLAT_ERR_SUCCESS;
}
#endif /* RSE_BRINGUP_OTP_EMULATION */

static enum tfm_plat_err_t otp_read(uint32_t offset, uint32_t len,
                                    uint32_t buf_len, uint8_t *buf)
{
    enum lcm_error_t lcm_err;
#ifdef RSE_BRINGUP_OTP_EMULATION
    enum tfm_plat_err_t plat_err;
#endif /* RSE_BRINGUP_OTP_EMULATION */

    if (len == 0) {
        return TFM_PLAT_ERR_SUCCESS;
    }

    if (offset == 0) {
        return TFM_PLAT_ERR_OTP_READ_UNSUPPORTED;
    }

#ifdef RSE_BRINGUP_OTP_EMULATION
    plat_err = check_if_otp_is_emulated(offset, len);
    if (plat_err != TFM_PLAT_ERR_SUCCESS) {
        return plat_err;
    }
#endif /* RSE_BRINGUP_OTP_EMULATION */

    if (buf_len < len) {
        len = buf_len;
    }

    lcm_err = lcm_otp_read(&LCM_DEV_S, offset, len, buf);
    if (lcm_err != LCM_ERROR_NONE) {
        return lcm_err;
    } else {
        return TFM_PLAT_ERR_SUCCESS;
    }
}

static enum tfm_plat_err_t otp_read_encrypted(uint32_t offset, uint32_t len,
                                              uint32_t buf_len, uint8_t *buf,
                                              enum kmu_hardware_keyslot_t key)
{
    if (len == 0) {
        return TFM_PLAT_ERR_SUCCESS;
    }

    if (offset == 0) {
        return TFM_PLAT_ERR_OTP_READ_ENCRYPTED_UNSUPPORTED;
    }

#ifndef RSE_ENCRYPTED_OTP_KEYS
    return otp_read(offset, len, buf_len, buf);
#else
    /* This is designed for keys, so 32 is a sane limit */
    uint32_t tmp_buf[32 / sizeof(uint32_t)];
    uint32_t iv[4] = {offset, 0, 0, 0};
    cc3xx_err_t cc_err;
    enum tfm_plat_err_t plat_err;

#ifdef RSE_BRINGUP_OTP_EMULATION
    plat_err = check_if_otp_is_emulated(offset, len);
    if (plat_err != TFM_PLAT_ERR_SUCCESS) {
        return plat_err;
    }
#endif /* RSE_BRINGUP_OTP_EMULATION */

    if (len > sizeof(tmp_buf)) {
        return TFM_PLAT_ERR_OTP_READ_ENCRYPTED_INVALID_INPUT;
    }

    plat_err = otp_read(offset, len, sizeof(tmp_buf), (uint8_t *)tmp_buf);
    if (plat_err != TFM_PLAT_ERR_SUCCESS) {
        return plat_err;
    }

    cc_err = cc3xx_lowlevel_aes_init(CC3XX_AES_DIRECTION_DECRYPT, CC3XX_AES_MODE_CTR,
                                     KMU_HW_SLOT_KCE_CM, NULL, CC3XX_AES_KEYSIZE_256,
                                     iv, sizeof(iv));
    if (cc_err != CC3XX_ERR_SUCCESS) {
        return cc_err;
    }

    cc3xx_lowlevel_aes_set_output_buffer(buf, buf_len);

    cc_err = cc3xx_lowlevel_aes_update((uint8_t *)tmp_buf, len);
    if (cc_err != CC3XX_ERR_SUCCESS) {
        cc3xx_lowlevel_aes_uninit();
        return cc_err;
    }

    cc3xx_lowlevel_aes_finish(NULL, NULL);

    return TFM_PLAT_ERR_SUCCESS;
#endif
}

static enum tfm_plat_err_t otp_write(uint32_t offset, uint32_t len,
                                     uint32_t buf_len, const uint8_t *buf)
{
    enum lcm_error_t err;

    if (buf_len > len) {
        return TFM_PLAT_ERR_OTP_WRITE_INVALID_INPUT;
    }

    err = lcm_otp_write(&LCM_DEV_S, offset, buf_len, buf);
    if (err != LCM_ERROR_NONE) {
        return err;
    }

    return TFM_PLAT_ERR_SUCCESS;
}

static enum tfm_plat_err_t otp_write_encrypted(uint32_t offset, uint32_t len,
                                     uint32_t buf_len, const uint8_t *buf,
                                     enum kmu_hardware_keyslot_t key)
{
#ifndef RSE_ENCRYPTED_OTP_KEYS
    return otp_write(offset, len, buf_len, buf);
#else
    /* This is designed for keys, so 32 is a sane limit */
    uint32_t tmp_buf[32 / sizeof(uint32_t)];
    uint32_t iv[4] = {offset, 0, 0, 0};
    cc3xx_err_t cc_err;
    enum tfm_plat_err_t plat_err;

    if (len > sizeof(tmp_buf)) {
        return TFM_PLAT_ERR_OTP_WRITE_ENCRYPTED_INVALID_INPUT;
    }

    cc_err = cc3xx_lowlevel_aes_init(CC3XX_AES_DIRECTION_ENCRYPT, CC3XX_AES_MODE_CTR,
                            key, NULL, CC3XX_AES_KEYSIZE_256,
                            iv, sizeof(iv));
    if (cc_err != CC3XX_ERR_SUCCESS) {
        return cc_err;
    }

    cc3xx_lowlevel_aes_set_output_buffer((uint8_t *)tmp_buf, sizeof(tmp_buf));

    cc_err = cc3xx_lowlevel_aes_update(buf, len);
    if (cc_err != CC3XX_ERR_SUCCESS) {
        cc3xx_lowlevel_aes_uninit();
        return cc_err;
    }

    cc3xx_lowlevel_aes_finish(NULL, NULL);

    plat_err = otp_write(offset, len, sizeof(tmp_buf), (uint8_t *)tmp_buf);
    if (plat_err != TFM_PLAT_ERR_SUCCESS) {
        return plat_err;
    }

    cc3xx_secure_erase_buffer(tmp_buf, sizeof(tmp_buf) / sizeof(uint32_t));
    if (cc_err != CC3XX_ERR_SUCCESS) {
        return cc_err;
    }

    return TFM_PLAT_ERR_SUCCESS;
#endif
}

static enum tfm_plat_err_t check_keys_for_tampering(enum lcm_lcs_t lcs)
{
    enum tfm_plat_err_t err;
    enum integrity_checker_error_t ic_err;
#ifdef RSE_HAS_MANUFACTURING_DATA
    uint32_t manufacturing_size;
#endif /* RSE_HAS_MANUFACTURING_DATA */
    uint64_t cm_size;
    uint64_t dm_size;

#ifdef RSE_HAS_MANUFACTURING_DATA
    err = otp_read(USER_AREA_OFFSET(manufacturing_data.header.size),
                   USER_AREA_SIZE(manufacturing_data.header.size),
                   sizeof(manufacturing_size), (uint8_t*)&manufacturing_size);
    if (err == TFM_PLAT_ERR_SUCCESS) {
        ic_err = integrity_checker_check_value(&INTEGRITY_CHECKER_DEV_S,
                                               INTEGRITY_CHECKER_MODE_ZERO_COUNT,
                                               (uint32_t *)(USER_AREA_ADDRESS(manufacturing_data.header) - manufacturing_size),
                                               manufacturing_size + 2 * sizeof(uint32_t),
                                               (uint32_t *)USER_AREA_ADDRESS(manufacturing_data.header.zero_count),
                                               USER_AREA_SIZE(manufacturing_data.header.zero_count));
        if (ic_err != INTEGRITY_CHECKER_ERROR_NONE) {
            return ic_err;
        }
    } else if (err != TFM_PLAT_ERR_OTP_EMULATION_UNSUPPORTED) {
        return err;
    }
#endif /* RSE_HAS_MANUFACTURING_DATA */

    if (lcs == LCM_LCS_DM || lcs == LCM_LCS_SE) {
            ic_err = integrity_checker_check_value(&INTEGRITY_CHECKER_DEV_S,
                                                   INTEGRITY_CHECKER_MODE_ZERO_COUNT,
                                                   (uint32_t *)USER_AREA_ADDRESS(cm_locked_size),
                                                   USER_AREA_SIZE(cm_locked_size),
                                                   (uint32_t *)USER_AREA_ADDRESS(cm_locked_size_zero_count),
                                                   USER_AREA_SIZE(cm_locked_size_zero_count));
            if (ic_err != INTEGRITY_CHECKER_ERROR_NONE) {
                return ic_err;
            }

            err = otp_read(USER_AREA_OFFSET(cm_locked_size),
                           USER_AREA_SIZE(cm_locked_size),
                           sizeof(cm_size), (uint8_t*)&cm_size);
            if (err != TFM_PLAT_ERR_SUCCESS) {
                return err;
            }

            ic_err = integrity_checker_check_value(&INTEGRITY_CHECKER_DEV_S,
                                                   INTEGRITY_CHECKER_MODE_ZERO_COUNT,
                                                   (uint32_t *)USER_AREA_ADDRESS(cm_locked),
                                                   cm_size,
                                                   (uint32_t *)USER_AREA_ADDRESS(cm_zero_count),
                                                   USER_AREA_SIZE(cm_zero_count));
            if (ic_err != INTEGRITY_CHECKER_ERROR_NONE) {
                return ic_err;
            }
    }

    if (lcs == LCM_LCS_SE) {
            ic_err = integrity_checker_check_value(&INTEGRITY_CHECKER_DEV_S,
                                                   INTEGRITY_CHECKER_MODE_ZERO_COUNT,
                                                   (uint32_t *)USER_AREA_ADDRESS(dm_locked_size),
                                                   USER_AREA_SIZE(dm_locked_size),
                                                   (uint32_t *)USER_AREA_ADDRESS(dm_locked_size_zero_count),
                                                   USER_AREA_SIZE(dm_locked_size_zero_count));
            if (ic_err != INTEGRITY_CHECKER_ERROR_NONE) {
                return ic_err;
            }

            err = otp_read(USER_AREA_OFFSET(dm_locked_size),
                           USER_AREA_SIZE(dm_locked_size),
                           sizeof(dm_size), (uint8_t*)&dm_size);
            if (err != TFM_PLAT_ERR_SUCCESS) {
                return err;
            }

            ic_err = integrity_checker_check_value(&INTEGRITY_CHECKER_DEV_S,
                                                   INTEGRITY_CHECKER_MODE_ZERO_COUNT,
                                                   (uint32_t *)(USER_AREA_ADDRESS(cm_locked) + cm_size),
                                                   dm_size,
                                                   (uint32_t *)USER_AREA_ADDRESS(dm_zero_count),
                                                   USER_AREA_SIZE(dm_zero_count));
            if (ic_err != INTEGRITY_CHECKER_ERROR_NONE) {
                return ic_err;
            }
    }

    return TFM_PLAT_ERR_SUCCESS;
}

static enum lcm_lcs_t map_otp_lcs_to_lcm_lcs(enum plat_otp_lcs_t lcs)
{
    switch (lcs) {
    case PLAT_OTP_LCS_ASSEMBLY_AND_TEST:
        return LCM_LCS_CM;
    case PLAT_OTP_LCS_PSA_ROT_PROVISIONING:
        return LCM_LCS_DM;
    case PLAT_OTP_LCS_SECURED:
        return LCM_LCS_SE;
    case PLAT_OTP_LCS_DECOMMISSIONED:
        return LCM_LCS_RMA;
    default:
        return LCM_LCS_INVALID;
    }
}

static enum plat_otp_lcs_t map_lcm_lcs_to_otp_lcs(enum lcm_lcs_t lcs)
{
    switch (lcs) {
    case LCM_LCS_CM:
        return PLAT_OTP_LCS_ASSEMBLY_AND_TEST;
    case LCM_LCS_DM:
        return PLAT_OTP_LCS_PSA_ROT_PROVISIONING;
    case LCM_LCS_SE:
        return PLAT_OTP_LCS_SECURED;
    case LCM_LCS_RMA:
        return PLAT_OTP_LCS_DECOMMISSIONED;
    default:
        return PLAT_OTP_LCS_UNKNOWN;
    }
}

static enum tfm_plat_err_t otp_read_lcs(size_t out_len, uint8_t *out)
{
    enum lcm_error_t lcm_err;
    enum lcm_lcs_t lcm_lcs;
    enum plat_otp_lcs_t *lcs = (enum plat_otp_lcs_t*) out;

    lcm_err = lcm_get_lcs(&LCM_DEV_S, &lcm_lcs);
    if (lcm_err != LCM_ERROR_NONE) {
        return lcm_err;
    }

    if (out_len != sizeof(uint32_t)) {
        return TFM_PLAT_ERR_OTP_READ_LCS_INVALID_INPUT;
    }

    *lcs = map_lcm_lcs_to_otp_lcs(lcm_lcs);

    return TFM_PLAT_ERR_SUCCESS;
}

enum tfm_plat_err_t tfm_plat_otp_init(void)
{
    uint32_t otp_size;
    enum lcm_error_t err;
    enum lcm_lcs_t lcs;
    enum integrity_checker_error_t ic_err;
    uint32_t unset_tracking_bits;

    err = lcm_init(&LCM_DEV_S);
    if (err != LCM_ERROR_NONE) {
        return err;
    }

    lcm_get_otp_size(&LCM_DEV_S, &otp_size);
    if ((otp_size < OTP_OFFSET(user_data) + sizeof(struct plat_user_area_layout_t)) ||
        (OTP_TOTAL_SIZE < OTP_OFFSET(user_data) + sizeof(struct plat_user_area_layout_t))) {
        return TFM_PLAT_ERR_OTP_INIT_SYSTEM_ERR;
    }

#ifdef RSE_BRINGUP_OTP_EMULATION
    /* Check that everything inside the main area can be emulated */
    if (USER_AREA_OFFSET(unlocked_area) + USER_AREA_SIZE(unlocked_area)
        > RSE_BRINGUP_OTP_EMULATION_SIZE) {
        return TFM_PLAT_ERR_OTP_INIT_SYSTEM_ERR;
    }
#endif /* RSE_BRINGUP_OTP_EMULATION */

    err = lcm_get_lcs(&LCM_DEV_S, &lcs);
    if (err != LCM_ERROR_NONE) {
        return err;
    }

    ic_err = integrity_checker_compute_value(&INTEGRITY_CHECKER_DEV_S,
                                             INTEGRITY_CHECKER_MODE_ZERO_COUNT,
                                             USER_AREA_ADDRESS(attack_tracking_bits),
                                             USER_AREA_SIZE(attack_tracking_bits),
                                             &unset_tracking_bits,
                                             sizeof(unset_tracking_bits), NULL);
    if (ic_err != INTEGRITY_CHECKER_ERROR_NONE) {
        return TFM_PLAT_ERR_SYSTEM_ERR;
    }

    if (unset_tracking_bits == 0) {
        return TFM_PLAT_ERR_NOT_PERMITTED;
    }

    return check_keys_for_tampering(lcs);
}

#define PLAT_OTP_ID_BL2_ROTPK_MAX PLAT_OTP_ID_BL2_ROTPK_0 + MCUBOOT_IMAGE_NUMBER
#define PLAT_OTP_ID_NV_COUNTER_BL2_MAX \
    PLAT_OTP_ID_NV_COUNTER_BL2_0 + MCUBOOT_IMAGE_NUMBER

enum tfm_plat_err_t tfm_plat_otp_read(enum tfm_otp_element_id_t id,
                                      size_t out_len, uint8_t *out)
{
    enum tfm_plat_err_t err;
#ifdef RSE_HAS_MANUFACTURING_DATA
    uint32_t manufacturing_data_size;
#endif /* RSE_HAS_MANUFACTURING_DATA */
    size_t bl1_2_size;
    uint32_t bl1_2_offset;

    if (id >= PLAT_OTP_ID_MAX) {
        return TFM_PLAT_ERR_PLAT_OTP_READ_INVALID_INPUT;
    }

    if ((id >= PLAT_OTP_ID_BL2_ROTPK_MAX && id <= PLAT_OTP_ID_BL2_ROTPK_8) ||
        (id >= PLAT_OTP_ID_NV_COUNTER_BL2_MAX && id <= PLAT_OTP_ID_NV_COUNTER_BL2_8)) {
        return TFM_PLAT_ERR_PLAT_OTP_READ_UNSUPPORTED;
    }

    switch(id) {
    case PLAT_OTP_ID_LCS:
        return otp_read_lcs(out_len, out);
    case PLAT_OTP_ID_KEY_BL2_ENCRYPTION:
    case PLAT_OTP_ID_KEY_SECURE_ENCRYPTION:
    case PLAT_OTP_ID_KEY_NON_SECURE_ENCRYPTION:
        return otp_read_encrypted(otp_offsets[id], otp_sizes[id], out_len, out,
                                  OTP_ROM_ENCRYPTION_KEY);
    case PLAT_OTP_ID_BL1_2_IMAGE:
        err = otp_read(USER_AREA_OFFSET(cm_locked.bl1_2_image_len),
                       USER_AREA_SIZE(cm_locked.bl1_2_image_len),
                       sizeof(bl1_2_size), (uint8_t *)&bl1_2_size);
        if (err != TFM_PLAT_ERR_SUCCESS) {
            return err;
        }

#ifdef RSE_HAS_MANUFACTURING_DATA
        err = otp_read(USER_AREA_OFFSET(manufacturing_data.header.size),
                       USER_AREA_SIZE(manufacturing_data.header.size),
                       sizeof(manufacturing_data_size), (uint8_t *)&manufacturing_data_size);
        if (err != TFM_PLAT_ERR_SUCCESS) {
            return err;
        }
        bl1_2_offset = USER_AREA_OFFSET(manufacturing_data.header) - manufacturing_data_size - bl1_2_size;
#else
        bl1_2_offset = USER_AREA_OFFSET(dma_initial_command_sequence) - bl1_2_size;
#endif

        return otp_read(bl1_2_offset,
                        bl1_2_size,
                        out_len, out);

    case PLAT_OTP_ID_MANUFACTURING_DATA:
#ifdef RSE_HAS_MANUFACTURING_DATA
        err = otp_read(USER_AREA_OFFSET(manufacturing_data.header.size),
                       USER_AREA_SIZE(manufacturing_data.header.size),
                       sizeof(manufacturing_data_size), (uint8_t *)&manufacturing_data_size);
        if (err != TFM_PLAT_ERR_SUCCESS) {
            return err;
        }

        return otp_read(USER_AREA_OFFSET(manufacturing_data.header) - manufacturing_data_size,
                        manufacturing_data_size,
                        out_len, out);
#else
        return TFM_PLAT_ERR_PLAT_OTP_READ_MFG_DATA_UNSUPPORTED;
#endif
    default:
        return otp_read(otp_offsets[id], otp_sizes[id], out_len, out);
    }
}

static enum tfm_plat_err_t otp_write_lcs(size_t in_len, const uint8_t *in)
{
    enum tfm_plat_err_t err;
    uint32_t lcs;
    enum lcm_lcs_t new_lcs = map_otp_lcs_to_lcm_lcs(*(uint32_t*)in);
    enum lcm_error_t lcm_err;
    uint16_t gppc_val = 0;
    uint32_t zero_bit_count;
    uint64_t region_size;
    enum integrity_checker_error_t ic_err;

    if (in_len != sizeof(lcs)) {
        return TFM_PLAT_ERR_OTP_WRITE_LCS_INVALID_INPUT;
    }

    switch(new_lcs) {
        case LCM_LCS_DM:
            /* Write the size of the CM locked area */
            region_size = USER_AREA_SIZE(cm_locked);
            err = otp_write(USER_AREA_OFFSET(cm_locked_size),
                            USER_AREA_SIZE(cm_locked_size),
                            sizeof(region_size), (uint8_t *)&region_size);
            if (err != TFM_PLAT_ERR_SUCCESS) {
                return err;
            }

            /* Write the zero-bit count of the CM locked area size */
            ic_err = integrity_checker_compute_value(&INTEGRITY_CHECKER_DEV_S,
                                                     INTEGRITY_CHECKER_MODE_ZERO_COUNT,
                                                     (uint32_t*)&region_size,
                                                     sizeof(region_size),
                                                     &zero_bit_count,
                                                     sizeof(zero_bit_count),
                                                     NULL);
            if (ic_err != INTEGRITY_CHECKER_ERROR_NONE) {
                return ic_err;
            }
            err = otp_write(USER_AREA_OFFSET(cm_locked_size_zero_count),
                            USER_AREA_SIZE(cm_locked_size_zero_count),
                            sizeof(zero_bit_count), (uint8_t *)&zero_bit_count);
            if (err != TFM_PLAT_ERR_SUCCESS) {
                return err;
            }

            /* Write the zero-count of the CM locked area */
            ic_err = integrity_checker_compute_value(&INTEGRITY_CHECKER_DEV_S,
                                                     INTEGRITY_CHECKER_MODE_ZERO_COUNT,
                                                     (uint32_t *)USER_AREA_ADDRESS(cm_locked),
                                                     region_size,
                                                     &zero_bit_count,
                                                     sizeof(zero_bit_count),
                                                     NULL);
            if (ic_err != INTEGRITY_CHECKER_ERROR_NONE) {
                return ic_err;
            }
            err = otp_write(USER_AREA_OFFSET(cm_zero_count),
                            USER_AREA_SIZE(cm_zero_count), sizeof(zero_bit_count),
                            (uint8_t *)&zero_bit_count);
            if (err != TFM_PLAT_ERR_SUCCESS) {
                return err;
            }
            break;
        case LCM_LCS_SE:
            /* Write the size of the DM locked area */
            region_size = USER_AREA_SIZE(dm_locked);
            err = otp_write(USER_AREA_OFFSET(dm_locked_size),
                            USER_AREA_SIZE(dm_locked_size),
                            sizeof(region_size), (uint8_t *)&region_size);
            if (err != TFM_PLAT_ERR_SUCCESS) {
                return err;
            }

            /* Write the zero-bit count of the DM locked area size */
            ic_err = integrity_checker_compute_value(&INTEGRITY_CHECKER_DEV_S,
                                                     INTEGRITY_CHECKER_MODE_ZERO_COUNT,
                                                     (uint32_t*)&region_size,
                                                     sizeof(region_size),
                                                     &zero_bit_count,
                                                     sizeof(zero_bit_count),
                                                     NULL);
            if (ic_err != INTEGRITY_CHECKER_ERROR_NONE) {
                return ic_err;
            }
            err = otp_write(USER_AREA_OFFSET(dm_locked_size_zero_count),
                            USER_AREA_SIZE(dm_locked_size_zero_count),
                            sizeof(zero_bit_count), (uint8_t *)&zero_bit_count);
            if (err != TFM_PLAT_ERR_SUCCESS) {
                return err;
            }

            /* Write the zero-count of the DM locked area */
            ic_err = integrity_checker_compute_value(&INTEGRITY_CHECKER_DEV_S,
                                                     INTEGRITY_CHECKER_MODE_ZERO_COUNT,
                                                     (uint32_t *)USER_AREA_ADDRESS(dm_locked),
                                                     region_size,
                                                     &zero_bit_count,
                                                     sizeof(zero_bit_count),
                                                     NULL);
            if (ic_err != INTEGRITY_CHECKER_ERROR_NONE) {
                return ic_err;
            }
            err = otp_write(USER_AREA_OFFSET(dm_zero_count),
                            USER_AREA_SIZE(dm_zero_count), sizeof(zero_bit_count),
                            (uint8_t *)&zero_bit_count);
            if (err != TFM_PLAT_ERR_SUCCESS) {
                return err;
            }
            break;
        case LCM_LCS_RMA:
            break;
        case LCM_LCS_CM:
        case LCM_LCS_INVALID:
            return TFM_PLAT_ERR_OTP_WRITE_LCS_SYSTEM_ERR;
    }

    lcm_err = lcm_set_lcs(&LCM_DEV_S, new_lcs, gppc_val);
    if (lcm_err != LCM_ERROR_NONE) {
        return lcm_err;
    }

    return TFM_PLAT_ERR_SUCCESS;
}

enum tfm_plat_err_t tfm_plat_otp_write(enum tfm_otp_element_id_t id,
                                       size_t in_len, const uint8_t *in)
{
#ifdef RSE_HAS_MANUFACTURING_DATA
    enum tfm_plat_err_t err;
    size_t manufacturing_data_size = 0;
#endif
    uint32_t bl1_2_offset;

    if (id >= PLAT_OTP_ID_MAX) {
        return TFM_PLAT_ERR_PLAT_OTP_WRITE_INVALID_INPUT;
    }

    if ((id >= PLAT_OTP_ID_BL2_ROTPK_MAX && id <= PLAT_OTP_ID_BL2_ROTPK_8) ||
        (id >= PLAT_OTP_ID_NV_COUNTER_BL2_MAX && id <= PLAT_OTP_ID_NV_COUNTER_BL2_8)) {
        return TFM_PLAT_ERR_PLAT_OTP_WRITE_UNSUPPORTED;
    }

    switch (id) {
    case PLAT_OTP_ID_LCS:
        return otp_write_lcs(in_len, in);
    case PLAT_OTP_ID_KEY_BL2_ENCRYPTION:
    case PLAT_OTP_ID_KEY_SECURE_ENCRYPTION:
    case PLAT_OTP_ID_KEY_NON_SECURE_ENCRYPTION:
        return otp_write_encrypted(otp_offsets[id], otp_sizes[id], in_len, in,
                                   OTP_ROM_ENCRYPTION_KEY);
    case PLAT_OTP_ID_BL1_2_IMAGE:
#ifdef RSE_HAS_MANUFACTURING_DATA
        err = otp_read(USER_AREA_OFFSET(manufacturing_data.header.size),
                       USER_AREA_SIZE(manufacturing_data.header.size),
                       sizeof(manufacturing_data_size), (uint8_t *)&manufacturing_data_size);
        if (err != TFM_PLAT_ERR_SUCCESS) {
            return err;
        }

        bl1_2_offset = USER_AREA_OFFSET(manufacturing_data.header) - manufacturing_data_size - in_len;
#else
        bl1_2_offset = USER_AREA_OFFSET(dma_initial_command_sequence) - in_len;
#endif

        return otp_write(bl1_2_offset, otp_sizes[id], in_len, in);
    default:
        return otp_write(otp_offsets[id], otp_sizes[id], in_len, in);
    }
}


enum tfm_plat_err_t tfm_plat_otp_get_size(enum tfm_otp_element_id_t id,
                                          size_t *size)
{
    if (id >= PLAT_OTP_ID_MAX) {
        return TFM_PLAT_ERR_PLAT_OTP_GET_SIZE_INVALID_INPUT;
    }

    if ((id >= PLAT_OTP_ID_BL2_ROTPK_MAX && id <= PLAT_OTP_ID_BL2_ROTPK_8) ||
        (id >= PLAT_OTP_ID_NV_COUNTER_BL2_MAX && id <= PLAT_OTP_ID_NV_COUNTER_BL2_8)) {
        return TFM_PLAT_ERR_PLAT_OTP_GET_SIZE_UNSUPPORTED;
    }

    *size = otp_sizes[id];

    return TFM_PLAT_ERR_SUCCESS;
}

enum tfm_plat_err_t tfm_plat_otp_secure_provisioning_start(void)
{
    enum lcm_bool_t sp_enabled;
    enum lcm_error_t lcm_err;
#if LCM_VERSION == 0
    static uint8_t __ALIGNED(INTEGRITY_CHECKER_REQUIRED_ALIGNMENT) dummy_key_value[32] = {
                                                       0x01, 0x02, 0x03, 0x04,
                                                       0x01, 0x02, 0x03, 0x04,
                                                       0x01, 0x02, 0x03, 0x04,
                                                       0x01, 0x02, 0x03, 0x04,
                                                       0x01, 0x02, 0x03, 0x04,
                                                       0x01, 0x02, 0x03, 0x04,
                                                       0x01, 0x02, 0x03, 0x04,
                                                       0x01, 0x02, 0x03, 0x04};
    uint32_t gppc_val = 0x0800;
    uint32_t dm_config_2 = 0xFFFFFFFFu;
    enum lcm_lcs_t lcs;
#endif /* LCM_VERSION == 0 */

    lcm_get_sp_enabled(&LCM_DEV_S, &sp_enabled);

    if (sp_enabled != LCM_TRUE) {
        lcm_set_sp_enabled(&LCM_DEV_S);
    } else {
#if LCM_VERSION == 0
        /* Now we're in SP mode, if we have the R0 LCM, we should make sure the
         * transition will occur.
         */
        lcm_err = lcm_get_lcs(&LCM_DEV_S, &lcs);
        if (lcm_err != LCM_ERROR_NONE) {
            return lcm_err;
        }

        switch(lcs) {
        case LCM_LCS_CM:
            /* Trigger CM->DM by setting the uppermost bit of GPPC */
            lcm_err = lcm_otp_write(&LCM_DEV_S, OTP_OFFSET(cm_config_2), sizeof(gppc_val),
                                    (uint8_t *)&gppc_val);
            if (lcm_err != LCM_ERROR_NONE) {
                return lcm_err;
            }
            break;
        case LCM_LCS_DM:
            /* Triggering DM->SE is trickier. Both KP_DM and KCE_DM must be set
             * with dummy data.
             */
            lcm_err = lcm_otp_write(&LCM_DEV_S, OTP_OFFSET(kp_dm),
                                    sizeof(dummy_key_value), dummy_key_value);
            if (lcm_err != LCM_ERROR_NONE) {
                return lcm_err;
            }

            lcm_err = lcm_otp_write(&LCM_DEV_S, OTP_OFFSET(kce_dm),
                                    sizeof(dummy_key_value), dummy_key_value);
            if (lcm_err != LCM_ERROR_NONE) {
                return lcm_err;
            }

            /* Finally, write dm_config to trigger the zero-count checking. It
             * doesn't matter what is written on the R0 LCM.
             */
            lcm_err = lcm_otp_write(&LCM_DEV_S, OTP_OFFSET(dm_config),
                                    sizeof(dm_config_2), (uint8_t *)&dm_config_2);
            if (lcm_err != LCM_ERROR_NONE && lcm_err != LCM_ERROR_OTP_WRITE_WRITE_VERIFY_FAIL) {
                return lcm_err;
            }
            break;
        default:
            break;
        }

#endif /* LCM_VERSION == 0 */
    }

    return TFM_PLAT_ERR_SUCCESS;
}

enum tfm_plat_err_t tfm_plat_otp_secure_provisioning_finish(void)
{
    tfm_hal_system_reset();

    /* We'll never get here */
    return TFM_PLAT_ERR_SUCCESS;
}
