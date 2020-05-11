/*
 * FpSdcpDevice - A base class for SDCP enabled devices
 * Copyright (C) 2020 Benjamin Berg <bberg@redhat.com>
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

#define FP_COMPONENT "sdcp_device"
#include "fpi-log.h"

#include <sechash.h>
#include <cert.h>

#include "fp-sdcp-device-private.h"
#include "fpi-sdcp-device.h"
#include "fpi-print.h"

/**
 * SECTION: fpi-sdcp-device
 * @title: Internal FpSdcpDevice
 * @short_description: Internal SDCP Device routines
 *
 * Internal SDCP handling routines. See #FpSdcpDevice for public routines.
 */


G_DEFINE_BOXED_TYPE (FpiSdcpClaim, fpi_sdcp_claim, fpi_sdcp_claim_copy, fpi_sdcp_claim_free)

/**
 * fpi_sdcp_claim_new:
 *
 * Create an empty #FpiSdcpClaim to provide to the base class.
 *
 * Returns: (transfer full): A newly created #FpiSdcpClaim
 */
FpiSdcpClaim *
fpi_sdcp_claim_new (void)
{
  FpiSdcpClaim *res = NULL;

  res = g_new0 (FpiSdcpClaim, 1);

  return res;
}

/**
 * fpi_sdcp_claim_free:
 * @claim: a #FpiSdcpClaim
 *
 * Release the memory used by an #FpiSdcpClaim.
 */
void
fpi_sdcp_claim_free (FpiSdcpClaim * claim)
{
  g_return_if_fail (claim);

  g_clear_pointer (&claim->cert_m, g_bytes_unref);
  g_clear_pointer (&claim->pk_d, g_bytes_unref);
  g_clear_pointer (&claim->pk_f, g_bytes_unref);
  g_clear_pointer (&claim->h_f, g_bytes_unref);
  g_clear_pointer (&claim->s_m, g_bytes_unref);
  g_clear_pointer (&claim->s_d, g_bytes_unref);

  g_free (claim);
}

/**
 * fpi_sdcp_claim_copy:
 * @other: The #FpiSdcpClaim to copy
 *
 * Create a (shallow) copy of a #FpiSdcpClaim.
 *
 * Returns: (transfer full): A newly created #FpiSdcpClaim
 */
FpiSdcpClaim *
fpi_sdcp_claim_copy (FpiSdcpClaim *other)
{
  FpiSdcpClaim *res = NULL;

  res = fpi_sdcp_claim_new ();

  if (other->cert_m)
    res->cert_m = g_bytes_ref (other->cert_m);
  if (other->pk_d)
    res->pk_d = g_bytes_ref (other->pk_d);
  if (other->pk_f)
    res->pk_f = g_bytes_ref (other->pk_f);
  if (other->h_f)
    res->h_f = g_bytes_ref (other->h_f);
  if (other->s_m)
    res->s_m = g_bytes_ref (other->s_m);
  if (other->s_d)
    res->s_d = g_bytes_ref (other->s_d);

  return res;
}

static void
dump_bytes (GBytes *d)
{
  g_autoptr(GString) line = NULL;
  const guint8 *dump_data;
  gsize dump_len;

  dump_data = g_bytes_get_data (d, &dump_len);

  line = g_string_new ("");
  /* Dump the buffer. */
  for (gint i = 0; i < dump_len; i++)
    {
      g_string_append_printf (line, "%02x ", dump_data[i]);
      if ((i + 1) % 16 == 0)
        {
          g_debug ("%s", line->str);
          g_string_set_size (line, 0);
        }
    }

  if (line->len)
    g_debug ("%s", line->str);

}

/* Manually redefine what G_DEFINE_* macro does */
static inline gpointer
fp_sdcp_device_get_instance_private (FpSdcpDevice *self)
{
  FpSdcpDeviceClass *sdcp_class = g_type_class_peek_static (FP_TYPE_SDCP_DEVICE);

  return G_STRUCT_MEMBER_P (self,
                            g_type_class_get_instance_private_offset (sdcp_class));
}

/**
 * fpi_sdcp_kdf:
 * @self: The #FpSdcpDevice
 * @baseKey: The key to base it on
 * @data_a: (nullable): First data segment to concatenate
 * @data_b: (nullable): Second data segment to concatenate
 * @out_key_2: (nullable) (out): Second output key or %NULL.
 * @error: (out): #GError in case the return value is %NULL
 *
 * Convenience function to calculate a KDF with a specific label
 * and up to two data segments that are concatinated. The returned
 * keys will be 32bytes (256bit) in length. If @out_key_2 is set
 * then two keys will be generated.
 *
 * Returns: A new #PK11SymKey of length @bitlength
 **/
static PK11SymKey *
fpi_sdcp_kdf (FpSdcpDevice *self,
              PK11SymKey   *baseKey,
              const gchar  *label,
              GBytes       *data_a,
              GBytes       *data_b,
              PK11SymKey  **out_key_2,
              GError      **error)
{
  PK11SymKey * res = NULL;
  CK_SP800_108_KDF_PARAMS kdf_params;
  CK_SP800_108_COUNTER_FORMAT counter_format;
  CK_SP800_108_DKM_LENGTH_FORMAT length_format;
  CK_DERIVED_KEY additional_key;
  CK_ATTRIBUTE additional_key_attrs[2];
  CK_ULONG attr_type, attr_len;
  CK_OBJECT_HANDLE out_key_handle = 0;
  CK_PRF_DATA_PARAM data_param[5];
  SECItem params;

  kdf_params.prfType = CKM_SHA256_HMAC;
  kdf_params.pDataParams = data_param;

  /* First item is the counter */
  counter_format.bLittleEndian = FALSE;
  counter_format.ulWidthInBits = 32;
  data_param[0].type = CK_SP800_108_ITERATION_VARIABLE;
  data_param[0].pValue = &counter_format;
  data_param[0].ulValueLen = sizeof (counter_format);
  kdf_params.ulNumberOfDataParams = 1;

  /* Then the label */
  data_param[kdf_params.ulNumberOfDataParams].type = CK_SP800_108_BYTE_ARRAY;
  data_param[kdf_params.ulNumberOfDataParams].pValue = (guint8 *) label;
  data_param[kdf_params.ulNumberOfDataParams].ulValueLen = strlen (label) + 1;
  kdf_params.ulNumberOfDataParams += 1;

  /* Then the context a */
  if (data_a)
    {
      data_param[kdf_params.ulNumberOfDataParams].type = CK_SP800_108_BYTE_ARRAY;
      data_param[kdf_params.ulNumberOfDataParams].pValue = (guint8 *) g_bytes_get_data (data_a, NULL);
      data_param[kdf_params.ulNumberOfDataParams].ulValueLen = g_bytes_get_size (data_a);
      kdf_params.ulNumberOfDataParams += 1;
    }

  /* Then the context b */
  if (data_b)
    {
      data_param[kdf_params.ulNumberOfDataParams].type = CK_SP800_108_BYTE_ARRAY;
      data_param[kdf_params.ulNumberOfDataParams].pValue = (guint8 *) g_bytes_get_data (data_b, NULL);
      data_param[kdf_params.ulNumberOfDataParams].ulValueLen = g_bytes_get_size (data_b);
      kdf_params.ulNumberOfDataParams += 1;
    }

  /* And the output length */
  length_format.dkmLengthMethod = CK_SP800_108_DKM_LENGTH_SUM_OF_KEYS;
  length_format.bLittleEndian = FALSE;
  length_format.ulWidthInBits = 32;
  data_param[kdf_params.ulNumberOfDataParams].type = CK_SP800_108_DKM_LENGTH;
  data_param[kdf_params.ulNumberOfDataParams].pValue = &length_format;
  data_param[kdf_params.ulNumberOfDataParams].ulValueLen = sizeof (length_format);
  kdf_params.ulNumberOfDataParams += 1;

  /* TODO: support a second key out (may be discarded) */
  kdf_params.ulAdditionalDerivedKeys = 0;
  kdf_params.pAdditionalDerivedKeys = NULL;
  if (out_key_2)
    {
      attr_type = CKK_SHA256_HMAC;
      attr_len = 256 / 8;
      additional_key_attrs[0].type = CKA_KEY_TYPE;
      additional_key_attrs[0].pValue = &attr_type;
      additional_key_attrs[0].ulValueLen = sizeof (attr_type);
      additional_key_attrs[1].type = CKA_VALUE_LEN;
      additional_key_attrs[1].pValue = &attr_len;
      additional_key_attrs[1].ulValueLen = sizeof (attr_len);

      additional_key.pTemplate = additional_key_attrs;
      additional_key.ulAttributeCount = 2;
      additional_key.phKey = &out_key_handle;

      kdf_params.ulAdditionalDerivedKeys = 1;
      kdf_params.pAdditionalDerivedKeys = &additional_key;
    }

  params.len = sizeof (kdf_params);
  params.data = (guint8 *) &kdf_params;
  res = PK11_Derive (baseKey,
                     CKM_SP800_108_COUNTER_KDF,
                     &params,
                     CKM_SHA256_HMAC,
                     CKA_SIGN,
                     256 / 8);
  if (!res)
    {
      g_propagate_error (error,
                         fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                   "Error deriving secret (label: %s): %d",
                                                   label,
                                                   PORT_GetError ()));
    }

  if (out_key_2)
    *out_key_2 = PK11_SymKeyFromHandle (PK11_GetSlotFromKey (baseKey), res, CKO_DATA, CKM_NULL, out_key_handle, FALSE, NULL);

  return res;
}

/**
 * fpi_sdcp_mac:
 * @self: The #FpSdcpDevice
 * @baseKey: The key to base it on
 * @data_a: (nullable): Data segment a to concatenate
 * @data_b: (nullable): Data segment b to concatenate
 * @error: (out): #GError in case the return value is %NULL
 *
 * Convenience function to calculate a MAC with a specific label
 * and a generic data segments that is concatenated.
 *
 * Returns: A new #PK11SymKey
 **/
static GBytes *
fpi_sdcp_mac (FpSdcpDevice *self,
              const gchar  *label,
              GBytes       *data_a,
              GBytes       *data_b,
              GError      **error)
{
  g_autoptr(GBytes) res = NULL;
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);
  SECStatus r;
  SECItem input, output;
  g_autofree guint8 *data = NULL;
  gsize label_len;
  gsize data_a_len = 0;
  gsize data_b_len = 0;
  gsize length;

  label_len = strlen (label) + 1;
  length = label_len;
  if (data_a)
    data_a_len = g_bytes_get_size (data_a);
  if (data_b)
    data_b_len = g_bytes_get_size (data_b);

  length += data_a_len + data_b_len;

  data = g_malloc (length);

  memcpy (data, label, label_len);
  if (data_a)
    memcpy (data + label_len, g_bytes_get_data (data_a, NULL), data_a_len);
  if (data_b)
    memcpy (data + label_len + data_a_len, g_bytes_get_data (data_b, NULL), data_b_len);

  input.len = length;
  input.data = data;
  output.len = 32;
  output.data = g_malloc0 (32);
  res = g_bytes_new_take (output.data, output.len);

  r = PK11_SignWithSymKey (priv->mac_secret,
                           CKM_SHA256_HMAC,
                           NULL,
                           &output,
                           &input);
  if (r != SECSuccess)
    {
      g_propagate_error (error,
                         fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                   "Error calculating MAC (label: %s): %d",
                                                   label,
                                                   r));
    }

  return g_steal_pointer (&res);
}

/* FpiSdcpDevice */

/* Internal functions of FpSdcpDevice */
void
fpi_sdcp_device_connect (FpSdcpDevice *self)
{
  G_GNUC_UNUSED g_autofree void * ec_params_data = NULL;
  FpSdcpDeviceClass *cls = FP_SDCP_DEVICE_GET_CLASS (self);
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);
  SECOidData * oid_data;
  SECKEYECParams ec_parameters;
  SECStatus r = SECSuccess;

  /* Disable loading p11-kit's user configuration */
  g_setenv ("P11_KIT_NO_USER_CONFIG", "1", TRUE);

  /* Initialise NSS; Same as NSS_NoDB_Init but using a context. */
  if (!priv->nss_init_context)
    {
      priv->nss_init_context = NSS_InitContext ("", "", "", "", NULL,
                                                NSS_INIT_READONLY |
                                                NSS_INIT_NOCERTDB |
                                                NSS_INIT_NOMODDB |
                                                NSS_INIT_FORCEOPEN |
                                                NSS_INIT_NOROOTINIT |
                                                NSS_INIT_OPTIMIZESPACE);
    }
  if (!priv->nss_init_context)
    goto nss_error;

  g_clear_pointer (&priv->slot, PK11_FreeSlot);
  g_clear_pointer (&priv->host_key_private, SECKEY_DestroyPrivateKey);
  g_clear_pointer (&priv->host_key_public, SECKEY_DestroyPublicKey);

  /* Create a slot for PK11 operation */
  priv->slot = PK11_GetBestSlot (CKM_EC_KEY_PAIR_GEN, NULL);
  if (priv->slot == NULL)
    goto nss_error;

  /* SDCP Connect: 3.i. Generate an ephemeral ECDH key pair */
  /* Look up the OID data for our curve. */
  oid_data = SECOID_FindOIDByTag (SEC_OID_SECG_EC_SECP256R1);
  if (!oid_data)
    goto nss_error;

  /* Copy into EC parameters */
  ec_parameters.len = oid_data->oid.len + 2;
  ec_parameters.data = ec_params_data = g_malloc0 (oid_data->oid.len + 2);
  ec_parameters.data[0] = SEC_ASN1_OBJECT_ID;
  ec_parameters.data[1] = oid_data->oid.len;
  memcpy (ec_parameters.data + 2, oid_data->oid.data, oid_data->oid.len);

  priv->host_key_private = PK11_GenerateKeyPair (priv->slot, CKM_EC_KEY_PAIR_GEN,
                                                 &ec_parameters,
                                                 &priv->host_key_public,
                                                 FALSE, TRUE,
                                                 NULL);

  if (!priv->host_key_private || !priv->host_key_public)
    goto nss_error;

  /* SDCP Connect: 3.ii. Generate  host random */
  r = PK11_GenerateRandom (priv->host_random, sizeof (priv->host_random));
  if (r != SECSuccess)
    goto nss_error;

  /* SDCP Connect: 3.iii. Send the Connect message */
  cls->connect (self);

  return;

nss_error:
  if (r == SECSuccess)
    r = PORT_GetError ();
  fpi_sdcp_device_connect_complete (self,
                                    NULL, NULL, NULL,
                                    fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                              "Error calling NSS crypto routine: %d", r));
}

void
fpi_sdcp_device_reconnect (FpSdcpDevice *self)
{
  FpSdcpDeviceClass *cls = FP_SDCP_DEVICE_GET_CLASS (self);
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);
  SECStatus r;

  /* SDCP Reconnect: 2.i. Generate host random */
  r = PK11_GenerateRandom (priv->host_random, sizeof (priv->host_random));
  if (r != SECSuccess)
    {
      fpi_sdcp_device_reconnect_complete (self,
                                          NULL,
                                          fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                                    "Error calling NSS crypto routine: %d", r));
    }

  /* SDCP Reconnect: 2.ii. Send the Reconnect message */
  if (cls->reconnect)
    cls->reconnect (self);
  else
    fpi_sdcp_device_connect (self);
}

void
fpi_sdcp_device_enroll (FpSdcpDevice *self)
{
  FpSdcpDeviceClass *cls = FP_SDCP_DEVICE_GET_CLASS (self);
  FpPrint *print;

  g_return_if_fail (FP_IS_SDCP_DEVICE (self));
  g_return_if_fail (fpi_device_get_current_action (FP_DEVICE (self)) == FPI_DEVICE_ACTION_ENROLL);

  fpi_device_get_enroll_data (FP_DEVICE (self), &print);

  fpi_print_set_device_stored (print, FALSE);
  g_object_set (print, "fpi-data", NULL, NULL);

  /* For enrollment, all we need to do is start the process. But just to be sure,
   * clear a bit of internal state.
   */
  cls->enroll_begin (self);
}

void
fpi_sdcp_device_identify (FpSdcpDevice *self)
{
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);
  FpSdcpDeviceClass *cls = FP_SDCP_DEVICE_GET_CLASS (self);
  FpiDeviceAction action;
  SECStatus r;

  g_return_if_fail (FP_IS_SDCP_DEVICE (self));
  action = fpi_device_get_current_action (FP_DEVICE (self));

  g_return_if_fail (action == FPI_DEVICE_ACTION_IDENTIFY || action == FPI_DEVICE_ACTION_VERIFY);

  /* Generate a new nonce. */
  r = PK11_GenerateRandom (priv->host_random, sizeof (priv->host_random));
  if (r != SECSuccess)
    {
      fpi_device_action_error (FP_DEVICE (self),
                               fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                         "Error calling NSS crypto routine: %d", r));
      return;
    }

  cls->identify (self);
}

/*********************************************************/
/* Private API */

/**
 * fp_sdcp_device_set_intermediate_cas:
 * @self: The #FpSdcpDevice
 * @ca_1: (transfer none): DER encoded intermediate CA certificate #1
 * @ca_2: (transfer none): DER encoded intermediate CA certificate #2
 *
 * Set the intermediate CAs used by the device.
 */
void
fpi_sdcp_device_set_intermediat_cas (FpSdcpDevice *self,
                                     GBytes       *ca_1,
                                     GBytes       *ca_2)
{
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);

  g_ptr_array_set_size (priv->intermediate_cas, 0);
  if (ca_1)
    g_ptr_array_add (priv->intermediate_cas, g_bytes_ref (ca_1));
  if (ca_2)
    g_ptr_array_add (priv->intermediate_cas, g_bytes_ref (ca_2));
}

/* FIXME: This is (transfer full), but other getters have (transfer none)
*        for drivers. Kind of inconsistent, but it is convenient here. */
/**
 * fp_sdcp_device_get_connect_data:
 * @r_h: (out) (transfer full): The host random
 * @pk_h: (out) (transfer full): The host public key
 *
 * Get data required to connect to (i.e. open) the device securely.
 */
void
fpi_sdcp_device_get_connect_data (FpSdcpDevice *self,
                                  GBytes      **r_h,
                                  GBytes      **pk_h)
{
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);

  g_return_if_fail (r_h != NULL);
  g_return_if_fail (pk_h != NULL);

  *r_h = g_bytes_new (priv->host_random, sizeof (priv->host_random));

  g_assert (priv->host_key_public->u.ec.publicValue.len == 65);
  *pk_h = g_bytes_new (priv->host_key_public->u.ec.publicValue.data, priv->host_key_public->u.ec.publicValue.len);
}

/**
 * fp_sdcp_device_get_reconnect_data:
 * @r_h: (out) (transfer full): The host random
 *
 * Get data required to reconnect to (i.e. open) to the device securely.
 */
void
fpi_sdcp_device_get_reconnect_data (FpSdcpDevice *self,
                                    GBytes      **r_h)
{
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);

  g_return_if_fail (r_h != NULL);

  *r_h = g_bytes_new (priv->host_random, sizeof (priv->host_random));
}

/**
 * fp_sdcp_device_get_identify_data:
 * @r_h: (out) (transfer full): The host random
 *
 * Get data required to identify a new print.
 */
void
fpi_sdcp_device_get_identify_data (FpSdcpDevice *self,
                                   GBytes      **nonce)
{
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);

  g_return_if_fail (nonce != NULL);

  *nonce = g_bytes_new (priv->host_random, sizeof (priv->host_random));
}

/* Returns the certificates public key after validation. */
static SECKEYPublicKey *
fpi_sdcp_validate_cert (FpSdcpDevice *self,
                        FpiSdcpClaim *claim,
                        GError      **error)
{
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);
  CERTValInParam in_params[1] = { 0, };
  CERTValOutParam out_params[2] = { 0, };
  const void *cert_m_data;
  gsize cert_m_length;
  CERTCertDBHandle *cert_db = NULL;
  CERTCertificate *cert_m = NULL;

  g_autoptr(GPtrArray) intermediate_cas = NULL;
  PLArenaPool *res_arena = NULL;
  SECKEYPublicKey *res = NULL;
  SECStatus r;
  gint i;

  g_debug ("cert_m:");
  dump_bytes (claim->cert_m);
  cert_m_data = g_bytes_get_data (claim->cert_m, &cert_m_length);
  cert_m = CERT_DecodeCertFromPackage ((char *) cert_m_data, cert_m_length);
  if (!cert_m)
    {
      /* So, the MS test client we use for the virtual-sdcp driver does not return
       * a certificate (yeah ... why?). This special case is purely for testing
       * purposes and should be removed by fixing the test client!
       */
      if (g_str_equal (fp_device_get_driver (FP_DEVICE (self)), "virtual_sdcp") && cert_m_length == 65)
        {
          /* Create a new public key directly from the buffer rather than from the certificate. */
          res_arena = PORT_NewArena (DER_DEFAULT_CHUNKSIZE);
          g_assert (res_arena);

          res = (SECKEYPublicKey *) PORT_ArenaZAlloc (res_arena, sizeof (SECKEYPublicKey));
          g_assert (res);

          res->arena = res_arena;
          res->pkcs11Slot = 0;
          res->pkcs11ID = CK_INVALID_HANDLE;

          res->keyType = priv->host_key_public->keyType;
          res->u.ec.DEREncodedParams = priv->host_key_public->u.ec.DEREncodedParams;
          res->u.ec.size = priv->host_key_public->u.ec.size;
          res->u.ec.publicValue.len = 65;
          res->u.ec.publicValue.type = priv->host_key_public->u.ec.publicValue.type;
          res->u.ec.publicValue.data = (guint8 *) cert_m_data;

          goto out;
        }

      g_propagate_error (error,
                         fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                   "Failed to read cert_m: %d", PORT_GetError ()));
      goto out;
    }

#if 0
  /* The following code would be a better way of specifying the intermediate CAs
   * (instead of inserting them into the certificate store), but it does not
   * work because the feature has simply not been implemented in PKIX.
   * The code here is left purely as a reference and warning.
   */
  CERTCertList *intermediate_cas = NULL;

  /* Setup list for the intermediate CAs. */
  intermediate_cas = CERT_NewCertList ();
  for (i = 0; i < priv->intermediate_cas->len; i++)
    {
      CERTCertificate *cert = NULL;
      const void *data;
      gsize length;

      data = g_bytes_get_data ((GBytes *) g_ptr_array_index (priv->intermediate_cas, i),
                               &length);
      cert = CERT_DecodeCertFromPackage ((char *) data, length);
      if (!cert)
        {
          g_propagate_error (error,
                             fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                       "Failed to read intermediate cert: %d", PORT_GetError ()));
          goto out;
        }
      /* Adding takes the reference. */
      r = CERT_AddCertToListTail (intermediate_cas, cert);

      if (r != SECSuccess)
        {
          g_propagate_error (error,
                             fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                       "Failed to add cert to cert list: %d", r));
          goto out;
        }
    }
#endif

  /* Import intermediate certificates into cert DB. */
  cert_db = CERT_GetDefaultCertDB ();
  if (!cert_db)
    {
      g_propagate_error (error,
                         fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                   "No default certificate DB!"));
      goto out;
    }

  intermediate_cas = g_ptr_array_new_full (priv->intermediate_cas->len, g_free);
  for (i = 0; i < priv->intermediate_cas->len; i++)
    {
      gsize length;
      SECItem *item = NULL;

      item = g_new0 (SECItem, 1);
      item->type = siDERCertBuffer;
      item->data = (guint8 *) g_bytes_get_data ((GBytes *) g_ptr_array_index (priv->intermediate_cas, i),
                                                &length);
      item->len = length;
      g_ptr_array_add (intermediate_cas, item);
    }
  r = CERT_ImportCerts (cert_db, certUsageVerifyCA,
                        intermediate_cas->len, (SECItem **) intermediate_cas->pdata,
                        NULL,
                        FALSE, FALSE, NULL);
  if (r != SECSuccess)
    {
      g_propagate_error (error,
                         fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                   "Failed to import intermediate CAs: %d", PORT_GetError ()));
      goto out;
    }

  /* We assume we have the root CA in the system store already. */
  in_params[0].type = cert_pi_end;

  out_params[0].type = cert_po_end; // cert_po_errorLog;
  out_params[0].value.pointer.log = NULL;
  out_params[1].type = cert_po_end;

  r = CERT_PKIXVerifyCert (cert_m,
                           certUsageAnyCA, /* XXX: is this correct? */
                           in_params,
                           out_params,
                           NULL);
  if (r != SECSuccess)
    {
      g_propagate_error (error,
                         fpi_device_error_new_msg (FP_DEVICE_ERROR_UNTRUSTED,
                                                   "Failed to verify device certificate: %d", PORT_GetError ()));
      goto out;
    }

  /* All seems good, extract the public key in order to return it. */
  res = CERT_ExtractPublicKey (cert_m);
  if (!res)
    {
      g_propagate_error (error,
                         fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                   "Failed to extract public key from certificate: %d", PORT_GetError ()));
      goto out;
    }

out:
  g_clear_pointer (&cert_m, CERT_DestroyCertificate);
  if (out_params[0].value.pointer.log)
    PORT_FreeArena (out_params[0].value.pointer.log->arena, FALSE);
  return res;
}

/* FIXME: How to provide intermediate CAs provided? Same call or separate channel? */
/**
 * fpi_sdcp_device_connect_complete:
 * @self: a #FpSdcpDevice fingerprint device
 * @r_d: The device random nonce
 * @claim: The device #FpiSdcpClaim
 * @mac: The MAC authenticating @claim
 * @error: A #GError or %NULL on success
 *
 * Reports completion of connect (i.e. open) operation.
 */
void
fpi_sdcp_device_connect_complete (FpSdcpDevice *self,
                                  GBytes       *r_d,
                                  FpiSdcpClaim *claim,
                                  GBytes       *mac,
                                  GError       *error)
{
  g_autoptr(GBytes) r_h = NULL;
  g_autoptr(GBytes) claim_hash_bytes = NULL;
  g_autoptr(GBytes) claim_mac = NULL;
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);
  SECKEYPublicKey firmware_key_public = { 0, };
  SECKEYPublicKey device_key_public = { 0, };
  SECKEYPublicKey *model_key_public = NULL;
  HASHContext *hash_ctx;
  guint8 hash_out[SHA256_LENGTH];
  guint hash_len = 0;
  FpiDeviceAction action;
  PK11SymKey *a = NULL;
  PK11SymKey *enc_secret = NULL;
  gsize length;
  SECItem sig, hash;
  SECStatus r;

  action = fpi_device_get_current_action (FP_DEVICE (self));

  g_return_if_fail (action == FPI_DEVICE_ACTION_OPEN);

  if (error)
    {
      if (r_d || claim || mac)
        {
          g_warning ("Driver provided connect information but also reported error.");
          g_clear_pointer (&r_d, g_bytes_unref);
          g_clear_pointer (&claim, fpi_sdcp_claim_free);
          g_clear_pointer (&mac, g_bytes_unref);
        }

      fpi_device_open_complete (FP_DEVICE (self), error);
      return;
    }

  if (!r_d || !claim || !mac ||
      (!claim->cert_m || !claim->pk_d || !claim->pk_f || !claim->h_f || !claim->s_m || !claim->s_d))
    {
      g_warning ("Driver did not provide all required information to callback, returning error instead.");
      g_clear_pointer (&r_d, g_bytes_unref);
      g_clear_pointer (&claim, fpi_sdcp_claim_free);
      g_clear_pointer (&mac, g_bytes_unref);

      fpi_device_open_complete (FP_DEVICE (self),
                                fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                          "Driver called connect complete with incomplete arguments."));
      return;
    }

  /* Device key is of same type as host key */
  g_assert (g_bytes_get_size (claim->pk_f) == 65);
  firmware_key_public.keyType = priv->host_key_public->keyType;
  firmware_key_public.u.ec.DEREncodedParams = priv->host_key_public->u.ec.DEREncodedParams;
  firmware_key_public.u.ec.size = priv->host_key_public->u.ec.size;
  firmware_key_public.u.ec.publicValue.len = 65;
  firmware_key_public.u.ec.publicValue.type = priv->host_key_public->u.ec.publicValue.type;
  firmware_key_public.u.ec.publicValue.data = (guint8 *) g_bytes_get_data (claim->pk_f, NULL);

  /* SDCP Connect: 5.i. Perform key agreement */
  a = PK11_PubDeriveWithKDF (priv->host_key_private,
                             &firmware_key_public,
                             TRUE,
                             NULL,
                             NULL,
                             CKM_ECDH1_DERIVE,
                             CKM_SP800_108_COUNTER_KDF,
                             CKA_DERIVE,
                             32, /* 256 bit (HMAC) secret */
                             CKD_NULL,
                             NULL,
                             NULL);

  if (!a)
    {
      error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                        "Error doing key agreement: %d", PORT_GetError ());
      goto out;
    }

  /* SDCP Connect: 5.ii. Derive master secret */
  g_clear_pointer (&priv->master_secret, PK11_FreeSymKey);

  r_h = g_bytes_new (priv->host_random, sizeof (priv->host_random));

  priv->master_secret = fpi_sdcp_kdf (self,
                                      a,
                                      "master secret",
                                      r_h,
                                      r_d,
                                      NULL,
                                      &error);
  if (!priv->master_secret)
    goto out;

  /* SDCP Connect: 5.iii. Derive MAC secret and symetric key */

  /* NOTE: symetric key is never used, as such we just don't derive it! */
  g_clear_pointer (&priv->mac_secret, PK11_FreeSymKey);
  priv->mac_secret = fpi_sdcp_kdf (self,
                                   priv->master_secret,
                                   "application keys",
                                   NULL,
                                   NULL,
                                   &enc_secret,
                                   &error);
  if (!priv->mac_secret)
    goto out;

  /* SDCP Connect: 5.iv. Validate the MAC over H(claim) */
  hash_ctx = HASH_Create (HASH_AlgSHA256);
  if (!hash_ctx)
    {
      error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                        "Could not create hash context");
      goto out;
    }
  HASH_Begin (hash_ctx);
  HASH_Update (hash_ctx, g_bytes_get_data (claim->cert_m, NULL), g_bytes_get_size (claim->cert_m));
  HASH_Update (hash_ctx, g_bytes_get_data (claim->pk_d, NULL), g_bytes_get_size (claim->pk_d));
  HASH_Update (hash_ctx, g_bytes_get_data (claim->pk_f, NULL), g_bytes_get_size (claim->pk_f));
  HASH_Update (hash_ctx, g_bytes_get_data (claim->h_f, NULL), g_bytes_get_size (claim->h_f));
  HASH_Update (hash_ctx, g_bytes_get_data (claim->s_m, NULL), g_bytes_get_size (claim->s_m));
  HASH_Update (hash_ctx, g_bytes_get_data (claim->s_d, NULL), g_bytes_get_size (claim->s_d));
  HASH_End (hash_ctx, hash_out, &hash_len, sizeof (hash_out));
  g_clear_pointer (&hash_ctx, HASH_Destroy);
  g_assert (hash_len == sizeof (hash_out));

  claim_hash_bytes = g_bytes_new (hash_out, sizeof (hash_out));
  g_debug ("H(c):");
  dump_bytes (claim_hash_bytes);

  claim_mac = fpi_sdcp_mac (self, "connect", claim_hash_bytes, NULL, &error);
  if (!claim_mac)
    goto out;

  g_debug ("MAC(s, \"connect\"||H(c)):");
  dump_bytes (claim_mac);

  if (!g_bytes_equal (mac, claim_mac))
    {
      error = fpi_device_error_new_msg (FP_DEVICE_ERROR_UNTRUSTED,
                                        "Device MAC over H(c) is incorrect.");
      goto out;
    }

  /* SDCP Connect: 5.v. Unpack the claim (SKIP, already done) */
  /* SDCP Connect: 5.vi. Verify claim */

  /* First, validate the certificate (and return its public key). */
  model_key_public = fpi_sdcp_validate_cert (self, claim, &error);
  if (!model_key_public)
    goto out;

  /* Verify(pk_m, H(pk_d), s_m) */
  sig.data = (guint8 *) g_bytes_get_data (claim->s_m, &length);
  sig.len = length;
  memset (hash_out, 0, sizeof (hash_out));
  r = PK11_HashBuf (HASH_GetHashOidTagByHashType (HASH_AlgSHA256),
                    hash_out,
                    g_bytes_get_data (claim->pk_d, NULL),
                    g_bytes_get_size (claim->pk_d));
  if (r != SECSuccess)
    {
      error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                        "Failed to hash device public key!");
      goto out;
    }

  hash.data = hash_out;
  hash.len = sizeof (hash_out);
  r = PK11_Verify (model_key_public, &sig, &hash, NULL);
  if (r != SECSuccess)
    {
      error = fpi_device_error_new_msg (FP_DEVICE_ERROR_UNTRUSTED,
                                        "Verification of device public key failed: %d", PORT_GetError ());
      goto out;
    }

  device_key_public.keyType = priv->host_key_public->keyType;
  device_key_public.u.ec.DEREncodedParams = priv->host_key_public->u.ec.DEREncodedParams;
  device_key_public.u.ec.size = priv->host_key_public->u.ec.size;
  device_key_public.u.ec.publicValue.len = 65;
  device_key_public.u.ec.publicValue.type = priv->host_key_public->u.ec.publicValue.type;
  device_key_public.u.ec.publicValue.data = (guint8 *) g_bytes_get_data (claim->pk_d, NULL);

  /* Verify(pk_d, H(C001||h_f||pk_f), s_d) */
  sig.data = (guint8 *) g_bytes_get_data (claim->s_d, &length);
  sig.len = length;

  hash_ctx = HASH_Create (HASH_AlgSHA256);
  if (!hash_ctx)
    {
      error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                        "Could not create hash context");
      goto out;
    }
  HASH_Begin (hash_ctx);
  HASH_Update (hash_ctx, (guint8 *) "\xC0\x01", 2);
  HASH_Update (hash_ctx, g_bytes_get_data (claim->h_f, NULL), g_bytes_get_size (claim->h_f));
  HASH_Update (hash_ctx, g_bytes_get_data (claim->pk_f, NULL), g_bytes_get_size (claim->pk_f));
  HASH_End (hash_ctx, hash_out, &hash_len, sizeof (hash_out));
  g_clear_pointer (&hash_ctx, HASH_Destroy);
  g_assert (hash_len == sizeof (hash_out));

  hash.data = hash_out;
  hash.len = sizeof (hash_out);
  r = PK11_Verify (&device_key_public, &sig, &hash, NULL);
  if (r != SECSuccess)
    {
      error = fpi_device_error_new_msg (FP_DEVICE_ERROR_UNTRUSTED,
                                        "Verification of boot process failed: %d", PORT_GetError ());
      goto out;
    }

  /* XXX/FIXME: We should be checking H(f) against a list of compromised firmwares.
   *            We would need a way to distribute and load it though.
   */

out:
  g_clear_pointer (&a, PK11_FreeSymKey);
  g_clear_pointer (&enc_secret, PK11_FreeSymKey);
  g_clear_pointer (&model_key_public, SECKEY_DestroyPublicKey);

  if (error)
    g_clear_pointer (&priv->mac_secret, PK11_FreeSymKey);

  fpi_device_open_complete (FP_DEVICE (self), error);
}

/**
 * fpi_sdcp_device_reconnect_complete:
 * @self: a #FpSdcpDevice fingerprint device
 * @mac: The MAC authenticating @claim
 * @error: A #GError or %NULL on success
 *
 * Reports completion of a reconnect (i.e. open) operation.
 */
void
fpi_sdcp_device_reconnect_complete (FpSdcpDevice *self,
                                    GBytes       *mac,
                                    GError       *error)
{
  g_autoptr(GError) err = NULL;
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);
  FpiDeviceAction action;

  action = fpi_device_get_current_action (FP_DEVICE (self));

  g_return_if_fail (action == FPI_DEVICE_ACTION_OPEN);

  if (error)
    {
      if (mac)
        {
          g_warning ("Driver provided a MAC but also reported an error.");
          g_bytes_unref (mac);
        }

      /* Silently try a normal connect instead. */
      fpi_sdcp_device_connect (self);
    }
  else if (mac)
    {
      g_autoptr(GBytes) mac_verify = NULL;
      g_autoptr(GBytes) host_random = NULL;

      /* We got a MAC, so we can check whether the device
       * still agrees with us on the shared secret. */
      host_random = g_bytes_new (priv->host_random, sizeof (priv->host_random));
      mac_verify = fpi_sdcp_mac (self, "reconnect", host_random, NULL, &err);
      if (!mac_verify)
        {
          fpi_device_open_complete (FP_DEVICE (self), g_steal_pointer (&err));
          return;
        }

      if (g_bytes_equal (mac, mac_verify))
        {
          g_debug ("Reconnect succeeded");
          fpi_device_open_complete (FP_DEVICE (self), NULL);
        }
      else
        {
          g_message ("Fast reconnect with SDCP device failed, doing a full connect.");
          fpi_sdcp_device_connect (self);
        }
    }
  else
    {
      fpi_device_open_complete (FP_DEVICE (self),
                                fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                          "Driver called reconnect complete with wrong arguments."));
    }
}

/**
 * fpi_sdcp_device_enroll_set_nonce:
 * @self: a #FpSdcpDevice fingerprint device
 * @nonce: The device generated nonce
 *
 * Called during enroll to inform the SDCP base class about the nonce
 * that the device chose. This can be called at any point, but must be
 * called before calling fpi_sdcp_device_enroll_ready().
 */
void
fpi_sdcp_device_enroll_set_nonce (FpSdcpDevice *self,
                                  GBytes       *nonce)
{
  g_autoptr(GBytes) id = NULL;
  GVariant *id_var;
  FpPrint *print;
  GVariant *data;

  g_return_if_fail (FP_IS_SDCP_DEVICE (self));
  g_return_if_fail (fpi_device_get_current_action (FP_DEVICE (self)) == FPI_DEVICE_ACTION_ENROLL);

  g_return_if_fail (nonce || g_bytes_get_size (nonce) != 32);

  fpi_device_get_enroll_data (FP_DEVICE (self), &print);

  id = fpi_sdcp_mac (self, "enroll", nonce, NULL, NULL);
  if (!id)
    {
      g_warning ("Could not generate enroll MAC");
      return;
    }

  id_var = g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE,
                                      g_bytes_get_data (id, NULL),
                                      g_bytes_get_size (id),
                                      1);
  data = g_variant_new ("(@ay)", id_var);

  /* Set to true once committed */
  fpi_print_set_device_stored (print, FALSE);

  /* Attach the ID to the print */
  g_object_set (print, "fpi-data", data, NULL);
}

/**
 * fpi_sdcp_device_enroll_ready:
 * @self: a #FpSdcpDevice fingerprint device
 * @error: a #GError or %NULL on success
 *
 * Called when the print is ready to be committed to device memory.
 * For each enroll step, fpi_device_enroll_progress() must first
 * be called until the enroll is ready to be committed.
 */
void
fpi_sdcp_device_enroll_ready (FpSdcpDevice *self,
                              GError       *error)
{
  g_autoptr(GVariant) data = NULL;
  g_autoptr(GVariant) id_var = NULL;
  g_autoptr(GBytes) id = NULL;
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);
  FpSdcpDeviceClass *cls = FP_SDCP_DEVICE_GET_CLASS (self);
  FpPrint *print;

  g_return_if_fail (FP_IS_SDCP_DEVICE (self));
  g_return_if_fail (fpi_device_get_current_action (FP_DEVICE (self)) == FPI_DEVICE_ACTION_ENROLL);

  fpi_device_get_enroll_data (FP_DEVICE (self), &print);

  if (error)
    {
      fpi_device_enroll_complete (FP_DEVICE (self), NULL, error);
      g_object_set (print, "fpi-data", NULL, NULL);
      return;
    }

  /* TODO: The following will need to ensure that the ID has been generated */

  g_object_get (G_OBJECT (print), "fpi-data", &data, NULL);

  if (data)
    {
      const guint8 *id_data;
      gsize id_len;

      g_variant_get (data,
                     "(@ay)",
                     &id_var);

      id_data = g_variant_get_fixed_array (id_var, &id_len, 1);
      id = g_bytes_new (id_data, id_len);
    }

  g_debug ("ID/enroll mac:");
  dump_bytes (id);

  if (!id)
    {
      g_warning ("Driver failed to call fpi_sdcp_device_enroll_set_nonce, aborting enroll.");

      /* NOTE: Cancel the enrollment, i.e. don't commit */
      priv->enroll_pre_commit_error = fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                                "Device/driver did not provide a nonce as required by protocol, aborting enroll!");
      cls->enroll_commit (self, NULL);
    }
  else
    {
      cls->enroll_commit (self, g_steal_pointer (&id));
    }
}

/**
 * fpi_sdcp_device_enroll_commit_complete:
 * @self: a #FpSdcpDevice fingerprint device
 *
 * Called when device has committed the given print to memory.
 * This finalizes the enroll operation.
 */
void
fpi_sdcp_device_enroll_commit_complete (FpSdcpDevice *self,
                                        GError       *error)
{
  g_autoptr(GVariant) data = NULL;
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);
  FpPrint *print;

  g_return_if_fail (FP_IS_SDCP_DEVICE (self));
  g_return_if_fail (fpi_device_get_current_action (FP_DEVICE (self)) == FPI_DEVICE_ACTION_ENROLL);

  if (priv->enroll_pre_commit_error)
    {
      if (error)
        {
          g_warning ("Cancelling enroll after error failed with: %s", error->message);
          g_error_free (error);
        }

      fpi_device_enroll_complete (FP_DEVICE (self),
                                  NULL,
                                  g_steal_pointer (&priv->enroll_pre_commit_error));
      return;
    }

  if (error)
    {
      fpi_device_enroll_complete (FP_DEVICE (self), NULL, error);
      return;
    }

  fpi_device_get_enroll_data (FP_DEVICE (self), &print);
  g_object_get (G_OBJECT (print), "fpi-data", &data, NULL);
  if (!data)
    {
      g_error ("Inconsistent state, the print must have the enrolled ID attached at this point");
      return;
    }

  fpi_print_set_type (print, FPI_PRINT_SDCP);
  fpi_print_set_device_stored (print, TRUE);

  fpi_device_enroll_complete (FP_DEVICE (self), g_object_ref (print), NULL);
}

/**
 * fpi_sdcp_device_identify_retry:
 * @self: a #FpSdcpDevice fingerprint device
 * @error: a #GError containing the retry condition
 *
 * Called when the device requires the finger to be presented again.
 * This should not be called for a verified no-match, it should only
 * be called if e.g. the finger was not centered properly or similar.
 *
 * Effectively this simply raises the error up. This function exists
 * to bridge the difference in semantics that SDPC has from how
 * libfprint works internally.
 */
void
fpi_sdcp_device_identify_retry (FpSdcpDevice *self,
                                GError       *error)
{
  FpiDeviceAction action;

  g_return_if_fail (FP_IS_SDCP_DEVICE (self));
  action = fpi_device_get_current_action (FP_DEVICE (self));

  g_return_if_fail (action == FPI_DEVICE_ACTION_IDENTIFY || action == FPI_DEVICE_ACTION_VERIFY);

  if (action == FPI_DEVICE_ACTION_VERIFY)
    fpi_device_verify_report (FP_DEVICE (self), FPI_MATCH_ERROR, NULL, error);
  else if (action == FPI_DEVICE_ACTION_IDENTIFY)
    fpi_device_identify_report (FP_DEVICE (self), NULL, NULL, error);
}

/**
 * fpi_sdcp_device_identify_complete:
 * @self: a #FpSdcpDevice fingerprint device
 * @id: (transfer none): the ID as reported by the device
 * @mac: (transfer none): MAC authenticating the message
 * @error: (transfer full): #GError if an error occured
 *
 * Called when device is done with the identification routine. The
 * returned ID may be %NULL if none of the in-device templates matched.
 */
void
fpi_sdcp_device_identify_complete (FpSdcpDevice *self,
                                   GBytes       *id,
                                   GBytes       *mac,
                                   GError       *error)
{
  g_autoptr(GBytes) mac_verify = NULL;
  g_autoptr(GBytes) host_random = NULL;
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);
  GError *err = NULL;
  FpPrint *identified_print;
  GVariant *id_var;
  GVariant *data;
  FpiDeviceAction action;

  g_return_if_fail (FP_IS_SDCP_DEVICE (self));
  action = fpi_device_get_current_action (FP_DEVICE (self));

  g_return_if_fail (action == FPI_DEVICE_ACTION_IDENTIFY || action == FPI_DEVICE_ACTION_VERIFY);

  if (error)
    {
      fpi_device_action_error (FP_DEVICE (self), error);
      return;
    }

  if (!id || !mac || g_bytes_get_size (id) != 32 || g_bytes_get_size (mac) != 32)
    {
      fpi_device_action_error (FP_DEVICE (self),
                               fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                         "Driver returned incorrect ID/MAC for identify operation"));
      return;
    }

  host_random = g_bytes_new (priv->host_random, sizeof (priv->host_random));
  mac_verify = fpi_sdcp_mac (self, "identify", host_random, id, &err);
  if (!mac_verify)
    {
      fpi_device_action_error (FP_DEVICE (self),
                               err);
      return;
    }

  if (!g_bytes_equal (mac, mac_verify))
    {
      fpi_device_action_error (FP_DEVICE (self),
                               fpi_device_error_new_msg (FP_DEVICE_ERROR_UNTRUSTED,
                                                         "Reported match from the device cannot be trusted!"));
      return;
    }

  /* Create a new print */
  identified_print = fp_print_new (FP_DEVICE (self));

  fpi_print_set_type (identified_print, FPI_PRINT_SDCP);
  fpi_print_set_device_stored (identified_print, TRUE);

  id_var = g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE,
                                      g_bytes_get_data (id, NULL),
                                      g_bytes_get_size (id),
                                      1);
  data = g_variant_new ("(@ay)", id_var);

  /* Set to true once committed */
  fpi_print_set_device_stored (identified_print, FALSE);

  /* Attach the ID to the print */
  g_object_set (identified_print, "fpi-data", data, NULL);


  /* The surrounding API expects a match/no-match against a given set. */
  if (action == FPI_DEVICE_ACTION_VERIFY)
    {
      FpPrint *print;

      fpi_device_get_verify_data (FP_DEVICE (self), &print);

      if (fp_print_equal (print, identified_print))
        fpi_device_verify_report (FP_DEVICE (self), FPI_MATCH_SUCCESS, identified_print, NULL);
      else
        fpi_device_verify_report (FP_DEVICE (self), FPI_MATCH_FAIL, identified_print, NULL);

      fpi_device_verify_complete (FP_DEVICE (self), NULL);
    }
  else
    {
      GPtrArray *prints;
      gint i;

      fpi_device_get_identify_data (FP_DEVICE (self), &prints);

      for (i = 0; i < prints->len; i++)
        {
          FpPrint *print = g_ptr_array_index (prints, i);

          if (fp_print_equal (print, identified_print))
            {
              fpi_device_identify_report (FP_DEVICE (self), print, identified_print, NULL);
              fpi_device_identify_complete (FP_DEVICE (self), NULL);
              return;
            }
        }

      /* Print wasn't in database. */
      fpi_device_identify_report (FP_DEVICE (self), NULL, identified_print, NULL);
      fpi_device_identify_complete (FP_DEVICE (self), NULL);
    }
}
