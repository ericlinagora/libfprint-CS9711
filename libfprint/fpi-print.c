/*
 * FPrint Print handling - Private APIs
 * Copyright (C) 2007 Daniel Drake <dsd@gentoo.org>
 * Copyright (C) 2019 Benjamin Berg <bberg@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define FP_COMPONENT "print"
#include "fpi-log.h"

#include "fp-print-private.h"
#include "fpi-device.h"

/**
 * SECTION: fpi-print
 * @title: Internal FpPrint
 * @short_description: Internal fingerprint handling routines
 *
 * Interaction with prints and their storage. See also the public
 * #FpPrint routines.
 */

/**
 * fpi_print_add_print:
 * @print: A #FpPrint
 * @add: Print to append to @print
 *
 * Appends the single #FPI_PRINT_NBIS print from @add to the collection of
 * prints in @print. Both print objects need to be of type #FPI_PRINT_NBIS
 * for this to work.
 */
void
fpi_print_add_print (FpPrint *print, FpPrint *add)
{
  g_return_if_fail (print->type == FPI_PRINT_NBIS);
  g_return_if_fail (add->type == FPI_PRINT_NBIS);

  g_assert (add->prints->len == 1);
  g_ptr_array_add (print->prints, g_memdup (add->prints->pdata[0], sizeof (struct xyt_struct)));
}

/**
 * fpi_print_set_type:
 * @print: A #FpPrint
 * @type: The newly type of the print data
 *
 * This function can only be called exactly once. Drivers should
 * call it after creating a new print, or to initialize the template
 * print passed during enrollment.
 */
void
fpi_print_set_type (FpPrint     *print,
                    FpiPrintType type)
{
  g_return_if_fail (FP_IS_PRINT (print));
  /* We only allow setting this once! */
  g_return_if_fail (print->type == FPI_PRINT_UNDEFINED);

  print->type = type;
  if (print->type == FPI_PRINT_NBIS)
    {
      g_assert_null (print->prints);
      print->prints = g_ptr_array_new_with_free_func (g_free);
    }
  g_object_notify (G_OBJECT (print), "fpi-type");
}

/**
 * fpi_print_set_device_stored:
 * @print: A #FpPrint
 * @device_stored: Whether the print is stored on the device or not
 *
 * Drivers must set this to %TRUE for any print that is really a handle
 * for data that is stored on the device itself.
 */
void
fpi_print_set_device_stored (FpPrint *print,
                             gboolean device_stored)
{
  g_return_if_fail (FP_IS_PRINT (print));

  print->device_stored = device_stored;
  g_object_notify (G_OBJECT (print), "device-stored");
}

/* XXX: This is the old version, but wouldn't it be smarter to instead
 * use the highest quality mintutiae? Possibly just using bz_prune from
 * upstream? */
static void
minutiae_to_xyt (struct fp_minutiae *minutiae,
                 int                 bwidth,
                 int                 bheight,
                 struct xyt_struct  *xyt)
{
  int i;
  struct fp_minutia *minutia;
  struct minutiae_struct c[MAX_FILE_MINUTIAE];

  /* struct xyt_struct uses arrays of MAX_BOZORTH_MINUTIAE (200) */
  int nmin = min (minutiae->num, MAX_BOZORTH_MINUTIAE);

  for (i = 0; i < nmin; i++)
    {
      minutia = minutiae->list[i];

      lfs2nist_minutia_XYT (&c[i].col[0], &c[i].col[1], &c[i].col[2],
                            minutia, bwidth, bheight);
      c[i].col[3] = sround (minutia->reliability * 100.0);

      if (c[i].col[2] > 180)
        c[i].col[2] -= 360;
    }

  qsort ((void *) &c, (size_t) nmin, sizeof (struct minutiae_struct),
         sort_x_y);

  for (i = 0; i < nmin; i++)
    {
      xyt->xcol[i]     = c[i].col[0];
      xyt->ycol[i]     = c[i].col[1];
      xyt->thetacol[i] = c[i].col[2];
    }
  xyt->nrows = nmin;
}

/**
 * fpi_print_add_from_image:
 * @print: A #FpPrint
 * @image: A #FpImage
 * @error: Return location for error
 *
 * Extracts the minutiae from the given image and adds it to @print of
 * type #FPI_PRINT_NBIS.
 *
 * The @image will be kept so that API users can get retrieve it e.g.
 * for debugging purposes.
 *
 * Returns: %TRUE on success
 */
gboolean
fpi_print_add_from_image (FpPrint *print,
                          FpImage *image,
                          GError **error)
{
  GPtrArray *minutiae;
  struct fp_minutiae _minutiae;
  struct xyt_struct *xyt;

  if (print->type != FPI_PRINT_NBIS || !image)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "Cannot add print data from image!");
      return FALSE;
    }

  minutiae = fp_image_get_minutiae (image);
  if (!minutiae || minutiae->len == 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "No minutiae found in image or not yet detected!");
      return FALSE;
    }

  _minutiae.num = minutiae->len;
  _minutiae.list = (struct fp_minutia **) minutiae->pdata;
  _minutiae.alloc = minutiae->len;

  xyt = g_new0 (struct xyt_struct, 1);
  minutiae_to_xyt (&_minutiae, image->width, image->height, xyt);
  g_ptr_array_add (print->prints, xyt);

  g_clear_object (&print->image);
  print->image = g_object_ref (image);
  g_object_notify (G_OBJECT (print), "image");

  return TRUE;
}

/**
 * fpi_print_bz3_match:
 * @template: A #FpPrint containing one or more prints
 * @print: A newly scanned #FpPrint to test
 * @bz3_threshold: The BZ3 match threshold
 * @error: Return location for error
 *
 * Match the newly scanned @print (containing exactly one print) against the
 * prints contained in @template which will have been stored during enrollment.
 *
 * Both @template and @print need to be of type #FPI_PRINT_NBIS for this to
 * work.
 *
 * Returns: Whether the prints match, @error will be set if #FPI_MATCH_ERROR is returned
 */
FpiMatchResult
fpi_print_bz3_match (FpPrint *template, FpPrint *print, gint bz3_threshold, GError **error)
{
  struct xyt_struct *pstruct;
  gint probe_len;
  gint i;

  /* XXX: Use a different error type? */
  if (template->type != FPI_PRINT_NBIS || print->type != FPI_PRINT_NBIS)
    {
      *error = fpi_device_error_new_msg (FP_DEVICE_ERROR_NOT_SUPPORTED,
                                         "It is only possible to match NBIS type print data");
      return FPI_MATCH_ERROR;
    }

  if (print->prints->len != 1)
    {
      *error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                         "New print contains more than one print!");
      return FPI_MATCH_ERROR;
    }

  pstruct = g_ptr_array_index (print->prints, 0);
  probe_len = bozorth_probe_init (pstruct);

  for (i = 0; i < template->prints->len; i++)
    {
      struct xyt_struct *gstruct;
      gint score;
      gstruct = g_ptr_array_index (template->prints, i);
      score = bozorth_to_gallery (probe_len, pstruct, gstruct);
      fp_dbg ("score %d", score);

      if (score >= bz3_threshold)
        return FPI_MATCH_SUCCESS;
    }

  return FPI_MATCH_FAIL;
}
