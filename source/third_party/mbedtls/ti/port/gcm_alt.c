/******************************************************************************
 Copyright (c) 2022-2023, Texas Instruments Incorporated
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 *  Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

 *  Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 *  Neither the name of Texas Instruments Incorporated nor the names of
    its contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include "mbedtls/gcm.h"
#include "mbedtls/error.h"
#include "gcm_alt.h"

#if defined(MBEDTLS_GCM_ALT)

#include <string.h>

#include <ti/drivers/AESGCM.h>
#include <ti/drivers/cryptoutils/cryptokey/CryptoKeyPlaintext.h>

const AESGCM_HWAttrs defaultAesGcmHwAttrs = {0};
bool gcmInitialized                       = false;

/*
 * Initialize context
 */
void mbedtls_gcm_init(mbedtls_gcm_context *ctx)
{
    memset(ctx, 0, sizeof(mbedtls_gcm_context));

    if (gcmInitialized == false)
    {
        AESGCM_init();
        gcmInitialized = true;
    }

    AESGCM_Params params;
    AESGCM_Params_init(&params);
    params.returnBehavior = AESGCM_RETURN_BEHAVIOR_POLLING;

    ctx->gcmConfig.object  = &ctx->gcmObject;
    ctx->gcmConfig.hwAttrs = &defaultAesGcmHwAttrs;

    ctx->handle = AESGCM_construct(&ctx->gcmConfig, &params);
}

int mbedtls_gcm_setkey(mbedtls_gcm_context *ctx,
                       mbedtls_cipher_id_t cipher,
                       const unsigned char *key,
                       unsigned int keybits)
{

    /* Initialize AES key */
    memcpy(ctx->keyMaterial, key, (keybits >> 3));
    /* CryptoKeyPlaintext_initKey() always return success */
    (void)CryptoKeyPlaintext_initKey(&ctx->cryptoKey, (uint8_t *)ctx->keyMaterial, (keybits >> 3));
    return 0;
}

static int gcm_auth_crypt(mbedtls_gcm_context *ctx,
                          int mode,
                          size_t length,
                          const unsigned char *iv,
                          size_t iv_len,
                          const unsigned char *add,
                          size_t add_len,
                          const unsigned char *input,
                          unsigned char *output,
                          size_t tag_len,
                          unsigned char *tag)
{
    int_fast16_t status = 0;

    if (ctx->handle == NULL)
    {
        return MBEDTLS_ERR_CIPHER_INVALID_CONTEXT;
    }
    /* The IV length must be 12. This is checked internally in AESGCM_setIV().
     * The combined length of AAD and payload data must be non-zero. This is
     * checked internally in AESGCM_setLengths().
     */
    if ((iv_len != 12) || (add_len == 0 && length == 0))
    {
        return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;
    }

    AESGCM_OneStepOperation operation;
    AESGCM_OneStepOperation_init(&operation);
    operation.key         = &ctx->cryptoKey;
    operation.aad         = (uint8_t *)add;
    operation.aadLength   = add_len;
    operation.input       = (uint8_t *)input;
    operation.inputLength = length;
    operation.output      = (uint8_t *)output;
    operation.iv          = (uint8_t *)iv;
    operation.ivLength    = iv_len;
    operation.mac         = (uint8_t *)tag;
    operation.macLength   = tag_len;

    if (mode == MBEDTLS_GCM_ENCRYPT)
    {
        status = AESGCM_oneStepEncrypt(ctx->handle, &operation);
    }
    else if (mode == MBEDTLS_GCM_DECRYPT)
    {
        status = AESGCM_oneStepDecrypt(ctx->handle, &operation);
    }

    if (status != AESGCM_STATUS_SUCCESS)
    {
        status = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
    }

    return (int)status;
}

int mbedtls_gcm_crypt_and_tag(mbedtls_gcm_context *ctx,
                              int mode,
                              size_t length,
                              const unsigned char *iv,
                              size_t iv_len,
                              const unsigned char *add,
                              size_t add_len,
                              const unsigned char *input,
                              unsigned char *output,
                              size_t tag_len,
                              unsigned char *tag)
{
    int_fast16_t status;

    status = gcm_auth_crypt(ctx, mode, length, iv, iv_len, add, add_len, input, output, tag_len, tag);

    if ((mode == MBEDTLS_GCM_DECRYPT) && (status == AESGCM_STATUS_MAC_INVALID))
    {
        /* This function does not check verification. Ignore failed authentication and return success */
        return 0;
    }

    return (int)status;
}

int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context *ctx,
                             size_t length,
                             const unsigned char *iv,
                             size_t iv_len,
                             const unsigned char *add,
                             size_t add_len,
                             const unsigned char *tag,
                             size_t tag_len,
                             const unsigned char *input,
                             unsigned char *output)
{
    int_fast16_t status;

    status = gcm_auth_crypt(ctx, MBEDTLS_GCM_DECRYPT, length, iv, iv_len, add, add_len, input, output, tag_len, (unsigned char *)tag);

    /*
     * Translate authentication failed error code. This differs from
     * using mbedtls_gcm_crypt_and_tag() with MBEDTLS_GCM_DECRYPT mode.
     */
    if (status == AESGCM_STATUS_MAC_INVALID)
    {
        return MBEDTLS_ERR_GCM_AUTH_FAILED;
    }

    return (int)status;
}

int mbedtls_gcm_starts(mbedtls_gcm_context *ctx, int mode, const unsigned char *iv, size_t iv_len)
{
    int_fast16_t status = 0;
    ctx->mode           = mode;

    if (ctx->handle == NULL)
    {
        return MBEDTLS_ERR_CIPHER_INVALID_CONTEXT;
    }

    if (mode == MBEDTLS_GCM_ENCRYPT)
    {
        status = AESGCM_setupEncrypt(ctx->handle, &ctx->cryptoKey, 0, 0);
    }
    else if (mode == MBEDTLS_GCM_DECRYPT)
    {
        status = AESGCM_setupDecrypt(ctx->handle, &ctx->cryptoKey, 0, 0);
    }

    if (status != AESGCM_STATUS_SUCCESS)
    {
        return MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
    }

    status = AESGCM_setIV(ctx->handle, iv, iv_len);
    if (status != AESGCM_STATUS_SUCCESS)
    {
        status = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
    }
    return (int)status;
}

int mbedtls_gcm_update_ad(mbedtls_gcm_context *ctx, const unsigned char *add, size_t add_len)
{
    int_fast16_t status;

    if (ctx->handle == NULL)
    {
        return MBEDTLS_ERR_CIPHER_INVALID_CONTEXT;
    }

    if (add_len == 0)
    {
        /* If no additional data, do nothing and return success */
        return 0;
    }

    AESGCM_SegmentedAADOperation segmentedAADOperation;
    AESGCM_SegmentedAADOperation_init(&segmentedAADOperation);
    segmentedAADOperation.aad       = (uint8_t *)add;
    segmentedAADOperation.aadLength = add_len;

    status = AESGCM_addAAD(ctx->handle, &segmentedAADOperation);

    if (status != AESGCM_STATUS_SUCCESS)
    {
        status = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
    }

    return (int)status;
}

int mbedtls_gcm_update(mbedtls_gcm_context *ctx,
                       const unsigned char *input,
                       size_t input_length,
                       unsigned char *output,
                       size_t output_size,
                       size_t *output_length)
{
    int_fast16_t status;
    *output_length = input_length;

    if (ctx->handle == NULL)
    {
        return MBEDTLS_ERR_CIPHER_INVALID_CONTEXT;
    }

    if (input_length == 0)
    {
        /* If no data, do nothing and return success */
        return 0;
    }

    AESGCM_SegmentedDataOperation segmentedDataOperation;
    AESGCM_SegmentedDataOperation_init(&segmentedDataOperation);
    segmentedDataOperation.input       = (uint8_t *)input;
    segmentedDataOperation.output      = (uint8_t *)output;
    segmentedDataOperation.inputLength = input_length;

    status = AESGCM_addData(ctx->handle, &segmentedDataOperation);

    if (status != AESGCM_STATUS_SUCCESS)
    {
        status = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
    }

    return (int)status;
}

int mbedtls_gcm_finish(mbedtls_gcm_context *ctx,
                       unsigned char *output,
                       size_t output_size,
                       size_t *output_length,
                       unsigned char *tag,
                       size_t tag_len)
{
    int_fast16_t status;

    if (ctx->handle == NULL)
    {
        return MBEDTLS_ERR_CIPHER_INVALID_CONTEXT;
    }

    AESGCM_SegmentedFinalizeOperation segmentedFinalizeOperation;
    AESGCM_SegmentedFinalizeOperation_init(&segmentedFinalizeOperation);
    segmentedFinalizeOperation.inputLength = 0;
    segmentedFinalizeOperation.mac         = tag;
    segmentedFinalizeOperation.macLength   = tag_len;

    if (ctx->mode == MBEDTLS_GCM_ENCRYPT)
    {
        status = AESGCM_finalizeEncrypt(ctx->handle, &segmentedFinalizeOperation);
    }
    else if (ctx->mode == MBEDTLS_GCM_DECRYPT)
    {
        status = AESGCM_finalizeDecrypt(ctx->handle, &segmentedFinalizeOperation);
    }

    if (status != AESGCM_STATUS_SUCCESS)
    {
        status = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
    }

    return (int)status;
}

void mbedtls_gcm_free(mbedtls_gcm_context *ctx)
{
    if (ctx->handle != NULL)
    {
        AESGCM_close(ctx->handle);
    }

    memset(ctx, 0, sizeof(mbedtls_gcm_context));
}

int mbedtls_gcm_set_lengths(mbedtls_gcm_context *ctx, size_t total_ad_len, size_t plaintext_len)
{
    int_fast16_t status;

    if (ctx->handle == NULL)
    {
        return MBEDTLS_ERR_CIPHER_INVALID_CONTEXT;
    }

    status = AESGCM_setLengths(ctx->handle, total_ad_len, plaintext_len);

    if (status != AESGCM_STATUS_SUCCESS)
    {
        status = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
    }

    return (int)status;
}

#if defined(MBEDTLS_SELF_TEST) && defined(MBEDTLS_AES_C)
#include "mbedtls/platform.h"
        /*
         * AES-GCM test vectors from:
         *
         * http://csrc.nist.gov/groups/STM/cavp/documents/mac/gcmtestvectors.zip
         */
#define MAX_TESTS 6

static const int key_index_test_data[MAX_TESTS] = {0, 0, 1, 1, 1, 1};

static const unsigned char key_test_data[MAX_TESTS][32] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c, 0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
     0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c, 0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08},
};

static const size_t iv_len_test_data[MAX_TESTS] = {12, 12, 12, 12, 8, 60};

static const int iv_index_test_data[MAX_TESTS] = {0, 0, 1, 1, 1, 2};

static const unsigned char iv_test_data[MAX_TESTS][64] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce, 0xdb, 0xad, 0xde, 0xca, 0xf8, 0x88},
    {0x93, 0x13, 0x22, 0x5d, 0xf8, 0x84, 0x06, 0xe5, 0x55, 0x90, 0x9c, 0x5a, 0xff, 0x52, 0x69,
     0xaa, 0x6a, 0x7a, 0x95, 0x38, 0x53, 0x4f, 0x7d, 0xa1, 0xe4, 0xc3, 0x03, 0xd2, 0xa3, 0x18,
     0xa7, 0x28, 0xc3, 0xc0, 0xc9, 0x51, 0x56, 0x80, 0x95, 0x39, 0xfc, 0xf0, 0xe2, 0x42, 0x9a,
     0x6b, 0x52, 0x54, 0x16, 0xae, 0xdb, 0xf5, 0xa0, 0xde, 0x6a, 0x57, 0xa6, 0x37, 0xb3, 0x9b},
};

static const size_t add_len_test_data[MAX_TESTS] = {0, 0, 0, 20, 20, 20};

static const int add_index_test_data[MAX_TESTS] = {0, 0, 0, 1, 1, 1};

static const unsigned char additional_test_data[MAX_TESTS][64] = {
    {0x00},
    {0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef, 0xfe, 0xed,
     0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef, 0xab, 0xad, 0xda, 0xd2},
};

static const size_t pt_len_test_data[MAX_TESTS] = {0, 16, 64, 60, 60, 60};

static const int pt_index_test_data[MAX_TESTS] = {0, 0, 1, 1, 1, 1};

static const unsigned char pt_test_data[MAX_TESTS][64] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5, 0xa5, 0x59, 0x09, 0xc5, 0xaf, 0xf5, 0x26, 0x9a,
     0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda, 0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31, 0x8a, 0x72,
     0x1c, 0x3c, 0x0c, 0x95, 0x95, 0x68, 0x09, 0x53, 0x2f, 0xcf, 0x0e, 0x24, 0x49, 0xa6, 0xb5, 0x25,
     0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57, 0xba, 0x63, 0x7b, 0x39, 0x1a, 0xaf, 0xd2, 0x55},
};

static const unsigned char ct_test_data[MAX_TESTS * 3][64] = {
    {0x00},
    {0x03, 0x88, 0xda, 0xce, 0x60, 0xb6, 0xa3, 0x92, 0xf3, 0x28, 0xc2, 0xb9, 0x71, 0xb2, 0xfe, 0x78},
    {0x42, 0x83, 0x1e, 0xc2, 0x21, 0x77, 0x74, 0x24, 0x4b, 0x72, 0x21, 0xb7, 0x84, 0xd0, 0xd4, 0x9c,
     0xe3, 0xaa, 0x21, 0x2f, 0x2c, 0x02, 0xa4, 0xe0, 0x35, 0xc1, 0x7e, 0x23, 0x29, 0xac, 0xa1, 0x2e,
     0x21, 0xd5, 0x14, 0xb2, 0x54, 0x66, 0x93, 0x1c, 0x7d, 0x8f, 0x6a, 0x5a, 0xac, 0x84, 0xaa, 0x05,
     0x1b, 0xa3, 0x0b, 0x39, 0x6a, 0x0a, 0xac, 0x97, 0x3d, 0x58, 0xe0, 0x91, 0x47, 0x3f, 0x59, 0x85},
    {0x42, 0x83, 0x1e, 0xc2, 0x21, 0x77, 0x74, 0x24, 0x4b, 0x72, 0x21, 0xb7, 0x84, 0xd0, 0xd4,
     0x9c, 0xe3, 0xaa, 0x21, 0x2f, 0x2c, 0x02, 0xa4, 0xe0, 0x35, 0xc1, 0x7e, 0x23, 0x29, 0xac,
     0xa1, 0x2e, 0x21, 0xd5, 0x14, 0xb2, 0x54, 0x66, 0x93, 0x1c, 0x7d, 0x8f, 0x6a, 0x5a, 0xac,
     0x84, 0xaa, 0x05, 0x1b, 0xa3, 0x0b, 0x39, 0x6a, 0x0a, 0xac, 0x97, 0x3d, 0x58, 0xe0, 0x91},
    {0x61, 0x35, 0x3b, 0x4c, 0x28, 0x06, 0x93, 0x4a, 0x77, 0x7f, 0xf5, 0x1f, 0xa2, 0x2a, 0x47,
     0x55, 0x69, 0x9b, 0x2a, 0x71, 0x4f, 0xcd, 0xc6, 0xf8, 0x37, 0x66, 0xe5, 0xf9, 0x7b, 0x6c,
     0x74, 0x23, 0x73, 0x80, 0x69, 0x00, 0xe4, 0x9f, 0x24, 0xb2, 0x2b, 0x09, 0x75, 0x44, 0xd4,
     0x89, 0x6b, 0x42, 0x49, 0x89, 0xb5, 0xe1, 0xeb, 0xac, 0x0f, 0x07, 0xc2, 0x3f, 0x45, 0x98},
    {0x8c, 0xe2, 0x49, 0x98, 0x62, 0x56, 0x15, 0xb6, 0x03, 0xa0, 0x33, 0xac, 0xa1, 0x3f, 0xb8,
     0x94, 0xbe, 0x91, 0x12, 0xa5, 0xc3, 0xa2, 0x11, 0xa8, 0xba, 0x26, 0x2a, 0x3c, 0xca, 0x7e,
     0x2c, 0xa7, 0x01, 0xe4, 0xa9, 0xa4, 0xfb, 0xa4, 0x3c, 0x90, 0xcc, 0xdc, 0xb2, 0x81, 0xd4,
     0x8c, 0x7c, 0x6f, 0xd6, 0x28, 0x75, 0xd2, 0xac, 0xa4, 0x17, 0x03, 0x4c, 0x34, 0xae, 0xe5},
    {0x00},
    {0x98, 0xe7, 0x24, 0x7c, 0x07, 0xf0, 0xfe, 0x41, 0x1c, 0x26, 0x7e, 0x43, 0x84, 0xb0, 0xf6, 0x00},
    {0x39, 0x80, 0xca, 0x0b, 0x3c, 0x00, 0xe8, 0x41, 0xeb, 0x06, 0xfa, 0xc4, 0x87, 0x2a, 0x27, 0x57,
     0x85, 0x9e, 0x1c, 0xea, 0xa6, 0xef, 0xd9, 0x84, 0x62, 0x85, 0x93, 0xb4, 0x0c, 0xa1, 0xe1, 0x9c,
     0x7d, 0x77, 0x3d, 0x00, 0xc1, 0x44, 0xc5, 0x25, 0xac, 0x61, 0x9d, 0x18, 0xc8, 0x4a, 0x3f, 0x47,
     0x18, 0xe2, 0x44, 0x8b, 0x2f, 0xe3, 0x24, 0xd9, 0xcc, 0xda, 0x27, 0x10, 0xac, 0xad, 0xe2, 0x56},
    {0x39, 0x80, 0xca, 0x0b, 0x3c, 0x00, 0xe8, 0x41, 0xeb, 0x06, 0xfa, 0xc4, 0x87, 0x2a, 0x27,
     0x57, 0x85, 0x9e, 0x1c, 0xea, 0xa6, 0xef, 0xd9, 0x84, 0x62, 0x85, 0x93, 0xb4, 0x0c, 0xa1,
     0xe1, 0x9c, 0x7d, 0x77, 0x3d, 0x00, 0xc1, 0x44, 0xc5, 0x25, 0xac, 0x61, 0x9d, 0x18, 0xc8,
     0x4a, 0x3f, 0x47, 0x18, 0xe2, 0x44, 0x8b, 0x2f, 0xe3, 0x24, 0xd9, 0xcc, 0xda, 0x27, 0x10},
    {0x0f, 0x10, 0xf5, 0x99, 0xae, 0x14, 0xa1, 0x54, 0xed, 0x24, 0xb3, 0x6e, 0x25, 0x32, 0x4d,
     0xb8, 0xc5, 0x66, 0x63, 0x2e, 0xf2, 0xbb, 0xb3, 0x4f, 0x83, 0x47, 0x28, 0x0f, 0xc4, 0x50,
     0x70, 0x57, 0xfd, 0xdc, 0x29, 0xdf, 0x9a, 0x47, 0x1f, 0x75, 0xc6, 0x65, 0x41, 0xd4, 0xd4,
     0xda, 0xd1, 0xc9, 0xe9, 0x3a, 0x19, 0xa5, 0x8e, 0x8b, 0x47, 0x3f, 0xa0, 0xf0, 0x62, 0xf7},
    {0xd2, 0x7e, 0x88, 0x68, 0x1c, 0xe3, 0x24, 0x3c, 0x48, 0x30, 0x16, 0x5a, 0x8f, 0xdc, 0xf9,
     0xff, 0x1d, 0xe9, 0xa1, 0xd8, 0xe6, 0xb4, 0x47, 0xef, 0x6e, 0xf7, 0xb7, 0x98, 0x28, 0x66,
     0x6e, 0x45, 0x81, 0xe7, 0x90, 0x12, 0xaf, 0x34, 0xdd, 0xd9, 0xe2, 0xf0, 0x37, 0x58, 0x9b,
     0x29, 0x2d, 0xb3, 0xe6, 0x7c, 0x03, 0x67, 0x45, 0xfa, 0x22, 0xe7, 0xe9, 0xb7, 0x37, 0x3b},
    {0x00},
    {0xce, 0xa7, 0x40, 0x3d, 0x4d, 0x60, 0x6b, 0x6e, 0x07, 0x4e, 0xc5, 0xd3, 0xba, 0xf3, 0x9d, 0x18},
    {0x52, 0x2d, 0xc1, 0xf0, 0x99, 0x56, 0x7d, 0x07, 0xf4, 0x7f, 0x37, 0xa3, 0x2a, 0x84, 0x42, 0x7d,
     0x64, 0x3a, 0x8c, 0xdc, 0xbf, 0xe5, 0xc0, 0xc9, 0x75, 0x98, 0xa2, 0xbd, 0x25, 0x55, 0xd1, 0xaa,
     0x8c, 0xb0, 0x8e, 0x48, 0x59, 0x0d, 0xbb, 0x3d, 0xa7, 0xb0, 0x8b, 0x10, 0x56, 0x82, 0x88, 0x38,
     0xc5, 0xf6, 0x1e, 0x63, 0x93, 0xba, 0x7a, 0x0a, 0xbc, 0xc9, 0xf6, 0x62, 0x89, 0x80, 0x15, 0xad},
    {0x52, 0x2d, 0xc1, 0xf0, 0x99, 0x56, 0x7d, 0x07, 0xf4, 0x7f, 0x37, 0xa3, 0x2a, 0x84, 0x42,
     0x7d, 0x64, 0x3a, 0x8c, 0xdc, 0xbf, 0xe5, 0xc0, 0xc9, 0x75, 0x98, 0xa2, 0xbd, 0x25, 0x55,
     0xd1, 0xaa, 0x8c, 0xb0, 0x8e, 0x48, 0x59, 0x0d, 0xbb, 0x3d, 0xa7, 0xb0, 0x8b, 0x10, 0x56,
     0x82, 0x88, 0x38, 0xc5, 0xf6, 0x1e, 0x63, 0x93, 0xba, 0x7a, 0x0a, 0xbc, 0xc9, 0xf6, 0x62},
    {0xc3, 0x76, 0x2d, 0xf1, 0xca, 0x78, 0x7d, 0x32, 0xae, 0x47, 0xc1, 0x3b, 0xf1, 0x98, 0x44,
     0xcb, 0xaf, 0x1a, 0xe1, 0x4d, 0x0b, 0x97, 0x6a, 0xfa, 0xc5, 0x2f, 0xf7, 0xd7, 0x9b, 0xba,
     0x9d, 0xe0, 0xfe, 0xb5, 0x82, 0xd3, 0x39, 0x34, 0xa4, 0xf0, 0x95, 0x4c, 0xc2, 0x36, 0x3b,
     0xc7, 0x3f, 0x78, 0x62, 0xac, 0x43, 0x0e, 0x64, 0xab, 0xe4, 0x99, 0xf4, 0x7c, 0x9b, 0x1f},
    {0x5a, 0x8d, 0xef, 0x2f, 0x0c, 0x9e, 0x53, 0xf1, 0xf7, 0x5d, 0x78, 0x53, 0x65, 0x9e, 0x2a,
     0x20, 0xee, 0xb2, 0xb2, 0x2a, 0xaf, 0xde, 0x64, 0x19, 0xa0, 0x58, 0xab, 0x4f, 0x6f, 0x74,
     0x6b, 0xf4, 0x0f, 0xc0, 0xc3, 0xb7, 0x80, 0xf2, 0x44, 0x45, 0x2d, 0xa3, 0xeb, 0xf1, 0xc5,
     0xd8, 0x2c, 0xde, 0xa2, 0x41, 0x89, 0x97, 0x20, 0x0e, 0xf8, 0x2e, 0x44, 0xae, 0x7e, 0x3f},
};

static const unsigned char tag_test_data[MAX_TESTS * 3][16] = {
    {0x58, 0xe2, 0xfc, 0xce, 0xfa, 0x7e, 0x30, 0x61, 0x36, 0x7f, 0x1d, 0x57, 0xa4, 0xe7, 0x45, 0x5a},
    {0xab, 0x6e, 0x47, 0xd4, 0x2c, 0xec, 0x13, 0xbd, 0xf5, 0x3a, 0x67, 0xb2, 0x12, 0x57, 0xbd, 0xdf},
    {0x4d, 0x5c, 0x2a, 0xf3, 0x27, 0xcd, 0x64, 0xa6, 0x2c, 0xf3, 0x5a, 0xbd, 0x2b, 0xa6, 0xfa, 0xb4},
    {0x5b, 0xc9, 0x4f, 0xbc, 0x32, 0x21, 0xa5, 0xdb, 0x94, 0xfa, 0xe9, 0x5a, 0xe7, 0x12, 0x1a, 0x47},
    {0x36, 0x12, 0xd2, 0xe7, 0x9e, 0x3b, 0x07, 0x85, 0x56, 0x1b, 0xe1, 0x4a, 0xac, 0xa2, 0xfc, 0xcb},
    {0x61, 0x9c, 0xc5, 0xae, 0xff, 0xfe, 0x0b, 0xfa, 0x46, 0x2a, 0xf4, 0x3c, 0x16, 0x99, 0xd0, 0x50},
    {0xcd, 0x33, 0xb2, 0x8a, 0xc7, 0x73, 0xf7, 0x4b, 0xa0, 0x0e, 0xd1, 0xf3, 0x12, 0x57, 0x24, 0x35},
    {0x2f, 0xf5, 0x8d, 0x80, 0x03, 0x39, 0x27, 0xab, 0x8e, 0xf4, 0xd4, 0x58, 0x75, 0x14, 0xf0, 0xfb},
    {0x99, 0x24, 0xa7, 0xc8, 0x58, 0x73, 0x36, 0xbf, 0xb1, 0x18, 0x02, 0x4d, 0xb8, 0x67, 0x4a, 0x14},
    {0x25, 0x19, 0x49, 0x8e, 0x80, 0xf1, 0x47, 0x8f, 0x37, 0xba, 0x55, 0xbd, 0x6d, 0x27, 0x61, 0x8c},
    {0x65, 0xdc, 0xc5, 0x7f, 0xcf, 0x62, 0x3a, 0x24, 0x09, 0x4f, 0xcc, 0xa4, 0x0d, 0x35, 0x33, 0xf8},
    {0xdc, 0xf5, 0x66, 0xff, 0x29, 0x1c, 0x25, 0xbb, 0xb8, 0x56, 0x8f, 0xc3, 0xd3, 0x76, 0xa6, 0xd9},
    {0x53, 0x0f, 0x8a, 0xfb, 0xc7, 0x45, 0x36, 0xb9, 0xa9, 0x63, 0xb4, 0xf1, 0xc4, 0xcb, 0x73, 0x8b},
    {0xd0, 0xd1, 0xc8, 0xa7, 0x99, 0x99, 0x6b, 0xf0, 0x26, 0x5b, 0x98, 0xb5, 0xd4, 0x8a, 0xb9, 0x19},
    {0xb0, 0x94, 0xda, 0xc5, 0xd9, 0x34, 0x71, 0xbd, 0xec, 0x1a, 0x50, 0x22, 0x70, 0xe3, 0xcc, 0x6c},
    {0x76, 0xfc, 0x6e, 0xce, 0x0f, 0x4e, 0x17, 0x68, 0xcd, 0xdf, 0x88, 0x53, 0xbb, 0x2d, 0x55, 0x1b},
    {0x3a, 0x33, 0x7d, 0xbf, 0x46, 0xa7, 0x92, 0xc4, 0x5e, 0x45, 0x49, 0x13, 0xfe, 0x2e, 0xa8, 0xf2},
    {0xa4, 0x4a, 0x82, 0x66, 0xee, 0x1c, 0x8e, 0xb0, 0xc8, 0xb5, 0xd4, 0xcf, 0x5a, 0xe9, 0xf1, 0x9a},
};

/* Alternate test implementation to accomodate platform restrictions */
int mbedtls_gcm_self_test_alt(int verbose)
{
    mbedtls_gcm_context ctx;
    unsigned char buf[64];
    unsigned char tag_buf[16];
    int i, j, ret;
    mbedtls_cipher_id_t cipher = MBEDTLS_CIPHER_ID_AES;
    size_t olen;

    for (j = 0; j < 3; j++)
    {
        int key_len = 128 + 64 * j;

        for (i = 0; i < MAX_TESTS; i++)
        {
            mbedtls_gcm_init(&ctx);

            if (verbose != 0)
                mbedtls_printf("  AES-GCM-%3d #%d (%s): ", key_len, i, "enc");

            ret = mbedtls_gcm_setkey(&ctx, cipher, key_test_data[key_index_test_data[i]], key_len);
            /*
             * AES-192 is an optional feature that may be unavailable when
             * there is an alternative underlying implementation i.e. when
             * MBEDTLS_AES_ALT is defined.
             */
            if (ret == MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED && key_len == 192)
            {
                mbedtls_printf("skipped\n");
                break;
            }
            else if (ret != 0)
            {
                goto exit;
            }

            ret = mbedtls_gcm_crypt_and_tag(&ctx,
                                            MBEDTLS_GCM_ENCRYPT,
                                            pt_len_test_data[i],
                                            iv_test_data[iv_index_test_data[i]],
                                            iv_len_test_data[i],
                                            additional_test_data[add_index_test_data[i]],
                                            add_len_test_data[i],
                                            pt_test_data[pt_index_test_data[i]],
                                            buf,
                                            16,
                                            tag_buf);
        #if defined(MBEDTLS_GCM_ALT)
            if (ret == MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED)
            {
                /* Allow alternative implementations to only support 12-byte nonces. */
                /* Allow AESGCM driver limitation if input_length = 0 and aad_length = 0 */
                if ((iv_len_test_data[i] != 12) || (add_len_test_data[i] == 0 && pt_len_test_data[i] == 0))
                {
                    mbedtls_printf("not supported\n");
                    mbedtls_gcm_free(&ctx);
                    continue;
                }
            }
        #endif /* defined(MBEDTLS_GCM_ALT) */
            if (ret != 0)
                goto exit;

            if (memcmp(buf, ct_test_data[j * 6 + i], pt_len_test_data[i]) != 0 ||
                memcmp(tag_buf, tag_test_data[j * 6 + i], 16) != 0)
            {
                ret = 1;
                goto exit;
            }

            mbedtls_gcm_free(&ctx);

            if (verbose != 0)
                mbedtls_printf("passed\n");

            mbedtls_gcm_init(&ctx);

            if (verbose != 0)
                mbedtls_printf("  AES-GCM-%3d #%d (%s): ", key_len, i, "dec");

            ret = mbedtls_gcm_setkey(&ctx, cipher, key_test_data[key_index_test_data[i]], key_len);
            if (ret != 0)
                goto exit;

            memcpy(tag_buf, tag_test_data[j * 6 + i], 16);

            ret = mbedtls_gcm_auth_decrypt(&ctx,
                                           pt_len_test_data[i],
                                           iv_test_data[iv_index_test_data[i]],
                                           iv_len_test_data[i],
                                           additional_test_data[add_index_test_data[i]],
                                           add_len_test_data[i],
                                           tag_buf,
                                           16,
                                           ct_test_data[j * 6 + i],
                                           buf);

            if (ret != 0)
                goto exit;

            if (memcmp(buf, pt_test_data[pt_index_test_data[i]], pt_len_test_data[i]) != 0)
            {
                ret = 1;
                goto exit;
            }

            mbedtls_gcm_free(&ctx);

            if (verbose != 0)
                mbedtls_printf("passed\n");

            mbedtls_gcm_init(&ctx);

            if (verbose != 0)
                mbedtls_printf("  AES-GCM-%3d #%d split (%s): ", key_len, i, "enc");

            ret = mbedtls_gcm_setkey(&ctx, cipher, key_test_data[key_index_test_data[i]], key_len);
            if (ret != 0)
                goto exit;

            ret = mbedtls_gcm_starts(&ctx,
                                     MBEDTLS_GCM_ENCRYPT,
                                     iv_test_data[iv_index_test_data[i]],
                                     iv_len_test_data[i]);
            if (ret != 0)
                goto exit;

            /* Required for platform */
            ret = mbedtls_gcm_set_lengths(&ctx, add_len_test_data[i], pt_len_test_data[i]);

            if (ret != 0)
                goto exit;

            ret = mbedtls_gcm_update_ad(&ctx, additional_test_data[add_index_test_data[i]], add_len_test_data[i]);
            if (ret != 0)
                goto exit;

            if (pt_len_test_data[i] > 32)
            {
                size_t rest_len = pt_len_test_data[i] - 32;
                ret = mbedtls_gcm_update(&ctx, pt_test_data[pt_index_test_data[i]], 32, buf, sizeof(buf), &olen);
                if (ret != 0)
                    goto exit;
                if (olen != 32)
                    goto exit;

                ret = mbedtls_gcm_update(&ctx,
                                         pt_test_data[pt_index_test_data[i]] + 32,
                                         rest_len,
                                         buf + 32,
                                         sizeof(buf) - 32,
                                         &olen);
                if (ret != 0)
                    goto exit;
                if (olen != rest_len)
                    goto exit;
            }
            else
            {
                ret = mbedtls_gcm_update(&ctx,
                                         pt_test_data[pt_index_test_data[i]],
                                         pt_len_test_data[i],
                                         buf,
                                         sizeof(buf),
                                         &olen);
                if (ret != 0)
                    goto exit;
                if (olen != pt_len_test_data[i])
                    goto exit;
            }

            ret = mbedtls_gcm_finish(&ctx, NULL, 0, &olen, tag_buf, 16);
            if (ret != 0)
                goto exit;

            if (memcmp(buf, ct_test_data[j * 6 + i], pt_len_test_data[i]) != 0 ||
                memcmp(tag_buf, tag_test_data[j * 6 + i], 16) != 0)
            {
                ret = 1;
                goto exit;
            }

            mbedtls_gcm_free(&ctx);

            if (verbose != 0)
                mbedtls_printf("passed\n");

            mbedtls_gcm_init(&ctx);

            if (verbose != 0)
                mbedtls_printf("  AES-GCM-%3d #%d split (%s): ", key_len, i, "dec");

            ret = mbedtls_gcm_setkey(&ctx, cipher, key_test_data[key_index_test_data[i]], key_len);
            if (ret != 0)
                goto exit;

            ret = mbedtls_gcm_starts(&ctx,
                                     MBEDTLS_GCM_DECRYPT,
                                     iv_test_data[iv_index_test_data[i]],
                                     iv_len_test_data[i]);
            if (ret != 0)
                goto exit;

            /* Required for platform */
            ret = mbedtls_gcm_set_lengths(&ctx, add_len_test_data[i], pt_len_test_data[i]);

            if (ret != 0)
                goto exit;

            ret = mbedtls_gcm_update_ad(&ctx, additional_test_data[add_index_test_data[i]], add_len_test_data[i]);
            if (ret != 0)
                goto exit;

            if (pt_len_test_data[i] > 32)
            {
                size_t rest_len = pt_len_test_data[i] - 32;
                ret             = mbedtls_gcm_update(&ctx, ct_test_data[j * 6 + i], 32, buf, sizeof(buf), &olen);
                if (ret != 0)
                    goto exit;
                if (olen != 32)
                    goto exit;

                ret = mbedtls_gcm_update(&ctx,
                                         ct_test_data[j * 6 + i] + 32,
                                         rest_len,
                                         buf + 32,
                                         sizeof(buf) - 32,
                                         &olen);
                if (ret != 0)
                    goto exit;
                if (olen != rest_len)
                    goto exit;
            }
            else
            {
                ret = mbedtls_gcm_update(&ctx, ct_test_data[j * 6 + i], pt_len_test_data[i], buf, sizeof(buf), &olen);
                if (ret != 0)
                    goto exit;
                if (olen != pt_len_test_data[i])
                    goto exit;
            }

            ret = mbedtls_gcm_finish(&ctx, NULL, 0, &olen, tag_buf, 16);
            if (ret != 0)
                goto exit;

            if (memcmp(buf, pt_test_data[pt_index_test_data[i]], pt_len_test_data[i]) != 0 ||
                memcmp(tag_buf, tag_test_data[j * 6 + i], 16) != 0)
            {
                ret = 1;
                goto exit;
            }

            mbedtls_gcm_free(&ctx);

            if (verbose != 0)
                mbedtls_printf("passed\n");
        }
    }

    if (verbose != 0)
        mbedtls_printf("\n");

    ret = 0;

exit:
    if (ret != 0)
    {
        if (verbose != 0)
            mbedtls_printf("failed\n");
        mbedtls_gcm_free(&ctx);
    }

    return (ret);
}

#endif /* MBEDTLS_SELF_TEST && MBEDTLS_AES_C */

#endif /* MBEDTLS_GCM_ALT */
