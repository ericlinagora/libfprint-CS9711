/*
 * Copyright (C) 2007-2008 Daniel Drake <dsd@gentoo.org>
 * Copyright (C) 2018 Bastien Nocera <hadess@hadess.net>
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

#pragma once

#include "fp-device.h"
#include "fpi-usb-transfer.h"

/* async drv <--> lib comms */

/**
 * FpiSsm:
 *
 * Sequential state machine that iterates sequentially over
 * a predefined series of states. Can be terminated by either completion or
 * failure error conditions.
 */
typedef struct _FpiSsm FpiSsm;

/**
 * FpiSsmCompletedCallback:
 * @ssm: a #FpiSsm state machine
 * @dev: the #fp_dev fingerprint device
 * @error: The #GError or %NULL on successful completion
 *
 * The callback called when a state machine completes successfully,
 * as set when calling fpi_ssm_start().
 */
typedef void (*FpiSsmCompletedCallback)(FpiSsm   *ssm,
                                        FpDevice *dev,
                                        GError   *error);

/**
 * FpiSsmHandlerCallback:
 * @ssm: a #FpiSsm state machine
 * @dev: the #fp_dev fingerprint device
 *
 * The callback called when a state machine transitions from one
 * state to the next, as set when calling fpi_ssm_new ().
 */
typedef void (*FpiSsmHandlerCallback)(FpiSsm   *ssm,
                                      FpDevice *dev);

/* for library and drivers */
FpiSsm *fpi_ssm_new (FpDevice             *dev,
                     FpiSsmHandlerCallback handler,
                     int                   nr_states);
void fpi_ssm_free (FpiSsm *machine);
void fpi_ssm_start (FpiSsm                 *ssm,
                    FpiSsmCompletedCallback callback);
void fpi_ssm_start_subsm (FpiSsm *parent,
                          FpiSsm *child);

/* for drivers */
void fpi_ssm_next_state (FpiSsm *machine);
void fpi_ssm_jump_to_state (FpiSsm *machine,
                            int     state);
void fpi_ssm_mark_completed (FpiSsm *machine);
void fpi_ssm_mark_failed (FpiSsm *machine,
                          GError *error);
void fpi_ssm_set_data (FpiSsm        *machine,
                       gpointer       ssm_data,
                       GDestroyNotify ssm_data_destroy);
gpointer fpi_ssm_get_data (FpiSsm *machine);
GError * fpi_ssm_get_error (FpiSsm *machine);
GError * fpi_ssm_dup_error (FpiSsm *machine);
int fpi_ssm_get_cur_state (FpiSsm *machine);

/* Callbacks to be used by the driver instead of implementing their own
 * logic.
 */
void fpi_ssm_next_state_timeout_cb (FpDevice *dev,
                                    void     *data);
void fpi_ssm_usb_transfer_cb (FpiUsbTransfer *transfer,
                              FpDevice       *device,
                              gpointer        user_data,
                              GError         *error);
