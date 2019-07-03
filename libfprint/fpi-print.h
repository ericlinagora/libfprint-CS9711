#pragma once

#include "fpi-enums.h"
#include "fp-device.h"
#include "fp-print.h"

G_BEGIN_DECLS

/**
 * FpPrintType:
 * @FP_PRINT_UNDEFINED: Undefined type, this happens prior to enrollment
 * @FP_PRINT_RAW: A raw print where the data is directly compared
 * @FP_PRINT_NBIS: NBIS minutiae comparison
 */
typedef enum {
  FP_PRINT_UNDEFINED = 0,
  FP_PRINT_RAW,
  FP_PRINT_NBIS,
} FpPrintType;

/**
 * FpiMatchResult:
 * @FPI_MATCH_ERROR: An error occured during matching
 * @FPI_MATCH_SUCCESS: The prints matched
 * @FPI_MATCH_FAIL: The prints did not match
 */
typedef enum {
  FPI_MATCH_ERROR = 0,
  FPI_MATCH_SUCCESS,
  FPI_MATCH_FAIL,
} FpiMatchResult;

void     fpi_print_add_print (FpPrint *print,
                              FpPrint *add);

void     fpi_print_set_type (FpPrint    *print,
                             FpPrintType type);
void     fpi_print_set_device_stored (FpPrint *print,
                                      gboolean device_stored);

gboolean fpi_print_add_from_image (FpPrint *print,
                                   FpImage *image,
                                   GError **error);

FpiMatchResult fpi_print_bz3_match (FpPrint * template,
                                    FpPrint *print,
                                    gint bz3_threshold,
                                    GError **error);

G_END_DECLS
