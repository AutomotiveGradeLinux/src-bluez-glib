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

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include <glib.h>
#include <stdlib.h>
#include <gio/gio.h>
#include <glib-object.h>

#include "bluez-glib.h"
#include "common.h"
#include "conf.h"
#include "call_work.h"
#include "bluez-call.h"
#include "bluez-agent.h"

typedef struct bluez_signal_callback_list_entry_t {
	gpointer callback;
	gpointer user_data;
} callback_list_entry_t;

typedef struct {
	GMutex mutex;
	GSList *list;
} callback_list_t;

callback_list_t bluez_adapter_callbacks;
callback_list_t bluez_device_callbacks;
callback_list_t bluez_media_control_callbacks;
callback_list_t bluez_media_player_callbacks;

// The global handler thread and BlueZ state
static GThread *g_bluez_thread;
static struct bluez_state *g_bluez_state;

// Global log level
static bluez_log_level_t g_bluez_log_level = BLUEZ_LOG_LEVEL_DEFAULT;

static const char *g_bluez_log_level_names[BLUEZ_LOG_LEVEL_DEBUG + 1] = {
	"ERROR",
	"WARNING",
	"INFO",
	"DEBUG"
};

// Wrappers to hedge possible future abstractions
static void bluez_set_state(struct bluez_state *ns)
{
	g_bluez_state = ns;
}

static struct bluez_state *bluez_get_state(void)
{
	return g_bluez_state;
}

EXPORT void bluez_set_log_level(bluez_log_level_t level)
{
	g_bluez_log_level = level;
}

void bluez_log(bluez_log_level_t level, const char *func, const char *format, ...)
{
	FILE *out = stdout;

	if (level > g_bluez_log_level)
		return;

	if (level == BLUEZ_LOG_LEVEL_ERROR)
		out = stderr;

	va_list args;
	va_start(args, format);
	fprintf(out, "%s: %s: ", g_bluez_log_level_names[level], func);
        gchar *format_line = g_strconcat(format, "\n", NULL);
	vfprintf(out, format_line, args);
	va_end(args);
	g_free(format_line);
}

static void callback_add(callback_list_t *callbacks, gpointer callback, gpointer user_data)
{
	callback_list_entry_t *entry = NULL;

	if(!callbacks)
		return;

	g_mutex_lock(&callbacks->mutex);
	entry = g_malloc0(sizeof(*entry));
	entry->callback = callback;
	entry->user_data = user_data;
	callbacks->list = g_slist_append(callbacks->list, entry);
	g_mutex_unlock(&callbacks->mutex);
}

static void callback_remove(callback_list_t *callbacks, gpointer callback)
{
	callback_list_entry_t *entry = NULL;
	GSList *list;

	if(!(callbacks && callbacks->list))
		return;

	g_mutex_lock(&callbacks->mutex);
	for (list = callbacks->list; list; list = g_slist_next(list)) {
		entry = list->data;
		if (entry->callback == callback)
			break;
		entry = NULL;
	}
	if (entry) {
		callbacks->list = g_slist_remove(callbacks->list, entry);
		g_free(entry);
	}
	g_mutex_unlock(&callbacks->mutex);
}

static void run_callbacks(callback_list_t *callbacks,
			  gchar *adapter,
			  gchar *device,
			  bluez_event_t event,
			  GVariant *properties)
{
	GSList *list;

	if (!(adapter || device))
		return;

	g_mutex_lock(&callbacks->mutex);
	for (list = callbacks->list; list; list = g_slist_next(list)) {
		callback_list_entry_t *entry = list->data;
		if (entry->callback) {
			if (device) {
				bluez_device_event_cb_t cb = (bluez_device_event_cb_t) entry->callback;
				(*cb)(adapter, device, event, properties, entry->user_data);
			} else {
				bluez_adapter_event_cb_t cb = (bluez_adapter_event_cb_t) entry->callback;
				(*cb)(adapter, event, properties, entry->user_data);
			}
		}
	}
	g_mutex_unlock(&callbacks->mutex);
}

static void run_media_callbacks(callback_list_t *callbacks,
				gchar *adapter,
				gchar *device,
				gchar *endpoint,
				gchar *player,
				bluez_event_t event,
				GVariant *properties)
{
	GSList *list;

	if (!(endpoint || player))
		return;

	g_mutex_lock(&callbacks->mutex);
	for (list = callbacks->list; list; list = g_slist_next(list)) {
		callback_list_entry_t *entry = list->data;
		if (entry->callback) {
			if (player) {
				bluez_media_player_event_cb_t cb = (bluez_media_player_event_cb_t) entry->callback;
				(*cb)(adapter, device, player, event, properties, entry->user_data);
			} else {
				bluez_media_control_event_cb_t cb = (bluez_media_control_event_cb_t) entry->callback;
				(*cb)(adapter, device, endpoint, event, properties, entry->user_data);
			}
		}
	}
	g_mutex_unlock(&callbacks->mutex);
}

EXPORT void bluez_add_adapter_event_callback(bluez_adapter_event_cb_t cb, gpointer user_data)
{
	if (!cb)
		return;

	callback_add(&bluez_adapter_callbacks, cb, user_data);
}

EXPORT void bluez_add_device_event_callback(bluez_device_event_cb_t cb, gpointer user_data)
{
	if (!cb)
		return;

	callback_add(&bluez_device_callbacks, cb, user_data);
}

EXPORT void bluez_add_media_control_event_callback(bluez_media_control_event_cb_t cb, gpointer user_data)
{
	if (!cb)
		return;

	callback_add(&bluez_media_control_callbacks, cb, user_data);
}

EXPORT void bluez_add_media_player_event_callback(bluez_media_player_event_cb_t cb, gpointer user_data)
{
	if (!cb)
		return;

	callback_add(&bluez_media_player_callbacks, cb, user_data);
}

static void mediaplayer1_set_path(struct bluez_state *ns, const char *path)
{
	if (ns->mediaplayer_path)
		g_free(ns->mediaplayer_path);
	ns->mediaplayer_path = g_strdup(path);
}

static void bluez_devices_signal_callback(GDBusConnection *connection,
					  const gchar *sender_name,
					  const gchar *object_path,
					  const gchar *interface_name,
					  const gchar *signal_name,
					  GVariant *parameters,
					  gpointer user_data)
{
	const gchar *path = NULL;
	GVariantIter *array = NULL;
	GVariant *var = NULL;
	const gchar *key = NULL;

#ifdef BLUEZ_GLIB_DEBUG
	INFO("sender=%s", sender_name);
	INFO("object_path=%s", object_path);
	INFO("interface=%s", interface_name);
	INFO("signal=%s", signal_name);
	DEBUG("parameters: %s", g_variant_print(parameters, TRUE));
#endif

	if (!g_strcmp0(signal_name, "InterfacesAdded")) {

		g_variant_get(parameters, "(&oa{sa{sv}})", &path, &array);

		// no adapter or device in path
		if (!g_strcmp0(path, BLUEZ_PATH))
			return;

		while (g_variant_iter_next(array, "{&s@a{sv}}", &key, &var)) {
			if (!g_strcmp0(key, BLUEZ_DEVICE_INTERFACE)) {
				DEBUG("device added!");
				gchar *adapter = bluez_return_adapter(path);
				gchar *device = bluez_return_device(path);
				run_callbacks(&bluez_device_callbacks, adapter, device, BLUEZ_EVENT_ADD, var);
				g_free(adapter);
				g_free(device);

			} else if (!g_strcmp0(key, BLUEZ_ADAPTER_INTERFACE)) {
				DEBUG("adapter added!");
				gchar *adapter = bluez_return_adapter(path);
				run_callbacks(&bluez_adapter_callbacks, adapter, NULL, BLUEZ_EVENT_ADD, var);
				g_free(adapter);

			} else if (!g_strcmp0(key, BLUEZ_MEDIATRANSPORT_INTERFACE)) {
				gchar *adapter = bluez_return_adapter(path);
				gchar *device = bluez_return_device(path);
				gchar *endpoint = bluez_return_endpoint(path);
				DEBUG("media endpoint added!");
				DEBUG("media endpoint path %s, key %s, endpoint %s",
				      path, key, endpoint);
				run_media_callbacks(&bluez_media_control_callbacks,
						    adapter,
						    device,
						    endpoint,
						    NULL,
						    BLUEZ_EVENT_ADD,
						    var);
				g_free(adapter);
				g_free(device);
				g_free(endpoint);

			} else if (!g_strcmp0(key, BLUEZ_MEDIAPLAYER_INTERFACE)) {
				gchar *adapter = bluez_return_adapter(path);
				gchar *device = bluez_return_device(path);
				gchar *player = find_index(path, 5);
				DEBUG("media player removed!");
				DEBUG("media player = %s", player);
				run_media_callbacks(&bluez_media_player_callbacks,
						    adapter,
						    device,
						    NULL,
						    player,
						    BLUEZ_EVENT_ADD,
						    var);
				g_free(adapter);
				g_free(device);
				g_free(player);
			}
			g_variant_unref(var);
		}
		g_variant_iter_free(array);

	} else if (!g_strcmp0(signal_name, "InterfacesRemoved")) {

		g_variant_get(parameters, "(&o@as)", &path, NULL);

		if (is_mediatransport1_interface(path)) {
			gchar *adapter = bluez_return_adapter(path);
			gchar *device = bluez_return_device(path);
			gchar *endpoint = bluez_return_endpoint(path);
			DEBUG("media endpoint removed!");
			DEBUG("media endpoint = %s", endpoint);
			run_media_callbacks(&bluez_media_control_callbacks,
					    adapter,
					    device,
					    endpoint,
					    NULL,
					    BLUEZ_EVENT_REMOVE,
					    NULL);
			g_free(adapter);
			g_free(device);
			g_free(endpoint);

		} else if (is_mediaplayer1_interface(path)) {
			gchar *adapter = bluez_return_adapter(path);
			gchar *device = bluez_return_device(path);
			gchar *player = find_index(path, 5);
			DEBUG("media player removed!");
			DEBUG("media player = %s", player);
			run_media_callbacks(&bluez_media_player_callbacks,
					    adapter,
					    device,
					    NULL,
					    player,
					    BLUEZ_EVENT_REMOVE,
					    NULL);
			g_free(adapter);
			g_free(device);
			g_free(player);

		} else if (split_length(path) == 4) {
			/* adapter removal */
			DEBUG("adapter removed!");
			gchar *adapter = bluez_return_adapter(path);
			run_callbacks(&bluez_adapter_callbacks, adapter, NULL, BLUEZ_EVENT_REMOVE, NULL);
			g_free(adapter);

		} else if (split_length(path) == 5) {
			/* device removal */
			DEBUG("device removed!");
			gchar *adapter = bluez_return_adapter(path);
			gchar *device = bluez_return_device(path);
			run_callbacks(&bluez_device_callbacks, adapter, device, BLUEZ_EVENT_REMOVE, NULL);
			g_free(adapter);
			g_free(device);

		}
	} else if (!g_strcmp0(signal_name, "PropertiesChanged")) {

		g_variant_get(parameters, "(&s@a{sv}@as)", &path, &var, NULL);

		if (!g_strcmp0(path, BLUEZ_DEVICE_INTERFACE)) {
			gchar *adapter = bluez_return_adapter(object_path);
			gchar *device = bluez_return_device(object_path);
			run_callbacks(&bluez_device_callbacks, adapter, device, BLUEZ_EVENT_CHANGE, var);
			g_free(adapter);
			g_free(device);

		} else if (!g_strcmp0(path, BLUEZ_ADAPTER_INTERFACE)) {
			DEBUG("adapter changed!");
			gchar *adapter = bluez_return_adapter(object_path);
			run_callbacks(&bluez_adapter_callbacks, adapter, NULL, BLUEZ_EVENT_CHANGE, var);
			g_free(adapter);

		} else if (!g_strcmp0(path, BLUEZ_MEDIAPLAYER_INTERFACE)) {
			gchar *adapter = bluez_return_adapter(object_path);
			gchar *device = bluez_return_device(object_path);
			gchar *player = find_index(object_path, 5);
			DEBUG("media player changed!");
			DEBUG("media player = %s", player);
			run_media_callbacks(&bluez_media_player_callbacks,
					    adapter,
					    device,
					    NULL,
					    player,
					    BLUEZ_EVENT_CHANGE,
					    var);
			g_free(adapter);
			g_free(device);
			g_free(player);

		} else if (!g_strcmp0(path, BLUEZ_MEDIATRANSPORT_INTERFACE)) {
			gchar *adapter = bluez_return_adapter(object_path);
			gchar *device = bluez_return_device(object_path);
			gchar *endpoint = bluez_return_endpoint(object_path);
			DEBUG("media endpoint changed!");
			DEBUG("media endpoint %s", endpoint);
			run_media_callbacks(&bluez_media_control_callbacks,
					    adapter,
					    device,
					    endpoint,
					    NULL,
					    BLUEZ_EVENT_REMOVE,
					    var);
			g_free(adapter);
			g_free(device);
			g_free(endpoint);

		}
		g_variant_unref(var);
	}
}

// Returns number of adapters present
static int bluez_select_init_adapter(void)
{
	struct bluez_state *ns = bluez_get_state();
	GArray *adapters = NULL;
	gboolean rc;
	int i, n = 0;

	if (!(ns && ns->default_adapter))
		return 0;

	rc = bluez_get_adapters(&adapters);
	if (!rc) {
		return 0;
	}

	for(i = 0; i < adapters->len; i++) {
		gchar *adapter = g_array_index(adapters, gchar*, i);
		if (!g_strcmp0(adapter, ns->default_adapter)) {
			DEBUG("found default adapter %s", adapter);
			ns->adapter = ns->default_adapter;
			break;
		}
	}
	if (adapters->len && i == adapters->len) {
		/* fallback to 1st available adapter */
		ns->adapter = g_strdup(g_array_index(adapters, gchar*, 0));
		INFO("default adapter %s not found, fell back to: %s",
		     ns->default_adapter, ns->adapter);
	}
	n = adapters->len;

	// Clean up
	for(i = 0; i < adapters->len; i++) {
		g_free(g_array_index(adapters, gchar*, i));
	}
	g_array_unref(adapters);

	return n;
}

static struct bluez_state *bluez_dbus_init(GMainLoop *loop, gboolean autoconnect)
{
	struct bluez_state *ns;
	GError *error = NULL;

	ns = g_try_malloc0(sizeof(*ns));
	if (!ns) {
		ERROR("out of memory allocating bluez state");
		goto err_no_ns;
	}

	INFO("connecting to dbus");

	ns->loop = loop;
	ns->conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
	if (!ns->conn) {
		if (error)
			g_dbus_error_strip_remote_error(error);
		ERROR("Cannot connect to D-Bus, %s",
				error ? error->message : "unspecified");
		g_error_free(error);
		goto err_no_conn;

	}

	INFO("connected to dbus");

	ns->device_sub = g_dbus_connection_signal_subscribe(
			ns->conn,
			BLUEZ_SERVICE,
			NULL,   /* interface */
			NULL,	/* member */
			NULL,	/* object path */
			NULL,	/* arg0 */
			G_DBUS_SIGNAL_FLAGS_NONE,
			bluez_devices_signal_callback,
			ns,
			NULL);
	if (!ns->device_sub) {
		ERROR("Unable to subscribe to interface signals");
		goto err_no_device_sub;
	}

	g_mutex_init(&ns->cw_mutex);
	ns->next_cw_id = 1;

	if (autoconnect)
		g_timeout_add_seconds(5, bluez_autoconnect, ns);

	INFO("done");
	return ns;

err_no_device_sub:
	g_dbus_connection_close(ns->conn, NULL, NULL, NULL);
err_no_conn:
	g_free(ns);
err_no_ns:
	return NULL;
}

static void signal_init_done(struct init_data *id, gboolean rc)
{
	g_mutex_lock(&id->mutex);
	id->init_done = TRUE;
	id->rc = rc;
	g_cond_signal(&id->cond);
	g_mutex_unlock(&id->mutex);
}

static void bluez_cleanup(struct bluez_state *ns)
{
	g_dbus_connection_signal_unsubscribe(ns->conn, ns->device_sub);
	g_dbus_connection_close(ns->conn, NULL, NULL, NULL);
	g_free(ns);
}

typedef struct notifier_data {
	struct bluez_state *ns;
	bluez_init_cb_t cb;
	gpointer cb_data;
} notifier_data_t;

// Helper function to trigger client init callback once the glib main loop
// is running.
static gboolean main_loop_start_notifier(gpointer user_data)
{
	if (!user_data) {
		ERROR("Bad start notifier data!");

		// Probably not much point returning TRUE to try again
		return FALSE;
	}

	notifier_data_t *data = (notifier_data_t*) user_data;
	DEBUG("running init callback");
	if (data->cb && data->ns) {
		bluez_init_cb_t cb = data->cb;
		(*cb)(data->ns->adapter, TRUE, data->cb_data);
		g_free(data);
	}

	// Let loop know we can be removed
	return FALSE;
}

static gpointer bluez_handler_func(gpointer ptr)
{
	struct init_data *id = ptr;
	struct bluez_state *ns;
	GMainLoop *loop;
	int num_adapters = 0;
	unsigned int delay;
	unsigned int attempt;
	notifier_data_t *notifier_data;

	g_atomic_rc_box_acquire(id);

	// Save callback info for later use
	notifier_data = g_malloc0(sizeof(*notifier_data));
	if (!notifier_data) {
		ERROR("Unable to alloc notifier data");
		goto err_no_loop;
	}
	notifier_data->cb = id->cb;
	notifier_data->cb_data = id->user_data;

	loop = g_main_loop_new(NULL, FALSE);
	if (!loop) {
		ERROR("Unable to create main loop");
		goto err_no_loop;
	}

	// Do BlueZ D-Bus related init
	ns = bluez_dbus_init(loop, id->autoconnect);
	if (!ns) {
		ERROR("bluez_init() failed");
		goto err_no_ns;
	}

	id->ns = ns;
	bluez_set_state(ns);

	ns->default_adapter = get_default_adapter();
	if (!ns->default_adapter) {
		ns->default_adapter = g_strdup(BLUEZ_DEFAULT_ADAPTER);
		gboolean rc = set_default_adapter(BLUEZ_DEFAULT_ADAPTER);
		if (!rc)
			WARNING("Request to save default adapter to persistent storage failed ");
	}

	if (id->register_agent) {
		gboolean rc = bluez_register_agent(id);
		if (!rc) {
			ERROR("bluetooth_register_agent() failed");
			goto err_no_agent;
		}
	}

	// Let main process know initialization is done
	signal_init_done(id, TRUE);

	// Cannot reference id after this point
	g_atomic_rc_box_release(id);

	// Wait for an adapter to appear
	num_adapters = 0;
	delay = 1;
	attempt = 1;
	while(num_adapters <= 0) {
		num_adapters = bluez_select_init_adapter();
		if (num_adapters > 0)
			break;

		// Back off querying rate after the first 60 seconds
		if (attempt++ == 60)
			delay = 10;

		sleep(delay);
	}

	if (num_adapters > 0) {
		notifier_data->ns = ns;
		g_timeout_add_full(G_PRIORITY_DEFAULT,
				   0,
				   main_loop_start_notifier,
				   notifier_data,
				   NULL);

		DEBUG("calling g_main_loop_run");
		g_main_loop_run(loop);
	} else {
		ERROR("bluez_select_init_adapter() failed");
	}

	g_main_loop_unref(ns->loop);

	if (ns->agent_path)
		bluez_unregister_agent(ns);

	bluez_cleanup(ns);
	bluez_set_state(NULL);

	return NULL;

err_no_agent:
	bluez_cleanup(ns);

err_no_ns:
	g_main_loop_unref(loop);

err_no_loop:
	if (notifier_data && notifier_data->cb) {
		(*notifier_data->cb)(NULL, FALSE, notifier_data->cb_data);
		g_free(notifier_data);
	}

	signal_init_done(id, FALSE);
	g_atomic_rc_box_release(id);

	return NULL;
}

EXPORT gboolean bluez_init(gboolean register_agent,
			   gboolean autoconnect,
			   bluez_init_cb_t cb,
			   gpointer user_data)
{
	struct init_data *id = NULL;
	gint64 end_time;
	gboolean rc;
	gboolean init_done;

	id = g_atomic_rc_box_new0(struct init_data);
	if (!id)
		return -ENOMEM;
	g_atomic_rc_box_acquire(id);

	id->register_agent = register_agent;
	id->autoconnect = autoconnect;
	//id->init_done = FALSE;
	id->init_done_cb = signal_init_done;
	//id->rc = TRUE;
	id->cb = cb;
	id->user_data = user_data;
	g_cond_init(&id->cond);
	g_mutex_init(&id->mutex);

	g_bluez_thread = g_thread_new("bluez_handler",
				      bluez_handler_func,
				      id);

	INFO("waiting for init done");

	/* wait maximum 10 seconds for init done */
	end_time = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
	g_mutex_lock(&id->mutex);
	while (!id->init_done) {
		if (!g_cond_wait_until(&id->cond, &id->mutex, end_time))
			break;
	}
	rc = id->rc;
	init_done = id->init_done;
	g_mutex_unlock(&id->mutex);
	g_atomic_rc_box_release(id);

	if (!init_done) {
		ERROR("init timeout");
		return FALSE;
	}

	if (!rc)
		ERROR("init thread failed");
	else
		INFO("running");

	return rc;
}

EXPORT char *bluez_get_default_adapter(void)
{
	struct bluez_state *ns = bluez_get_state();
	if (!(ns && ns->adapter)) {
		return NULL;
	}
	return ns->adapter;
}

EXPORT gboolean bluez_set_default_adapter(const char *adapter,
					  char **adapter_new)
{
	gboolean rc = TRUE;
	char *adapter_default = get_default_adapter();

	if (adapter) {
		if (adapter_default && g_strcmp0(adapter_default, adapter)) {
			rc = set_default_adapter(adapter);
			if (!rc) {
				WARNING("Request to save default adapter to persistent storage failed");
				return FALSE;
			}
		}
		if(rc && adapter_new)
			*adapter_new = g_strdup(adapter);
	} else if (adapter_default) {
		*adapter_new = g_strdup(adapter_default);
	} else {
		ERROR("No default adapter");
		rc = FALSE;
	}
	return rc;
}

EXPORT gboolean bluez_get_managed_objects(GVariant **reply)
{
	struct bluez_state *ns = bluez_get_state();
	GError *error = NULL;

	if (!ns) {
		ERROR("Invalid state");
		return FALSE;
	}
	if (!reply)
		return FALSE;

	*reply = bluez_get_properties(ns,
				      BLUEZ_AT_OBJECT,
				      BLUEZ_OBJECT_PATH,
				      &error);
	if (error) {
		ERROR("bluez_get_properties error: %s", error->message);
		g_clear_error(&error);
		*reply = NULL;
	}

	return TRUE;
}

EXPORT gboolean bluez_get_adapters(GArray **reply)
{
	struct bluez_state *ns = bluez_get_state();
	GVariant *objects;
	GError *error = NULL;

	if (!ns) {
		ERROR("No adapter");
		return FALSE;
	}
	if (!reply)
		return FALSE;		

	objects = bluez_get_properties(ns,
				       BLUEZ_AT_OBJECT,
				       BLUEZ_OBJECT_PATH,
				       &error);
	if (error) {
		ERROR("get properties error: %s", error->message);
		g_error_free(error);
		*reply = NULL;
		return FALSE;
	}

	// Iterate and pull out adapters
	GVariantIter *array, *array2;
	GVariant *var = NULL;
	const char *interface, *path2 = NULL;
	GArray *adapters = g_array_new(FALSE, FALSE, sizeof(gchar*));

	g_variant_get(objects, "(a{oa{sa{sv}}})", &array);
	while (g_variant_iter_loop(array, "{oa{sa{sv}}}", &path2, &array2)) {
		while (g_variant_iter_loop(array2, "{&s@a{sv}}", &interface, &var)) {
			if (!strcmp(interface, BLUEZ_ADAPTER_INTERFACE)) {
				gchar *adapter = bluez_return_adapter(path2);
				g_array_append_val(adapters, adapter);
				break;
			}
		}
	}
	if (array2)
		g_variant_iter_free(array2);
	if (array)
		g_variant_iter_free(array);

	*reply = adapters;

	g_variant_unref(objects);
	return TRUE;
}

EXPORT gboolean bluez_adapter_get_state(const char *adapter, GVariant **reply)
{
	struct bluez_state *ns = bluez_get_state();
	GError *error = NULL;
	const char *adapter_path;
	GVariant *properties = NULL;

	if (!ns || (!adapter && !ns->adapter)) {
		ERROR("No adapter");
		return FALSE;
	}
	if (!reply)
		return FALSE;

	adapter_path = BLUEZ_ROOT_PATH(adapter ? adapter : ns->adapter);
	properties = bluez_get_properties(ns,
					  BLUEZ_AT_ADAPTER,
					  adapter_path,
					  &error);
	if (error) {
		ERROR("bluez_get_properties error: %s", error->message);
		g_error_free(error);
		*reply = NULL;
		return FALSE;
	}

	// Pull properties out of tuple so caller does not have to
	g_variant_get(properties, "(@a{sv})", reply);
	g_variant_unref(properties);

	return TRUE;
}

EXPORT gboolean bluez_adapter_get_devices(const char *adapter, GVariant **reply)
{
	struct bluez_state *ns = bluez_get_state();
	GVariant *objects;
	GError *error = NULL;
	const char *target_adapter;

	if (!ns || (!adapter && !ns->adapter)) {
		ERROR("No adapter");
		return FALSE;
	}
	if (!reply)
		return FALSE;		

	target_adapter = adapter ? adapter : ns->adapter;

	objects = bluez_get_properties(ns,
				       BLUEZ_AT_OBJECT,
				       BLUEZ_OBJECT_PATH,
				       &error);
	if (error) {
		ERROR("bluez_get_properties error: %s", error->message);
		g_error_free(error);
		*reply = NULL;
		return FALSE;
	}

	// Iterate and pull out devices
	GVariantIter *array, *array2;
	GVariant *var = NULL;
	const char *interface, *path2 = NULL;
	GVariantDict *devices_dict = g_variant_dict_new(NULL);
	if (!devices_dict) {
		ERROR("g_variant_dict_new failed");
		g_variant_unref(objects);
		*reply = NULL;
		return FALSE;
	}

	g_variant_get(objects, "(a{oa{sa{sv}}})", &array);
	while (g_variant_iter_loop(array, "{oa{sa{sv}}}", &path2, &array2)) {
		gchar *device_adapter = bluez_return_adapter(path2);
		if (!device_adapter || strcmp(device_adapter, target_adapter) != 0) {
			g_free(device_adapter);
			continue;
		}
		while (g_variant_iter_loop(array2, "{&s@a{sv}}", &interface, &var)) {
			if (!strcmp(interface, BLUEZ_DEVICE_INTERFACE)) {
				gchar *device = bluez_return_device(path2);
				g_variant_dict_insert_value(devices_dict,
							    device,
							    var);
			}
		}
	}
	if (array2)
		g_variant_iter_free(array2);
	if (array)
		g_variant_iter_free(array);

	*reply = g_variant_dict_end(devices_dict);
	g_variant_dict_unref(devices_dict);
	g_variant_unref(objects);

	return TRUE;
}

EXPORT gboolean bluez_adapter_set_discovery(const char *adapter,
					    gboolean scan)
{
	struct bluez_state *ns = bluez_get_state();
	GError *error = NULL;

	if (!ns || (!adapter && !ns->adapter)) {
		ERROR("No adapter");
		return FALSE;
	}

	adapter = BLUEZ_ROOT_PATH(adapter ? adapter : ns->adapter);

	GVariant *reply = bluez_call(ns, BLUEZ_AT_ADAPTER, adapter,
				     scan ? "StartDiscovery" : "StopDiscovery",
				     NULL, &error);
	if (!reply) {
		ERROR("adapter %s method %s error: %s",
		      adapter, "Scan", BLUEZ_ERRMSG(error));
		g_error_free(error);
		return FALSE;
	}
	g_variant_unref(reply);

	return TRUE;
}

EXPORT gboolean bluez_adapter_set_discovery_filter(const char *adapter,
						   gchar **uuids,
						   gchar *transport)
{
	struct bluez_state *ns = bluez_get_state();
	GError *error = NULL;
	GVariantBuilder builder;
	GVariant *filter, *reply;

	if (!ns || (!adapter && !ns->adapter)) {
		ERROR("No adapter");
		return FALSE;
	}

	if (!(uuids || transport)) {
		return FALSE;
	}

	adapter = BLUEZ_ROOT_PATH(adapter ? adapter : ns->adapter);

	g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

	if (uuids && *uuids != NULL) {
		g_variant_builder_add(&builder, "{sv}", "UUIDs",
				      g_variant_new_strv((const gchar * const *) uuids, -1));
	}

	if (transport) {
		g_variant_builder_add(&builder, "{sv}", "Transport",
				      g_variant_new_string(transport));
	}

	filter = g_variant_builder_end(&builder);

	reply = bluez_call(ns, BLUEZ_AT_ADAPTER, adapter,
			   "SetDiscoveryFilter",
			   g_variant_new("(@a{sv})", filter), &error);
	if (!reply) {
		ERROR("adapter %s method %s error: %s",
		      adapter, "SetDiscoveryFilter", BLUEZ_ERRMSG(error));
		g_error_free(error);
		return FALSE;
	}

	g_variant_unref(reply);

	return TRUE;
}

EXPORT gboolean bluez_adapter_set_discoverable(const char *adapter,
					       gboolean discoverable)
{
	struct bluez_state *ns = bluez_get_state();
	GError *error = NULL;
	gboolean rc;

	if (!ns || (!adapter && !ns->adapter)) {
		ERROR("No adapter");
		return FALSE;
	}

	adapter = BLUEZ_ROOT_PATH(adapter ? adapter : ns->adapter);

	DEBUG("discoverable = %d", (int) discoverable);
	rc = bluez_set_boolean_property(ns, BLUEZ_AT_ADAPTER, adapter,
					"Discoverable", discoverable, &error);
	if (!rc) {
		ERROR("adapter %s set_property %s error: %s",
		      adapter, "Discoverable", BLUEZ_ERRMSG(error));
		g_error_free(error);
		return FALSE;
	}
	return TRUE;
}

EXPORT gboolean bluez_adapter_set_powered(const char *adapter,
					  gboolean powered)
{
	struct bluez_state *ns = bluez_get_state();
	GError *error = NULL;
	gboolean rc;

	if (!ns || (!adapter && !ns->adapter)) {
		ERROR("No adapter");
		return FALSE;
	}

	adapter = BLUEZ_ROOT_PATH(adapter ? adapter : ns->adapter);

	rc = bluez_set_boolean_property(ns, BLUEZ_AT_ADAPTER, adapter,
					"Powered", powered, &error);
	if (!rc) {
		ERROR("adapter %s set_property %s error: %s",
		      adapter, "Powered", BLUEZ_ERRMSG(error));
		g_error_free(error);
		return FALSE;
	}
	return TRUE;
}

static gchar *get_bluez_path(const char *adapter, const char *device)
{
	struct bluez_state *ns = bluez_get_state();
	const char *tmp;

	if (!ns || (!adapter && !ns->adapter))
		return NULL;

	if (!device)
		return NULL;

	tmp = device;

	/* Stop the dbus call from segfaulting from special characters */
	for (; *tmp; tmp++) {
		if (!g_ascii_isalnum(*tmp) && *tmp != '_') {
			ERROR("Invalid device parameter");
			return NULL;
		}
	}

	call_work_lock(ns);
	adapter = adapter ? adapter : ns->adapter;
	call_work_unlock(ns);

	return g_strconcat("/org/bluez/", adapter, "/", device, NULL);
}

static void connect_service_callback(void *user_data,
				     GVariant *result,
				     GError **error)
{
	struct call_work *cw = user_data;
	struct bluez_state *ns = cw->ns;
	gboolean status = TRUE;

	bluez_decode_call_error(ns,
				cw->access_type, cw->type_arg, cw->bluez_method,
				error);
	if (error && *error) {
		ERROR("Connect error: %s", (*error)->message);
		status = FALSE;
	}

	if (result)
		g_variant_unref(result);

	// Run callback
	if (cw->request_cb) {
		bluez_device_connect_cb_t cb = (bluez_device_connect_cb_t) cw->request_cb;
		gchar *device = g_strdup(cw->type_arg);
		(*cb)(device, status, cw->request_user_data);
	}

	call_work_destroy(cw);
}

EXPORT gboolean bluez_device_connect(const char *device,
				     const char *uuid,
				     bluez_device_connect_cb_t cb,
				     gpointer user_data)
{
	gboolean rc = TRUE;
	struct bluez_state *ns = bluez_get_state();
	GError *error = NULL;
	struct call_work *cw;
	gchar *device_path;

	device_path = get_bluez_path(NULL, device);
	if (!device) {
		ERROR("No path given");
		return FALSE;
	}

	cw = call_work_create(ns, BLUEZ_AT_DEVICE, device_path,
			      "connect_service", "Connect", &error);
	if (!cw) {
		ERROR("can't queue work %s", error->message);
		g_error_free(error);
		rc = FALSE;
		goto out_free;
	}

	// Set callback hook
	cw->request_cb = cb;
	cw->request_user_data = user_data;

	if (uuid) {
		/* connect single profile */
		cw->cpw = bluez_call_async(ns, BLUEZ_AT_DEVICE, device_path,
					   "ConnectProfile", g_variant_new("(&s)", uuid),
					   &error,
					   connect_service_callback, cw);
	} else {
		cw->cpw = bluez_call_async(ns, BLUEZ_AT_DEVICE, device_path,
					   "Connect", NULL,
					   &error,
					   connect_service_callback, cw);
	}
	if (!cw->cpw) {
		ERROR("Connection error: %s", error->message);
		call_work_destroy(cw);
		g_error_free(error);
		rc = FALSE;
		/* fall-thru */
	}

out_free:
	g_free(device_path);

	return rc;
}

EXPORT gboolean bluez_device_disconnect(const char *device,
					const char *uuid)
{
	gboolean rc = TRUE;
	struct bluez_state *ns = bluez_get_state();
	GVariant *reply = NULL;
	GError *error = NULL;
	gchar *device_path;

	device_path = get_bluez_path(NULL, device);
	if (!device_path) {
		ERROR("No device given to disconnect");
		return FALSE;
	}
	DEBUG("device = %s, device_path = %s", device, device_path);

	if (uuid) {
		/* Disconnect single profile */
		reply = bluez_call(ns, BLUEZ_AT_DEVICE, device_path,
				   "DisconnectProfile", g_variant_new("(&s)", uuid),
				   &error);
	} else {
		reply = bluez_call(ns, BLUEZ_AT_DEVICE, device_path,
				   "Disconnect", NULL,
				   &error);
	}

	if (!reply) {
		ERROR("Disconnect error: %s", BLUEZ_ERRMSG(error));
		g_error_free(error);
		rc = FALSE;
		goto out_free;
	}

	g_variant_unref(reply);

out_free:
	g_free(device_path);

	return rc;
}

static void pair_service_callback(void *user_data,
				  GVariant *result,
				  GError **error)
{
	struct call_work *cw = user_data;
	struct bluez_state *ns = cw->ns;
	gboolean status = TRUE;

	bluez_decode_call_error(ns,
				cw->access_type, cw->type_arg, cw->bluez_method,
				error);
	if (error && *error) {
		ERROR("Connect error: %s", (*error)->message);
		status = FALSE;
	}

	if (result)
		g_variant_unref(result);

	// Run callback
	if (cw->request_cb) {
		bluez_device_pair_cb_t cb = (bluez_device_pair_cb_t) cw->request_cb;
		gchar *device = g_strdup(cw->type_arg);
		(*cb)(device, status, user_data);
	}

	call_work_destroy(cw);
}

EXPORT gboolean bluez_device_pair(const char *device,
				  bluez_device_pair_cb_t cb,
				  gpointer user_data)
{
	gboolean rc = TRUE;
	struct bluez_state *ns = bluez_get_state();
	GError *error = NULL;
	gchar *device_path;
	struct call_work *cw;

	device_path = get_bluez_path(NULL, device);
	if (!device_path) {
		ERROR("No path given");
		return FALSE;
	}

	cw = call_work_create(ns, BLUEZ_AT_DEVICE, device_path,
			      "pair_device", "Pair",
			      &error);
	if (!cw) {
		ERROR("can't queue work %s", error->message);
		g_error_free(error);
		rc = FALSE;
		goto out_free;
	}

	// Set callback hook
	cw->request_cb = cb;
	cw->request_user_data = user_data;

	cw->agent_data.fixed_pincode = get_pincode();

	cw->cpw = bluez_call_async(ns, BLUEZ_AT_DEVICE, device_path,
				   "Pair", NULL,
				   &error,
				   pair_service_callback, cw);
	if (!cw->cpw) {
		ERROR("Pairing error: %s", error->message);
		call_work_destroy(cw);
		g_error_free(error);
		rc = FALSE;
		goto out_free;
	}

out_free:
	g_free(device_path);

	return rc;
}

EXPORT gboolean bluez_cancel_pairing(void)
{
	struct bluez_state *ns = bluez_get_state();
	struct call_work *cw;
	GVariant *reply = NULL;
	GError *error = NULL;
	gchar *device_path;

	call_work_lock(ns);

	cw = call_work_lookup_unlocked(ns, "device", NULL, "RequestConfirmation");
	if (!cw) {
		call_work_unlock(ns);
		ERROR("No pairing in progress");
		return FALSE;
	}

	device_path = cw->agent_data.device_path;
	reply = bluez_call(ns, BLUEZ_AT_DEVICE, device_path,
			   "CancelPairing", NULL,
			   &error);
	if (!reply) {
		call_work_unlock(ns);
		ERROR("device %s method %s error: %s",
		      device_path, "CancelPairing", error->message);
		g_error_free(error);
		return FALSE;
	}

	call_work_unlock(ns);

	return TRUE;
}

EXPORT gboolean bluez_confirm_pairing(const char *pincode_str)
{
	gboolean rc = TRUE;
	struct bluez_state *ns = bluez_get_state();
	struct call_work *cw;
	int pin = -1;

	if (pincode_str)
		pin = (int) strtol(pincode_str, NULL, 10);

	// GSM - FIXME - this seems broken wrt a pin of all zeroes?
	if (!pincode_str || !pin) {
		ERROR("No pincode parameter");
		return FALSE;
	}

	call_work_lock(ns);
	cw = call_work_lookup_unlocked(ns, "device", NULL, "RequestConfirmation");
	if (!cw) {
		call_work_unlock(ns);
		ERROR("No pairing in progress");
		return FALSE;
	}

	if (pin == cw->agent_data.pin_code) {
		g_dbus_method_invocation_return_value(cw->invocation, NULL);
		INFO("pairing confirmed");
	} else {
		g_dbus_method_invocation_return_dbus_error(cw->invocation,
							   "org.bluez.Error.Rejected",
							   "No connection pending");
		ERROR("pairing failed");
		rc = FALSE;
	}

	call_work_destroy_unlocked(cw);
	call_work_unlock(ns);
	return rc;
}

EXPORT gboolean bluez_device_remove(const char *device)
{
	gboolean rc = TRUE;
	struct bluez_state *ns = bluez_get_state();
	GVariant *reply;
	GError *error = NULL;
	const char *adapter = NULL;

	gchar *device_path = get_bluez_path(NULL, device);
	if (!device_path) {
		ERROR("No path given");
		return FALSE;
	}

	GVariant *val = bluez_get_property(ns, BLUEZ_AT_DEVICE, device_path, "Adapter", &error);
	if (error) {
		// This can happen if a device is removed while pairing, so unless
		// an additional check for device presence is done first, it should
		// not be logged as a definite error.
		WARNING("adapter not found for device %s error: %s",
			device, error->message);
		g_error_free(error);
		rc = FALSE;
		goto out_free;
	}

	adapter = g_variant_get_string(val, NULL);
	if (!adapter) {
		ERROR("adapter invalid for device %s", device);
		rc = FALSE;
		goto out_free_val;
	}

	reply = bluez_call(ns, BLUEZ_AT_ADAPTER, adapter,
			   "RemoveDevice", g_variant_new("(o)", device_path),
			   &error);
	if (error) {
		ERROR("device %s method %s error: %s",
		      device_path, "RemoveDevice", error->message);
		g_error_free(error);
		rc = FALSE;
		goto out_free_val;
	}
	g_variant_unref(reply);

	INFO("device %s removed", device_path);

out_free_val:
	g_variant_unref(val);
out_free:
	g_free(device_path);

	return rc;
}

static void mediaplayer1_connect_disconnect(struct bluez_state *ns,
				            const gchar *player,
					    int state)
{
	GVariant *reply;
	gchar *path = g_strdup(player);
	const char *uuids[] = {
		"0000110a-0000-1000-8000-00805f9b34fb",
		"0000110e-0000-1000-8000-00805f9b34fb",
		NULL
	};
	const char **tmp = (const char **) uuids;

	*g_strrstr(path, "/") = '\0';

	for (; *tmp; tmp++) {
		reply = bluez_call(ns, BLUEZ_AT_DEVICE, path,
				   state ? "ConnectProfile" : "DisconnectProfile",
				   g_variant_new("(&s)", *tmp), NULL);
		if (!reply)
			break;
		g_variant_unref(reply);
	}

	g_free(path);
}

EXPORT gboolean bluez_device_avrcp_controls(const char *device,
					    bluez_media_control_t action)
{
	struct bluez_state *ns = bluez_get_state();
	const char *action_names[BLUEZ_MEDIA_CONTROL_REWIND + 1] = {
		"Connect", "Disconnect", "Play", "Pause", "Stop", 
		"Next", "Previous", "FastForward", "Rewind"
	};
	const char *action_str = action_names[action];

	gchar *player = NULL;
	gchar *device_path = get_bluez_path(NULL, device);
	if (device_path) {
		// TODO: handle multiple players per device
		GVariant *val = bluez_get_property(ns, BLUEZ_AT_MEDIACONTROL, device_path, "Player", NULL);
		if (val) {
			player = g_variant_get_string(val, NULL);
			g_variant_unref(val);
		}
		if (!player)
			player = g_strconcat(device_path, "/", BLUEZ_DEFAULT_PLAYER, NULL);

		g_free(device_path);
	} else {
		player = g_strdup(ns->mediaplayer_path);
	}

	if (!player) {
		ERROR("No path given");
		return FALSE;
	}

	if (action == BLUEZ_MEDIA_CONTROL_CONNECT || action == BLUEZ_MEDIA_CONTROL_DISCONNECT) {
		mediaplayer1_connect_disconnect(ns,
						player,
						action == BLUEZ_MEDIA_CONTROL_CONNECT);
		goto out_success;
	}

	GError *error = NULL;
	GVariant *reply = bluez_call(ns, BLUEZ_AT_MEDIAPLAYER, player, action_str, NULL, &error);
	if (!reply) {
		ERROR("mediaplayer %s method %s error: %s",
		      player, action_str, BLUEZ_ERRMSG(error));
		g_free(player);
		g_error_free(error);
		return FALSE;
	}
	g_variant_unref(reply);

out_success:
	g_free(player);

	return TRUE;
}

EXPORT gboolean bluez_set_pincode(const char *pincode)
{
	gboolean rc = TRUE;
	char *error = NULL;

	rc = set_pincode(pincode, &error);
	if (rc) {
		ERROR("%s", error);
	}

	return rc;
}
