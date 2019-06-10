/*
 * Polling/timing management
 * Copyright (C) 2008 Daniel Drake <dsd@gentoo.org>
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

#define FP_COMPONENT "poll"

#include "fp_internal.h"
#include "fpi-poll.h"

#include <config.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

#include <glib.h>
#include <glib-unix.h>
#include <libusb.h>

/**
 * SECTION:events
 * @title: Initialisation and events handling
 * @short_description: Initialisation and events handling functions
 *
 * These functions are only applicable to users of libfprint's asynchronous
 * API.
 *
 * libfprint does not create internal library threads and hence can only
 * execute when your application is calling a libfprint function. However,
 * libfprint often has work to be do, such as handling of completed USB
 * transfers, and processing of timeouts required in order for the library
 * to function. Therefore it is essential that your own application must
 * regularly "phone into" libfprint so that libfprint can handle any pending
 * events.
 *
 * The function you must call is fp_handle_events() or a variant of it. This
 * function will handle any pending events, and it is from this context that
 * all asynchronous event callbacks from the library will occur. You can view
 * this function as a kind of iteration function.
 *
 * If there are no events pending, fp_handle_events() will block for a few
 * seconds (and will handle any new events should anything occur in that time).
 * If you wish to customise this timeout, you can use
 * fp_handle_events_timeout() instead. If you wish to do a non-blocking
 * iteration, call fp_handle_events_timeout() with a zero timeout.
 *
 * How to integrate events handling depends on your main loop implementation.
 * The sister fprintd project includes an implementation of main loop handling
 * that integrates into GLib's main loop. The
 * [libusb documentation](http://libusb.sourceforge.net/api-1.0/group__poll.html#details)
 * also includes more details about how to integrate libfprint events into
 * your main loop.
 */

/**
 * SECTION:fpi-poll
 * @title: Timeouts
 * @short_description: Timeout handling helpers
 *
 * Helper functions to schedule a function call to be made after a timeout. This
 * is useful to avoid making blocking calls while waiting for hardware to answer
 * for example.
 */

static GMainContext *fpi_main_ctx = NULL;
static GSList *active_timers = NULL;

/* notifiers for added or removed poll fds */
static fp_pollfd_added_cb fd_added_cb = NULL;
static fp_pollfd_removed_cb fd_removed_cb = NULL;


struct fpi_timeout {
	fpi_timeout_fn  callback;
	struct fp_dev  *dev;
	void           *data;
	GSource        *source;
};

static void
fpi_timeout_destroy (gpointer data)
{
	fpi_timeout *timeout = data;

	active_timers = g_slist_remove (active_timers, timeout);
	g_free (timeout);
}

static gboolean
fpi_timeout_wrapper_cb (gpointer data)
{
	fpi_timeout *timeout = (fpi_timeout*) data;

	timeout->callback (timeout->dev, timeout->data);

	return G_SOURCE_REMOVE;
}

/**
 * fpi_timeout_add:
 * @msec: the time before calling the function, in milliseconds (1/1000ths of a second)
 * @callback: function to callback
 * @dev: a struct #fp_dev
 * @data: data to pass to @callback, or %NULL
 *
 * A timeout is the asynchronous equivalent of sleeping. You create a timeout
 * saying that you'd like to have a function invoked at a certain time in
 * the future.
 *
 * Note that you should hold onto the return value of this function to cancel it
 * use fpi_timeout_cancel(), otherwise the callback could be called while the driver
 * is being torn down.
 *
 * This function can be considered to never fail.
 *
 * Returns: an #fpi_timeout structure
 */
fpi_timeout *
fpi_timeout_add(unsigned int    msec,
		fpi_timeout_fn  callback,
		struct fp_dev  *dev,
		void           *data)
{
	fpi_timeout *timeout;

	timeout = g_new0 (fpi_timeout, 1);
	timeout->source = g_timeout_source_new (msec);
	active_timers = g_slist_prepend (active_timers, timeout);

	g_source_set_callback (timeout->source, fpi_timeout_wrapper_cb, timeout, fpi_timeout_destroy);
	g_source_attach (timeout->source, fpi_main_ctx);

	return timeout;
}

/**
 * fpi_timeout_set_name:
 * @timeout: a #fpi_timeout
 * @name: the name to give the timeout
 *
 * Sets a name for a timeout, allowing that name to be printed
 * along with any timeout related debug.
 */
void
fpi_timeout_set_name(fpi_timeout *timeout,
		     const char  *name)
{
	g_source_set_name (timeout->source, name);
}

/**
 * fpi_timeout_cancel:
 * @timeout: an #fpi_timeout structure
 *
 * Cancels a timeout scheduled with fpi_timeout_add(), and frees the
 * @timeout structure.
 */
void
fpi_timeout_cancel(fpi_timeout *timeout)
{
	g_source_destroy (timeout->source);
}

struct fpi_io_condition {
	fpi_io_condition_fn  callback;
	int                  fd;
	struct fp_dev       *dev;
	void                *data;
	GSource             *source;
};

static gboolean
fpi_io_condition_wrapper_cb (int fd, GIOCondition cond, gpointer data)
{
	fpi_io_condition *io_cond = data;
	short events = 0;

	if (cond & G_IO_IN)
		events |= POLL_IN;
	if (cond & G_IO_OUT)
		events |= POLL_OUT;
	if (cond & G_IO_PRI)
		events |= POLL_PRI;
	if (cond & G_IO_ERR)
		events |= POLL_ERR;
	if (cond & G_IO_HUP)
		events |= POLL_HUP;

	io_cond->callback (io_cond->dev, fd, cond, io_cond->data);

	return G_SOURCE_CONTINUE;
}

static void
fpi_io_condition_destroy (gpointer data)
{
	fpi_io_condition *io_cond = data;

	if (fd_removed_cb)
		fd_removed_cb(io_cond->fd);

	g_free (io_cond);
}

fpi_io_condition *
fpi_io_condition_add(int                  fd,
		     short int            events,
		     fpi_io_condition_fn  callback,
		     struct fp_dev       *dev,
		     void                *data)
{
	fpi_io_condition *io_cond;
	GIOCondition cond = 0;

	if (events & POLL_IN)
		cond |= G_IO_IN;
	if (events & POLL_OUT)
		cond |= G_IO_OUT;
	if (events & POLL_PRI)
		cond |= G_IO_PRI;
	if (events & POLL_ERR)
		cond |= G_IO_ERR;
	if (events & POLL_HUP)
		cond |= G_IO_HUP;

	io_cond = g_new0 (fpi_io_condition, 1);
	io_cond->source = g_unix_fd_source_new (fd, cond);
	io_cond->fd = fd;
	io_cond->callback = callback;
	io_cond->data = data;
	io_cond->dev = dev;

	g_source_set_callback (io_cond->source,
			       G_SOURCE_FUNC (fpi_io_condition_wrapper_cb),
			       io_cond,
			       fpi_io_condition_destroy);
	g_source_attach (io_cond->source, fpi_main_ctx);

	if (fd_added_cb)
		fd_added_cb(fd, events);

	return io_cond;
}

void
fpi_io_condition_set_name(fpi_io_condition *io_cond,
			  const char       *name)
{
	g_source_set_name (io_cond->source, name);
}

void
fpi_io_condition_remove(fpi_io_condition *io_cond)
{
	g_source_destroy(io_cond->source);
}

static gboolean
dummy_cb (gpointer user_data)
{
	return G_SOURCE_REMOVE;
}

/**
 * fp_handle_events_timeout:
 * @timeout: Maximum timeout for this blocking function
 *
 * Handle any pending events. If a non-zero timeout is specified, the function
 * will potentially block for the specified amount of time, although it may
 * return sooner if events have been handled. The function acts as non-blocking
 * for a zero timeout.
 *
 * Returns: 0 on success, non-zero on error.
 */
API_EXPORTED int fp_handle_events_timeout(struct timeval *timeout)
{
	GSource *timeout_source;

	if (timeout->tv_sec == 0 && timeout->tv_usec == 0) {
		g_main_context_iteration (fpi_main_ctx, FALSE);
		return 0;
	}

	/* Register a timeout on the mainloop and then run in blocking mode */
	timeout_source = g_timeout_source_new (timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
	g_source_set_name (timeout_source, "fpi poll timeout");
	g_source_set_callback (timeout_source, dummy_cb, NULL, NULL);
	g_source_attach (timeout_source, fpi_main_ctx);
	g_main_context_iteration (fpi_main_ctx, TRUE);
	g_source_destroy (timeout_source);

	return 0;
}

/**
 * fp_handle_events:
 *
 * Convenience function for calling fp_handle_events_timeout() with a sensible
 * default timeout value of two seconds (subject to change if we decide another
 * value is more sensible).
 *
 * Returns: 0 on success, non-zero on error.
 */
API_EXPORTED int fp_handle_events(void)
{
	struct timeval tv;
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	return fp_handle_events_timeout(&tv);
}

/**
 * fp_get_next_timeout:
 * @tv: a #timeval structure containing the duration to the next timeout.
 *
 * A zero filled @tv timeout means events are to be handled immediately
 *
 * Returns: returns 0 if no timeouts active, or 1 if timeout returned.
 */
API_EXPORTED int fp_get_next_timeout(struct timeval *tv)
{
	int timeout_;

	g_return_val_if_fail (g_main_context_acquire (fpi_main_ctx), 0);

	g_main_context_query (fpi_main_ctx,
			      G_MININT,
			      &timeout_,
			      NULL,
			      0);

	if (timeout_ < 0)
		return 0;

	tv->tv_sec = timeout_ / 1000;
	tv->tv_usec = (timeout_ % 1000) * 1000;

	return 1;
}

typedef struct {
	GSource source;
	GSList *fds;
} fpi_libusb_source;

typedef struct {
	int fd;
	gpointer tag;
} fpi_libusb_fd;

static fpi_libusb_source *libusb_source = NULL;

/**
 * fp_get_pollfds:
 * @pollfds: output location for a list of pollfds. If non-%NULL, must be
 * released with free() when done.
 *
 * Retrieve a list of file descriptors that should be polled for events
 * interesting to libfprint. This function is only for users who wish to
 * combine libfprint's file descriptor set with other event sources – more
 * simplistic users will be able to call fp_handle_events() or a variant
 * directly.
 *
 * Returns: the number of pollfds in the resultant list, or negative on error.
 */
API_EXPORTED ssize_t fp_get_pollfds(struct fp_pollfd **pollfds)
{
	gint timeout_;
	GPollFD fds_static[16];
	GPollFD *fds = fds_static;
	gint n_fds;
	int i;

	g_return_val_if_fail (g_main_context_acquire (fpi_main_ctx), -1);

	n_fds = g_main_context_query (fpi_main_ctx,
				      G_MININT,
				      &timeout_,
				      fds,
				      G_N_ELEMENTS (fds_static));

	if (n_fds > G_N_ELEMENTS (fds_static)) {
		fds = g_new0 (GPollFD, n_fds);

		n_fds = g_main_context_query (fpi_main_ctx,
					      G_MININT,
					      &timeout_,
					      fds,
					      n_fds);
	}

	g_main_context_release (fpi_main_ctx);

	*pollfds = g_new0 (struct fp_pollfd, n_fds);
	for (i = 0; i < n_fds; i++) {
		(*pollfds)[i].fd = fds[i].fd;

		if (fds[i].events & G_IO_IN)
			(*pollfds)[i].events |= POLL_IN;
		if (fds[i].events & G_IO_OUT)
			(*pollfds)[i].events |= POLL_OUT;
		if (fds[i].events & G_IO_PRI)
			(*pollfds)[i].events |= POLL_PRI;
		if (fds[i].events & G_IO_ERR)
			(*pollfds)[i].events |= POLL_ERR;
		if (fds[i].events & G_IO_HUP)
			(*pollfds)[i].events |= POLL_HUP;
	}

	if (fds != fds_static)
		g_free (fds);

	return n_fds;
}

/**
 * fp_set_pollfd_notifiers:
 * @added_cb: a #fp_pollfd_added_cb callback or %NULL
 * @removed_cb: a #fp_pollfd_removed_cb callback or %NULL
 *
 * This sets the callback functions to call for every new or removed
 * file descriptor used as an event source.
 */
API_EXPORTED void fp_set_pollfd_notifiers(fp_pollfd_added_cb added_cb,
	fp_pollfd_removed_cb removed_cb)
{
	fd_added_cb = added_cb;
	fd_removed_cb = removed_cb;
}

static void add_pollfd(int fd, short events, void *user_data)
{
	GIOCondition io_cond = 0;
	fpi_libusb_fd *data;
	gpointer tag;

	if (events & POLL_IN)
		io_cond |= G_IO_IN;
	if (events & POLL_OUT)
		io_cond |= G_IO_OUT;
	if (events & POLL_PRI)
		io_cond |= G_IO_PRI;
	if (events & POLL_ERR)
		io_cond |= G_IO_ERR;
	if (events & POLL_HUP)
		io_cond |= G_IO_HUP;

	tag = g_source_add_unix_fd (&libusb_source->source, fd, io_cond);

	data = g_new0 (fpi_libusb_fd, 1);
	data->fd = fd;
	data->tag = tag;

	libusb_source->fds = g_slist_prepend (libusb_source->fds, data);

	if (fd_added_cb)
		fd_added_cb(fd, events);
}

static void remove_pollfd(int fd, void *user_data)
{
	GSList *elem = g_slist_find_custom (libusb_source->fds, &fd, g_int_equal);
	fpi_libusb_fd *item;

	g_return_if_fail (elem != NULL);

	item = (fpi_libusb_fd*) elem->data;

	g_source_remove_unix_fd (&libusb_source->source, item->tag);

	libusb_source->fds = g_slist_remove_link (libusb_source->fds, elem);
	g_slist_free (elem);
	g_free (item);

	if (fd_removed_cb)
		fd_removed_cb(fd);
}

static gboolean
fpi_libusb_prepare (GSource    *source,
                    gint       *timeout_)
{
	struct timeval tv;

	*timeout_ = -1;

	if (libusb_get_next_timeout(fpi_usb_ctx, &tv) == 1) {
		if (tv.tv_sec == 0 && tv.tv_usec == 0)
			return TRUE;

		*timeout_ = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	}

	return FALSE;
}

static gboolean
fpi_libusb_check (GSource *source)
{
	/* Just call into libusb for every mainloop cycle */
	return TRUE;
}

static gboolean
fpi_libusb_dispatch (GSource     *source,
                     GSourceFunc  callback,
                     gpointer     user_data)
{
	struct timeval zero_tv = { 0, 0 };

	libusb_handle_events_timeout (fpi_usb_ctx, &zero_tv);

	return G_SOURCE_CONTINUE;
}

static void
fpi_libusb_finalize (GSource *source)
{
	fpi_libusb_source *fpi_source = (fpi_libusb_source*) source;

	g_slist_free_full (fpi_source->fds, g_free);
}

GSourceFuncs libusb_source_funcs = {
	.prepare = fpi_libusb_prepare,
	.check = fpi_libusb_check,
	.dispatch = fpi_libusb_dispatch,
	.finalize = fpi_libusb_finalize,
};

void fpi_poll_init(void)
{
	fpi_main_ctx = g_main_context_new ();

	libusb_source = (fpi_libusb_source*) g_source_new (&libusb_source_funcs, sizeof(fpi_libusb_source));
	g_source_set_name (&libusb_source->source, "libfprint internal libusb source");
	g_source_attach (&libusb_source->source, fpi_main_ctx);

	libusb_set_pollfd_notifiers(fpi_usb_ctx, add_pollfd, remove_pollfd, NULL);
}

void fpi_poll_exit(void)
{
	g_source_destroy (&libusb_source->source);
	libusb_source = NULL;
	g_main_context_unref (fpi_main_ctx);
	fpi_main_ctx = NULL;

	fd_added_cb = NULL;
	fd_removed_cb = NULL;

	libusb_set_pollfd_notifiers(fpi_usb_ctx, NULL, NULL, NULL);
}


void
fpi_timeout_cancel_all_for_dev(struct fp_dev *dev)
{
	GSList *l;

	g_return_if_fail (dev != NULL);

	l = active_timers;
	while (l) {
		fpi_timeout *cb_data = l->data;

		l = l->next;
		if (cb_data->dev == dev)
			g_source_destroy (cb_data->source);
	}
}
