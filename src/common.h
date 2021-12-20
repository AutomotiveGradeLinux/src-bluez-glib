/*
 * Copyright 2018-2021 Konsulko Group
 * Author: Matt Ranostay <matt.ranostay@konsulko.com>
 * Author: Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef BLUEZ_COMMON_H
#define BLUEZ_COMMON_H

#include <glib.h>
#include <gio/gio.h>

#include "bluez-glib.h"

// Marker for exposed API functions
#define EXPORT	__attribute__ ((visibility("default")))

// Global knob for some more expensive verbose debug logging
#undef BLUEZ_GLIB_DEBUG

struct call_work;

struct bluez_state {
	GMainLoop *loop;
	GDBusConnection *conn;
	guint device_sub;
	guint autoconnect_sub;

	/* NOTE: single connection allowed for now */
	/* NOTE: needs locking and a list */
	GMutex cw_mutex;
	int next_cw_id;
	GSList *cw_pending;
	struct call_work *cw;

	/* agent */
	GDBusNodeInfo *introspection_data;
	guint agent_id;
	guint registration_id;
	gchar *agent_path;
	gboolean agent_registered;

	/* mediaplayer */
	gchar *mediaplayer_path;

	/* default adapter */
	gchar *default_adapter;

	/* adapter */
	gchar *adapter;
};

struct init_data {
	GCond cond;
	GMutex mutex;
	gboolean register_agent;
	gboolean autoconnect;
	gboolean init_done;
	struct bluez_state *ns; // for bluez_register_agent
	gboolean rc;
	void (*init_done_cb)(struct init_data *id, gboolean rc);
	bluez_init_cb_t cb;
	gpointer user_data;
};

extern void bluez_log(bluez_log_level_t level, const char *func, const char *format, ...)
	__attribute__ ((format (printf, 3, 4)));

#define ERROR(format, ...) \
	bluez_log(BLUEZ_LOG_LEVEL_ERROR, __FUNCTION__, format, ##__VA_ARGS__)

#define WARNING(format, ...) \
	bluez_log(BLUEZ_LOG_LEVEL_WARNING, __FUNCTION__, format, ##__VA_ARGS__)

#define INFO(format, ...) \
	bluez_log(BLUEZ_LOG_LEVEL_INFO, __FUNCTION__, format, ##__VA_ARGS__)

#define DEBUG(format, ...) \
	bluez_log(BLUEZ_LOG_LEVEL_DEBUG, __FUNCTION__, format, ##__VA_ARGS__)

#endif /* BLUEZ_COMMON_H */
