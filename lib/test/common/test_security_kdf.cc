/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2021 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "srsran/common/bcd_helpers.h"
#include "srsran/common/common.h"
#include "srsran/common/security.h"
#include "srsran/common/test_common.h"
#include "srsran/srslog/srslog.h"

using namespace srsran;

int arrcmp(uint8_t const* const a, uint8_t const* const b, uint32_t len)
{
  uint32_t i = 0;

  for (i = 0; i < len; i++) {
    if (a[i] != b[i]) {
      return a[i] - b[i];
    }
  }
  return SRSRAN_SUCCESS;
}

int test_set_ksg()
{
  auto& logger  = srslog::fetch_basic_logger("LOG", false);
  int   err_lte = SRSRAN_ERROR;
  int   err_cmp = SRSRAN_ERROR;

  uint8_t  k_enb[] = {0xfe, 0x7d, 0xee, 0x80, 0x8d, 0x7f, 0x3b, 0x88, 0x2a, 0x08, 0x2c, 0xbd, 0xc8, 0x39, 0x0d, 0x12,
                     0x9e, 0x5d, 0x28, 0xaf, 0x0e, 0x83, 0x22, 0xeb, 0x57, 0x3a, 0xda, 0x36, 0xf2, 0x1a, 0x5a, 0x89};
  uint8_t  sk_gnb_o[32];
  uint16_t scg_counter = 0;
  err_lte              = srsran::security_generate_sk_gnb(k_enb, sk_gnb_o, scg_counter);
  TESTASSERT(err_lte == SRSRAN_SUCCESS);
  logger.info(&sk_gnb_o[0], sizeof(sk_gnb_o), "sk gnb o:");
  uint8_t sk_gnb[] = {0x45, 0xcb, 0xc3, 0xf8, 0xa8, 0x11, 0x93, 0xfd, 0x5c, 0x52, 0x29, 0x30, 0x0d, 0x59, 0xed, 0xf8,
                      0x12, 0xe9, 0x98, 0xa1, 0x15, 0xec, 0x4e, 0x0c, 0xe9, 0x03, 0xba, 0x89, 0x36, 0x7e, 0x26, 0x28};
  err_cmp          = arrcmp(sk_gnb_o, sk_gnb, sizeof(sk_gnb));
  TESTASSERT(err_cmp == 0);
  return SRSRAN_SUCCESS;
}

int test_set_nr_rrc_up()
{
  auto& logger  = srslog::fetch_basic_logger("LOG", false);
  int   err_lte = SRSRAN_ERROR;
  int   err_cmp = SRSRAN_ERROR;

  uint8_t sk_gnb[] = {0x45, 0xcb, 0xc3, 0xf8, 0xa8, 0x11, 0x93, 0xfd, 0x5c, 0x52, 0x29, 0x30, 0x0d, 0x59, 0xed, 0xf8,
                      0x12, 0xe9, 0x98, 0xa1, 0x15, 0xec, 0x4e, 0x0c, 0xe9, 0x03, 0xba, 0x89, 0x36, 0x7e, 0x26, 0x28};

  uint8_t sk_gnb_o[32];
  uint8_t k_rrc_enc_o[32];
  uint8_t k_rrc_int_o[32];

  err_lte = srsran::security_generate_k_nr_rrc(sk_gnb,
                                               srsran::CIPHERING_ALGORITHM_ID_ENUM::CIPHERING_ALGORITHM_ID_128_EEA2,
                                               srsran::INTEGRITY_ALGORITHM_ID_ENUM::INTEGRITY_ALGORITHM_ID_EIA0,
                                               k_rrc_enc_o,
                                               k_rrc_int_o);

  TESTASSERT(err_lte == SRSRAN_SUCCESS);
  logger.info(&k_rrc_enc_o[0], sizeof(k_rrc_enc_o), "RRC ENC output:");

  uint8_t k_rrc_enc[] = {0x52, 0xa9, 0x95, 0xdf, 0xf8, 0x9b, 0xc2, 0x94, 0xbd, 0x89, 0xff,
                         0xb1, 0x37, 0xa2, 0x9f, 0x24, 0x66, 0xa0, 0x9e, 0x99, 0x23, 0x86,
                         0xc8, 0xd1, 0xdf, 0x78, 0x92, 0x96, 0x4c, 0x6f, 0xb5, 0x22};

  err_cmp = arrcmp(k_rrc_enc_o, k_rrc_enc, sizeof(k_rrc_enc_o));

  TESTASSERT(err_cmp == 0);

  uint8_t k_up_enc_o[32];
  uint8_t k_up_int_o[32];

  err_lte = srsran::security_generate_k_nr_up(sk_gnb,
                                              srsran::CIPHERING_ALGORITHM_ID_ENUM::CIPHERING_ALGORITHM_ID_128_EEA2,
                                              srsran::INTEGRITY_ALGORITHM_ID_ENUM::INTEGRITY_ALGORITHM_ID_EIA0,
                                              k_up_enc_o,
                                              k_up_int_o);

  uint8_t k_up_enc[] = {0x7c, 0xe2, 0x06, 0x70, 0xbb, 0xbc, 0xc5, 0x90, 0x40, 0x87, 0xc0, 0xd4, 0x26, 0x53, 0xc5, 0x40,
                        0x15, 0x20, 0x52, 0xd3, 0xdf, 0xbc, 0x3f, 0x05, 0x86, 0x9b, 0x7f, 0x92, 0x00, 0x95, 0xbe, 0x68};

  logger.info(&k_up_enc_o[0], sizeof(k_up_enc_o), "UP ENC output:");

  err_cmp = arrcmp(k_up_enc_o, k_up_enc, sizeof(k_up_enc_o));
  TESTASSERT(err_cmp == 0);
  return SRSRAN_SUCCESS;
}

int test_generate_k_asme()
{
  auto& logger = srslog::fetch_basic_logger("LOG", false);

  uint8_t  k_asme_o[32] = {};
  uint8_t  ck[]  = {0x7b, 0xc1, 0x5d, 0x69, 0x30, 0x9c, 0xf3, 0xec, 0x5d, 0x32, 0x44, 0x04, 0xed, 0xd6, 0xf0, 0xf9};
  uint8_t  ik[]  = {0xc1, 0x5d, 0x69, 0x30, 0x9c, 0xf3, 0xec, 0x5d, 0x32, 0x44, 0x04, 0xed, 0xd6, 0xf0, 0xf9, 0x7b};
  uint8_t  ak[]  = {0x5d, 0x69, 0x30, 0x9c, 0xf3, 0xec};
  uint8_t  sqn[] = {0x00, 0x00, 0x00, 0x00, 0x12, 0xd9};
  uint16_t mcc   = 61441; // MCC 001
  uint16_t mnc   = 65281; // MNC 01

  // Generate K_asme
  security_generate_k_asme(ck, ik, ak, sqn, mcc, mnc, k_asme_o);

  uint8_t k_asme[] = {0xd5, 0xef, 0x4d, 0x8f, 0x33, 0x26, 0x69, 0x02, 0x29, 0x5d, 0x42, 0xf3, 0x22, 0xa2, 0xf2, 0xcf,
                      0x11, 0xfb, 0x2c, 0xcc, 0x12, 0x4c, 0x09, 0xb4, 0xd8, 0x8d, 0x36, 0x15, 0x97, 0x03, 0x79, 0x90};

  logger.info(k_asme_o, 32, "K_ASME output:");
  TESTASSERT(arrcmp(k_asme, k_asme_o, sizeof(k_asme_o)) == 0);

  return SRSRAN_SUCCESS;
}

int test_generate_k_nas()
{
  auto&   logger = srslog::fetch_basic_logger("LOG", false);
  uint8_t k_nas_enc_o[32];
  uint8_t k_nas_int_o[32];

  uint8_t k_asme[] = {0xd5, 0xef, 0x4d, 0x8f, 0x33, 0x26, 0x69, 0x02, 0x29, 0x5d, 0x42, 0xf3, 0x22, 0xa2, 0xf2, 0xcf,
                      0x11, 0xfb, 0x2c, 0xcc, 0x12, 0x4c, 0x09, 0xb4, 0xd8, 0x8d, 0x36, 0x15, 0x97, 0x03, 0x79, 0x90};

  TESTASSERT(srsran::security_generate_k_nas(k_asme,
                                             srsran::CIPHERING_ALGORITHM_ID_ENUM::CIPHERING_ALGORITHM_ID_EEA0,
                                             srsran::INTEGRITY_ALGORITHM_ID_ENUM::INTEGRITY_ALGORITHM_ID_128_EIA1,
                                             k_nas_enc_o,
                                             k_nas_int_o) == SRSRAN_SUCCESS);

  uint8_t k_nas_enc[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  logger.info(k_nas_enc_o, 32, "K NAS ENC output:");
  TESTASSERT(arrcmp(k_nas_enc, k_nas_enc_o, sizeof(k_nas_enc_o)) == 0);

  uint8_t k_nas_int[] = {0xb8, 0x55, 0x87, 0x1d, 0x17, 0xd4, 0xa7, 0x17, 0xe9, 0x31, 0xd9,
                         0xa6, 0x06, 0x9c, 0x6e, 0x54, 0xb7, 0x94, 0x08, 0xe2, 0x43, 0xdd,
                         0x36, 0x7d, 0xc8, 0xc6, 0x39, 0x22, 0x95, 0xb3, 0xb4, 0x51};

  logger.info(k_nas_int_o, 32, "K NAS INT output:");
  TESTASSERT(arrcmp(k_nas_int, k_nas_int_o, sizeof(k_nas_int_o)) == 0);

  return SRSRAN_SUCCESS;
}

int test_generate_k_enb()
{
  auto&   logger = srslog::fetch_basic_logger("LOG", false);
  uint8_t k_enb_o[32];

  uint8_t  k_asme[] = {0x61, 0x44, 0xc6, 0x81, 0xd1, 0xbe, 0xa9, 0xda, 0xe1, 0xb8, 0xcf, 0x6c, 0xd1, 0x0a, 0x68, 0x63,
                      0x41, 0xdb, 0x80, 0x46, 0xa1, 0xe7, 0xa9, 0xab, 0x4d, 0x1e, 0xa0, 0xe3, 0x3c, 0x99, 0x4a, 0xc0};
  uint32_t nas_ul_count = 0;

  TESTASSERT(srsran::security_generate_k_enb(k_asme, nas_ul_count, k_enb_o) == SRSRAN_SUCCESS);

  uint8_t k_enb[] = {0xc4, 0xc7, 0xbc, 0x79, 0x8a, 0xb9, 0x4e, 0x3d, 0x35, 0x4c, 0xd6, 0x60, 0x8e, 0x79, 0xaa, 0x92,
                     0xf5, 0x56, 0x9d, 0xf4, 0x65, 0x19, 0x50, 0x78, 0x50, 0x05, 0x1e, 0x36, 0xf0, 0x18, 0xca, 0x5f};

  logger.info(k_enb, 32, "K ENB output:");
  TESTASSERT(arrcmp(k_enb, k_enb_o, sizeof(k_enb_o)) == 0);

  return SRSRAN_SUCCESS;
}

int test_generate_rrc_keys()
{
  auto&   logger = srslog::fetch_basic_logger("LOG", false);
  uint8_t k_rrc_enc_o[32];
  uint8_t k_rrc_int_o[32];

  uint8_t k_enb[] = {0xc4, 0xc7, 0xbc, 0x79, 0x8a, 0xb9, 0x4e, 0x3d, 0x35, 0x4c, 0xd6, 0x60, 0x8e, 0x79, 0xaa, 0x92,
                     0xf5, 0x56, 0x9d, 0xf4, 0x65, 0x19, 0x50, 0x78, 0x50, 0x05, 0x1e, 0x36, 0xf0, 0x18, 0xca, 0x5f};

  TESTASSERT(srsran::security_generate_k_rrc(k_enb,
                                             srsran::CIPHERING_ALGORITHM_ID_ENUM::CIPHERING_ALGORITHM_ID_EEA0,
                                             srsran::INTEGRITY_ALGORITHM_ID_ENUM::INTEGRITY_ALGORITHM_ID_128_EIA2,
                                             k_rrc_enc_o,
                                             k_rrc_int_o) == SRSRAN_SUCCESS);

  uint8_t k_rrc_enc[] = {0x23, 0xaf, 0xdd, 0x7b, 0x2e, 0x4a, 0x5a, 0x99, 0xc7, 0x78, 0x82,
                         0x78, 0x59, 0xcf, 0x45, 0x14, 0x86, 0xa2, 0x75, 0x78, 0x8b, 0x6f,
                         0x36, 0xa5, 0xb9, 0xb8, 0x10, 0xf5, 0xd4, 0x72, 0xa2, 0x4b};

  logger.info(k_rrc_enc_o, 32, "K RRC ENC output:");
  TESTASSERT(arrcmp(k_rrc_enc, k_rrc_enc_o, sizeof(k_rrc_enc_o)) == 0);

  uint8_t k_rrc_int[] = {0x42, 0xf4, 0xf8, 0xc5, 0x85, 0xae, 0x89, 0x05, 0xa5, 0x0d, 0x0f,
                         0x32, 0xa9, 0x79, 0xd8, 0xb6, 0xfa, 0x1d, 0x6d, 0x19, 0x40, 0xf1,
                         0xd8, 0x79, 0xcf, 0x01, 0x89, 0x34, 0x2a, 0x8d, 0x73, 0xc2};

  logger.info(k_rrc_int_o, 32, "K RRC INT output:");
  TESTASSERT(arrcmp(k_rrc_int, k_rrc_int_o, sizeof(k_rrc_int_o)) == 0);

  return SRSRAN_SUCCESS;
}

int test_generate_up_keys()
{
  auto&   logger = srslog::fetch_basic_logger("LOG", false);
  uint8_t k_up_enc_o[32];
  uint8_t k_up_int_o[32];

  uint8_t k_enb[] = {0xc4, 0xc7, 0xbc, 0x79, 0x8a, 0xb9, 0x4e, 0x3d, 0x35, 0x4c, 0xd6, 0x60, 0x8e, 0x79, 0xaa, 0x92,
                     0xf5, 0x56, 0x9d, 0xf4, 0x65, 0x19, 0x50, 0x78, 0x50, 0x05, 0x1e, 0x36, 0xf0, 0x18, 0xca, 0x5f};

  TESTASSERT(srsran::security_generate_k_up(k_enb,
                                            srsran::CIPHERING_ALGORITHM_ID_ENUM::CIPHERING_ALGORITHM_ID_EEA0,
                                            srsran::INTEGRITY_ALGORITHM_ID_ENUM::INTEGRITY_ALGORITHM_ID_128_EIA2,
                                            k_up_enc_o,
                                            k_up_int_o) == SRSRAN_SUCCESS);

  uint8_t k_up_enc[] = {0x22, 0xbf, 0xb5, 0x87, 0x61, 0xca, 0x1d, 0xd3, 0xb2, 0x0a, 0x28, 0x1c, 0x7e, 0xab, 0x0c, 0x0b,
                        0x9c, 0x3c, 0x92, 0xe1, 0xdd, 0xc0, 0xc8, 0xc5, 0x70, 0x6c, 0xbb, 0x8f, 0x95, 0x5e, 0x82, 0x63};

  logger.info(k_up_enc_o, 32, "K UP ENC output:");
  TESTASSERT(arrcmp(k_up_enc, k_up_enc_o, sizeof(k_up_enc_o)) == 0);

  uint8_t k_up_int[] = {0x82, 0x0d, 0xcd, 0xf3, 0xb9, 0xc9, 0x4b, 0x32, 0xf8, 0x41, 0xc2, 0x5c, 0xc8, 0x78, 0xaa, 0x07,
                        0x77, 0x16, 0xc7, 0x83, 0xa5, 0x3f, 0xd3, 0xee, 0x58, 0x2f, 0xc5, 0x69, 0xe9, 0xc3, 0x3c, 0xa7};

  logger.info(k_up_int_o, 32, "K UP INT output:");
  TESTASSERT(arrcmp(k_up_int, k_up_int_o, sizeof(k_up_int_o)) == 0);

  return SRSRAN_SUCCESS;
}

int main(int argc, char** argv)
{
  auto& logger = srslog::fetch_basic_logger("LOG", false);
  logger.set_level(srslog::basic_levels::debug);
  logger.set_hex_dump_max_size(256);

  srslog::init();

  TESTASSERT(test_generate_k_asme() == SRSRAN_SUCCESS);
  TESTASSERT(test_generate_k_nas() == SRSRAN_SUCCESS);
  TESTASSERT(test_generate_k_enb() == SRSRAN_SUCCESS);
  TESTASSERT(test_generate_rrc_keys() == SRSRAN_SUCCESS);
  TESTASSERT(test_generate_up_keys() == SRSRAN_SUCCESS);

  TESTASSERT(test_set_ksg() == SRSRAN_SUCCESS);
  TESTASSERT(test_set_nr_rrc_up() == SRSRAN_SUCCESS);

  return SRSRAN_SUCCESS;
}
