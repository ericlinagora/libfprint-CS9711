/*
 * Functions to assist with asynchronous driver <---> library communications
 * Copyright (C) 2007-2008 Daniel Drake <dsd@gentoo.org>
 * Copyright (C) 2019 Benjamin Berg <bberg@redhat.com>
 * Copyright (C) 2019 Marco Trevisan <marco.trevisan@canonical.com>
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

#define FP_COMPONENT "SSM"

#include "drivers_api.h"
#include "fpi-ssm.h"


/**
 * SECTION:fpi-ssm
 * @title: Sequential state machine
 * @short_description: State machine helpers
 *
 * Asynchronous driver design encourages some kind of state machine behind it.
 * #FpiSsm provides a simple mechanism to implement a state machine, which
 * is often entirely linear. You can however also jump to a specific state
 * or do an early return from the SSM by completing it.
 *
 * e.g. `S1` ↦ `S2` ↦ `S3` ↦ `S4` ↦ `C1` ↦ `C2` ↦ `final`
 *
 * Where `S1` is the start state. The `C1` and later states are cleanup states
 * that may be defined. The difference is that these states will never be
 * skipped when marking the SSM as completed.
 *
 * Use fpi_ssm_new() to create a new state machine with a defined number of
 * states. Note that the state numbers start at zero, making them match the
 * first value in a C enumeration.
 *
 * To start a ssm, you pass in a completion callback function to fpi_ssm_start()
 * which gets called when the ssm completes (both on failure and on success).
 * Starting a ssm also takes ownership of it and it will be automatically
 * free'ed after the callback function has been called.
 *
 * To iterate to the next state, call fpi_ssm_next_state(). It is legal to
 * attempt to iterate beyond the final state - this is equivalent to marking
 * the ssm as successfully completed.
 *
 * To mark successful completion of a SSM, either iterate beyond the final
 * state or call fpi_ssm_mark_completed() from any state.
 *
 * To mark failed completion of a SSM, call fpi_ssm_mark_failed() from any
 * state. You must pass a non-zero error code.
 *
 * Your state handling function looks at the return value of
 * fpi_ssm_get_cur_state() in order to determine the current state and hence
 * which operations to perform (a switch statement is appropriate).
 *
 * Typically, the state handling function fires off an asynchronous
 * communication with the device (such as a USB transfer), and the
 * callback function iterates the machine to the next state
 * upon success (or fails).
 */

struct _FpiSsm
{
  FpDevice               *dev;
  const char             *name;
  FpiSsm                 *parentsm;
  gpointer                ssm_data;
  GDestroyNotify          ssm_data_destroy;
  int                     nr_states;
  int                     start_cleanup;
  int                     cur_state;
  gboolean                completed;
  GSource                *timeout;
  GError                 *error;
  FpiSsmCompletedCallback callback;
  FpiSsmHandlerCallback   handler;
};

/**
 * fpi_ssm_new:
 * @dev: a #fp_dev fingerprint device
 * @handler: the callback function
 * @nr_states: the number of states
 *
 * Allocate a new ssm, with @nr_states states. The @handler callback
 * will be called after each state transition.
 * This is a macro that calls fpi_ssm_new_full() using @nr_states as the
 * cleanup states and using the stringified version of @nr_states. It should
 * be used with an enum value.
 *
 * Returns: a new #FpiSsm state machine
 */

/**
 * fpi_ssm_new_full:
 * @dev: a #fp_dev fingerprint device
 * @handler: the callback function
 * @nr_states: the number of states
 * @start_cleanup: the first cleanup state
 * @machine_name: the name of the state machine (for debug purposes)
 *
 * Allocate a new ssm, with @nr_states states. The @handler callback
 * will be called after each state transition.
 *
 * Returns: a new #FpiSsm state machine
 */
FpiSsm *
fpi_ssm_new_full (FpDevice             *dev,
                  FpiSsmHandlerCallback handler,
                  int                   nr_states,
                  int                   start_cleanup,
                  const char           *machine_name)
{
  FpiSsm *machine;

  BUG_ON (dev == NULL);
  BUG_ON (nr_states < 1);
  BUG_ON (start_cleanup < 1);
  BUG_ON (start_cleanup > nr_states);
  BUG_ON (handler == NULL);

  machine = g_new0 (FpiSsm, 1);
  machine->handler = handler;
  machine->nr_states = nr_states;
  machine->start_cleanup = start_cleanup;
  machine->dev = dev;
  machine->name = g_strdup (machine_name);
  machine->completed = TRUE;
  return machine;
}

/**
 * fpi_ssm_set_data:
 * @machine: an #FpiSsm state machine
 * @ssm_data: (nullable): a pointer to machine data
 * @ssm_data_destroy: (nullable): #GDestroyNotify for @ssm_data
 *
 * Sets @machine's data (freeing the existing data, if any).
 */
void
fpi_ssm_set_data (FpiSsm        *machine,
                  gpointer       ssm_data,
                  GDestroyNotify ssm_data_destroy)
{
  g_return_if_fail (machine);

  if (machine->ssm_data_destroy && machine->ssm_data)
    machine->ssm_data_destroy (machine->ssm_data);

  machine->ssm_data = ssm_data;
  machine->ssm_data_destroy = ssm_data_destroy;
}

/**
 * fpi_ssm_get_data:
 * @machine: an #FpiSsm state machine
 *
 * Retrieve the pointer to SSM data set with fpi_ssm_set_ssm_data()
 *
 * Returns: a pointer
 */
void *
fpi_ssm_get_data (FpiSsm *machine)
{
  g_return_val_if_fail (machine, NULL);

  return machine->ssm_data;
}

/**
 * fpi_ssm_get_device:
 * @machine: an #FpiSsm state machine
 *
 * Retrieve the device that the SSM is for.
 *
 * Returns: #FpDevice
 */
FpDevice *
fpi_ssm_get_device (FpiSsm *machine)
{
  g_return_val_if_fail (machine, NULL);

  return machine->dev;
}

static void
fpi_ssm_clear_delayed_action (FpiSsm *machine)
{
  g_return_if_fail (machine);

  g_clear_pointer (&machine->timeout, g_source_destroy);
}

static void
fpi_ssm_set_delayed_action_timeout (FpiSsm        *machine,
                                    int            delay,
                                    FpTimeoutFunc  callback,
                                    gpointer       user_data,
                                    GDestroyNotify destroy_func)
{
  g_return_if_fail (machine);

  BUG_ON (machine->completed);
  BUG_ON (machine->timeout != NULL);

  fpi_ssm_clear_delayed_action (machine);

  machine->timeout = fpi_device_add_timeout (machine->dev, delay, callback,
                                             user_data, destroy_func);
}

/**
 * fpi_ssm_free:
 * @machine: an #FpiSsm state machine
 *
 * Frees a state machine. This does not call any error or success
 * callbacks, so you need to do this yourself.
 */
void
fpi_ssm_free (FpiSsm *machine)
{
  if (!machine)
    return;

  BUG_ON (machine->timeout != NULL);

  if (machine->ssm_data_destroy)
    g_clear_pointer (&machine->ssm_data, machine->ssm_data_destroy);
  g_clear_pointer (&machine->error, g_error_free);
  g_clear_pointer (&machine->name, g_free);
  fpi_ssm_clear_delayed_action (machine);
  g_free (machine);
}

/* Invoke the state handler */
static void
__ssm_call_handler (FpiSsm *machine)
{
  fp_dbg ("[%s] %s entering state %d", fp_device_get_driver (machine->dev),
          machine->name, machine->cur_state);
  machine->handler (machine, machine->dev);
}

/**
 * fpi_ssm_start:
 * @ssm: (transfer full): an #FpiSsm state machine
 * @callback: the #FpiSsmCompletedCallback callback to call on completion
 *
 * Starts a state machine. You can also use this function to restart
 * a completed or failed state machine. The @callback will be called
 * on completion.
 *
 * Note that @ssm will be stolen when this function is called.
 * So that all associated data will be free'ed automatically, after the
 * @callback is ran.
 */
void
fpi_ssm_start (FpiSsm *ssm, FpiSsmCompletedCallback callback)
{
  g_return_if_fail (ssm != NULL);

  BUG_ON (!ssm->completed);
  ssm->callback = callback;
  ssm->cur_state = 0;
  ssm->completed = FALSE;
  ssm->error = NULL;
  __ssm_call_handler (ssm);
}

static void
__subsm_complete (FpiSsm *ssm, FpDevice *_dev, GError *error)
{
  FpiSsm *parent = ssm->parentsm;

  BUG_ON (!parent);
  if (error)
    fpi_ssm_mark_failed (parent, error);
  else
    fpi_ssm_next_state (parent);
}

/**
 * fpi_ssm_start_subsm:
 * @parent: an #FpiSsm state machine
 * @child: an #FpiSsm state machine
 *
 * Starts a state machine as a child of another. if the child completes
 * successfully, the parent will be advanced to the next state. if the
 * child fails, the parent will be marked as failed with the same error code.
 *
 * The child will be automatically freed upon completion or failure.
 */
void
fpi_ssm_start_subsm (FpiSsm *parent, FpiSsm *child)
{
  g_return_if_fail (parent != NULL);
  g_return_if_fail (child != NULL);

  BUG_ON (parent->timeout);
  child->parentsm = parent;

  fpi_ssm_clear_delayed_action (parent);
  fpi_ssm_clear_delayed_action (child);

  fpi_ssm_start (child, __subsm_complete);
}

/**
 * fpi_ssm_mark_completed:
 * @machine: an #FpiSsm state machine
 *
 * Mark a ssm as completed successfully. The callback set when creating
 * the state machine with fpi_ssm_new() will be called synchronously.
 *
 * Note that any later cleanup state will still be executed.
 */
void
fpi_ssm_mark_completed (FpiSsm *machine)
{
  int next_state;

  g_return_if_fail (machine != NULL);

  BUG_ON (machine->completed);
  BUG_ON (machine->timeout != NULL);

  fpi_ssm_clear_delayed_action (machine);

  /* complete in a cleanup state just moves forward one step */
  if (machine->cur_state < machine->start_cleanup)
    next_state = machine->start_cleanup;
  else
    next_state = machine->cur_state + 1;

  if (next_state < machine->nr_states)
    {
      machine->cur_state = next_state;
      __ssm_call_handler (machine);
      return;
    }

  machine->completed = TRUE;

  if (machine->error)
    fp_dbg ("[%s] %s completed with error: %s", fp_device_get_driver (machine->dev),
            machine->name, machine->error->message);
  else
    fp_dbg ("[%s] %s completed successfully", fp_device_get_driver (machine->dev),
            machine->name);
  if (machine->callback)
    {
      GError *error = machine->error ? g_error_copy (machine->error) : NULL;

      machine->callback (machine, machine->dev, error);
    }
  fpi_ssm_free (machine);
}

static void
on_device_timeout_complete (FpDevice *dev,
                            gpointer  user_data)
{
  FpiSsm *machine = user_data;

  machine->timeout = NULL;
  fpi_ssm_mark_completed (machine);
}

/**
 * fpi_ssm_mark_completed_delayed:
 * @machine: an #FpiSsm state machine
 * @delay: the milliseconds to wait before switching to the next state
 *
 * Mark a ssm as completed successfully with a delay of @delay ms.
 * The callback set when creating the state machine with fpi_ssm_new () will be
 * called when the timeout is over.
 */
void
fpi_ssm_mark_completed_delayed (FpiSsm *machine,
                                int     delay)
{
  g_autofree char *source_name = NULL;

  g_return_if_fail (machine != NULL);

  fpi_ssm_set_delayed_action_timeout (machine, delay,
                                      on_device_timeout_complete,
                                      machine, NULL);

  source_name = g_strdup_printf ("[%s] ssm %s complete %d",
                                 fp_device_get_device_id (machine->dev),
                                 machine->name, machine->cur_state + 1);
  g_source_set_name (machine->timeout, source_name);
}

/**
 * fpi_ssm_mark_failed:
 * @machine: an #FpiSsm state machine
 * @error: (transfer full): a #GError
 *
 * Mark a state machine as failed with @error as the error code, completing it.
 */
void
fpi_ssm_mark_failed (FpiSsm *machine, GError *error)
{
  g_return_if_fail (machine != NULL);
  g_assert (error);

  /* During cleanup it is OK to call fpi_ssm_mark_failed a second time */
  if (machine->error && machine->cur_state < machine->start_cleanup)
    {
      fp_warn ("[%s] SSM %s already has an error set, ignoring new error %s",
               fp_device_get_driver (machine->dev), machine->name, error->message);
      g_error_free (error);
      return;
    }

  fp_dbg ("[%s] SSM %s failed in state %d%s with error: %s",
          fp_device_get_driver (machine->dev), machine->name,
          machine->cur_state,
          machine->cur_state >= machine->start_cleanup ? " (cleanup)" : "",
          error->message);
  if (!machine->error)
    machine->error = g_steal_pointer (&error);
  else
    g_error_free (error);
  fpi_ssm_mark_completed (machine);
}

/**
 * fpi_ssm_next_state:
 * @machine: an #FpiSsm state machine
 *
 * Iterate to next state of a state machine. If the current state is the
 * last state, then the state machine will be marked as completed, as
 * if calling fpi_ssm_mark_completed().
 */
void
fpi_ssm_next_state (FpiSsm *machine)
{
  g_return_if_fail (machine != NULL);

  BUG_ON (machine->completed);
  BUG_ON (machine->timeout != NULL);

  fpi_ssm_clear_delayed_action (machine);

  machine->cur_state++;
  if (machine->cur_state == machine->nr_states)
    fpi_ssm_mark_completed (machine);
  else
    __ssm_call_handler (machine);
}

void
fpi_ssm_cancel_delayed_state_change (FpiSsm *machine)
{
  g_return_if_fail (machine);
  BUG_ON (machine->completed);
  BUG_ON (machine->timeout == NULL);

  fp_dbg ("[%s] %s cancelled delayed state change",
          fp_device_get_driver (machine->dev), machine->name);

  fpi_ssm_clear_delayed_action (machine);
}

static void
on_device_timeout_next_state (FpDevice *dev,
                              gpointer  user_data)
{
  FpiSsm *machine = user_data;

  machine->timeout = NULL;
  fpi_ssm_next_state (machine);
}

/**
 * fpi_ssm_next_state_delayed:
 * @machine: an #FpiSsm state machine
 * @delay: the milliseconds to wait before switching to the next state
 *
 * Iterate to next state of a state machine with a delay of @delay ms. If the
 * current state is the last state, then the state machine will be marked as
 * completed, as if calling fpi_ssm_mark_completed().
 */
void
fpi_ssm_next_state_delayed (FpiSsm *machine,
                            int     delay)
{
  g_autofree char *source_name = NULL;

  g_return_if_fail (machine != NULL);

  fpi_ssm_set_delayed_action_timeout (machine, delay,
                                      on_device_timeout_next_state,
                                      machine, NULL);

  source_name = g_strdup_printf ("[%s] ssm %s jump to next state %d",
                                 fp_device_get_device_id (machine->dev),
                                 machine->name, machine->cur_state + 1);
  g_source_set_name (machine->timeout, source_name);
}

/**
 * fpi_ssm_jump_to_state:
 * @machine: an #FpiSsm state machine
 * @state: the state to jump to
 *
 * Jump to the @state state, bypassing intermediary states.
 * If @state is the last state, the machine won't be completed unless
 * fpi_ssm_mark_completed() isn't explicitly called.
 */
void
fpi_ssm_jump_to_state (FpiSsm *machine, int state)
{
  g_return_if_fail (machine != NULL);

  BUG_ON (machine->completed);
  BUG_ON (state < 0 || state > machine->nr_states);
  BUG_ON (machine->timeout != NULL);

  fpi_ssm_clear_delayed_action (machine);

  machine->cur_state = state;
  if (machine->cur_state == machine->nr_states)
    fpi_ssm_mark_completed (machine);
  else
    __ssm_call_handler (machine);
}

typedef struct
{
  FpiSsm *machine;
  int     next_state;
} FpiSsmJumpToStateDelayedData;

static void
on_device_timeout_jump_to_state (FpDevice *dev,
                                 gpointer  user_data)
{
  FpiSsmJumpToStateDelayedData *data = user_data;

  data->machine->timeout = NULL;
  fpi_ssm_jump_to_state (data->machine, data->next_state);
}

/**
 * fpi_ssm_jump_to_state_delayed:
 * @machine: an #FpiSsm state machine
 * @state: the state to jump to
 * @delay: the milliseconds to wait before switching to @state state
 *
 * Jump to the @state state with a delay of @delay milliseconds, bypassing
 * intermediary states.
 */
void
fpi_ssm_jump_to_state_delayed (FpiSsm *machine,
                               int     state,
                               int     delay)
{
  FpiSsmJumpToStateDelayedData *data;
  g_autofree char *source_name = NULL;

  g_return_if_fail (machine != NULL);
  BUG_ON (state < 0 || state > machine->nr_states);

  data = g_new0 (FpiSsmJumpToStateDelayedData, 1);
  data->machine = machine;
  data->next_state = state;

  fpi_ssm_set_delayed_action_timeout (machine, delay,
                                      on_device_timeout_jump_to_state,
                                      data, g_free);

  source_name = g_strdup_printf ("[%s] ssm %s jump to state %d",
                                 fp_device_get_device_id (machine->dev),
                                 machine->name, state);
  g_source_set_name (machine->timeout, source_name);
}

/**
 * fpi_ssm_get_cur_state:
 * @machine: an #FpiSsm state machine
 *
 * Returns the value of the current state. Note that states are
 * 0-indexed, so a value of 0 means “the first state”.
 *
 * Returns: the current state.
 */
int
fpi_ssm_get_cur_state (FpiSsm *machine)
{
  g_return_val_if_fail (machine != NULL, 0);

  return machine->cur_state;
}

/**
 * fpi_ssm_get_error:
 * @machine: an #FpiSsm state machine
 *
 * Returns the error code set by fpi_ssm_mark_failed().
 *
 * Returns: (transfer none): a error code
 */
GError *
fpi_ssm_get_error (FpiSsm *machine)
{
  g_return_val_if_fail (machine != NULL, NULL);

  return machine->error;
}

/**
 * fpi_ssm_dup_error:
 * @machine: an #FpiSsm state machine
 *
 * Returns the error code set by fpi_ssm_mark_failed().
 *
 * Returns: (transfer full): a error code
 */
GError *
fpi_ssm_dup_error (FpiSsm *machine)
{
  g_return_val_if_fail (machine != NULL, NULL);

  if (machine->error)
    return g_error_copy (machine->error);

  return NULL;
}

/**
 * fpi_ssm_usb_transfer_cb:
 * @transfer: a #FpiUsbTransfer
 * @device: a #FpDevice
 * @unused_data: User data (unused)
 * @error: The #GError or %NULL
 *
 * Can be used in as a #FpiUsbTransfer callback handler to automatically
 * advance or fail a statemachine on transfer completion.
 *
 * Make sure to set the #FpiSsm on the transfer.
 */
void
fpi_ssm_usb_transfer_cb (FpiUsbTransfer *transfer, FpDevice *device,
                         gpointer unused_data, GError *error)
{
  g_return_if_fail (transfer->ssm);

  if (error)
    fpi_ssm_mark_failed (transfer->ssm, error);
  else
    fpi_ssm_next_state (transfer->ssm);
}

/**
 * fpi_ssm_usb_transfer_with_weak_pointer_cb:
 * @transfer: a #FpiUsbTransfer
 * @device: a #FpDevice
 * @weak_ptr: A #gpointer pointer to nullify. You can pass a pointer to any
 *            #gpointer to nullify when the callback is completed. I.e a
 *            pointer to the current #FpiUsbTransfer.
 * @error: The #GError or %NULL
 *
 * Can be used in as a #FpiUsbTransfer callback handler to automatically
 * advance or fail a statemachine on transfer completion.
 * Passing a #gpointer* as @weak_ptr permits to nullify it once we're done
 * with the transfer.
 *
 * Make sure to set the #FpiSsm on the transfer.
 */
void
fpi_ssm_usb_transfer_with_weak_pointer_cb (FpiUsbTransfer *transfer,
                                           FpDevice *device, gpointer weak_ptr,
                                           GError *error)
{
  g_return_if_fail (transfer->ssm);

  if (weak_ptr)
    g_nullify_pointer ((gpointer *) weak_ptr);

  fpi_ssm_usb_transfer_cb (transfer, device, weak_ptr, error);
}

/**
 * fpi_ssm_spi_transfer_cb:
 * @transfer: a #FpiSpiTransfer
 * @device: a #FpDevice
 * @unused_data: User data (unused)
 * @error: The #GError or %NULL
 *
 * Can be used in as a #FpiSpiTransfer callback handler to automatically
 * advance or fail a statemachine on transfer completion.
 *
 * Make sure to set the #FpiSsm on the transfer.
 */
void
fpi_ssm_spi_transfer_cb (FpiSpiTransfer *transfer, FpDevice *device,
                         gpointer unused_data, GError *error)
{
  g_return_if_fail (transfer->ssm);

  if (error)
    fpi_ssm_mark_failed (transfer->ssm, error);
  else
    fpi_ssm_next_state (transfer->ssm);
}

/**
 * fpi_ssm_spi_transfer_with_weak_pointer_cb:
 * @transfer: a #FpiSpiTransfer
 * @device: a #FpDevice
 * @weak_ptr: A #gpointer pointer to nullify. You can pass a pointer to any
 *            #gpointer to nullify when the callback is completed. I.e a
 *            pointer to the current #FpiSpiTransfer.
 * @error: The #GError or %NULL
 *
 * Can be used in as a #FpiSpiTransfer callback handler to automatically
 * advance or fail a statemachine on transfer completion.
 * Passing a #gpointer* as @weak_ptr permits to nullify it once we're done
 * with the transfer.
 *
 * Make sure to set the #FpiSsm on the transfer.
 */
void
fpi_ssm_spi_transfer_with_weak_pointer_cb (FpiSpiTransfer *transfer,
                                           FpDevice *device, gpointer weak_ptr,
                                           GError *error)
{
  g_return_if_fail (transfer->ssm);

  if (weak_ptr)
    g_nullify_pointer ((gpointer *) weak_ptr);

  fpi_ssm_spi_transfer_cb (transfer, device, weak_ptr, error);
}
