/*
 * Copyright 2018,2020-2021 Konsulko Group
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

#include "bluez-call.h"
#include "common.h"

G_DEFINE_QUARK(bluez-error-quark, bluez_error)


void bluez_decode_call_error(struct bluez_state *ns,
			     const char *access_type,
			     const char *type_arg,
			     const char *method,
			     GError **error)
{
	if (!error || !*error)
		return;

	if (strstr((*error)->message,
		   "org.freedesktop.DBus.Error.UnknownObject")) {

		if (!strcmp(method, "Set") ||
		    !strcmp(method, "Get") ||
		    !strcmp(method, "GetAll")) {

			g_clear_error(error);
			g_set_error(error, BLUEZ_ERROR,
				    BLUEZ_ERROR_UNKNOWN_PROPERTY,
				    "unknown %s property on %s",
				    access_type, type_arg);

		} else if (!strcmp(method, "Connect") ||
			   !strcmp(method, "ConnectProfile") ||
			   !strcmp(method, "Disconnect") ||
			   !strcmp(method, "DisconnectProfile") ||
			   !strcmp(method, "Pair") ||
			   !strcmp(method, "Unpair") ||
			   !strcmp(method, "RemoveDevice")) {

			g_clear_error(error);
			g_set_error(error, BLUEZ_ERROR,
				    BLUEZ_ERROR_UNKNOWN_SERVICE,
				    "unknown service %s",
				    type_arg);

		} else if (!strcmp(method, "StartDiscovery") ||
			   !strcmp(method, "StopDiscovery") ||
			   !strcmp(method, "SetDiscoveryFilter") ||
			   !strcmp(method, "RegisterAgent")) {

			g_clear_error(error);
			g_set_error(error, BLUEZ_ERROR,
				    BLUEZ_ERROR_UNKNOWN_SERVICE,
				    "unknown service %s",
				    type_arg);
		} else if (!strcmp(method, "Play") ||
			   !strcmp(method, "Pause") ||
			   !strcmp(method, "Stop") ||
			   !strcmp(method, "Next") ||
			   !strcmp(method, "Previous") ||
			   !strcmp(method, "FastForward") ||
			   !strcmp(method, "Rewind")) {

			g_clear_error(error);
			g_set_error(error, BLUEZ_ERROR,
				    BLUEZ_ERROR_UNKNOWN_PROPERTY,
				    "unknown method %s",
				    method);
		}
	}
}

GVariant *bluez_call(struct bluez_state *ns,
		     const char *access_type,
		     const char *path,
		     const char *method,
		     GVariant *params,
		     GError **error)
{
	const char *interface;
	GVariant *reply;

	if (!path && (!strcmp(access_type, BLUEZ_AT_DEVICE) ||
			  !strcmp(access_type, BLUEZ_AT_ADAPTER) ||
			  !strcmp(access_type, BLUEZ_AT_MEDIAPLAYER))) {
		g_set_error(error, BLUEZ_ERROR, BLUEZ_ERROR_MISSING_ARGUMENT,
			    "missing %s argument",
			    access_type);
		return NULL;
	}

	if (!strcmp(access_type, BLUEZ_AT_DEVICE)) {
		interface = BLUEZ_DEVICE_INTERFACE;
	} else if (!strcmp(access_type, BLUEZ_AT_ADAPTER)) {
		interface = BLUEZ_ADAPTER_INTERFACE;
	} else if (!strcmp(access_type, BLUEZ_AT_AGENTMANAGER)) {
		path = BLUEZ_PATH;
		interface = BLUEZ_AGENTMANAGER_INTERFACE;
	} else if (!strcmp(access_type, BLUEZ_AT_MEDIAPLAYER)) {
		interface = BLUEZ_MEDIAPLAYER_INTERFACE;
	} else {
		g_set_error(error, BLUEZ_ERROR, BLUEZ_ERROR_ILLEGAL_ARGUMENT,
			    "illegal %s argument",
			    access_type);
		return NULL;
	}

	reply = g_dbus_connection_call_sync(ns->conn,
					    BLUEZ_SERVICE, path, interface, method, params,
					    NULL, G_DBUS_CALL_FLAGS_NONE, DBUS_REPLY_TIMEOUT,
					    NULL, error);
	bluez_decode_call_error(ns, access_type, path, method,error);
	if (!reply && error) {
		if (*error)
			g_dbus_error_strip_remote_error(*error);
		ERROR("Error calling %s%s%s %s method %s",
		      access_type,
		      path ? "/" : "",
		      path ? path : "",
		      method,
		      error && *error ? (*error)->message :
		      "unspecified");
	}

	return reply;
}

static void bluez_call_async_ready(GObject *source_object,
				   GAsyncResult *res,
				   gpointer user_data)
{
	struct bluez_pending_work *cpw = user_data;
	struct bluez_state *ns = cpw->ns;
	GVariant *result;
	GError *error = NULL;

	result = g_dbus_connection_call_finish(ns->conn, res, &error);

	cpw->callback(cpw->user_data, result, &error);

	g_clear_error(&error);
	g_cancellable_reset(cpw->cancel);
	g_free(cpw);
}

void bluez_cancel_call(struct bluez_state *ns,
		       struct bluez_pending_work *cpw)
{
	g_cancellable_cancel(cpw->cancel);
}

struct bluez_pending_work *
bluez_call_async(struct bluez_state *ns,
		 const char *access_type,
		 const char *type_arg,
		 const char *method,
		 GVariant *params,
		 GError **error,
		 void (*callback)(void *user_data, GVariant *result, GError **error),
		 void *user_data)
{
	const char *path;
	const char *interface;
	struct bluez_pending_work *cpw;

	if (!type_arg && (!strcmp(access_type, BLUEZ_AT_DEVICE) ||
			  !strcmp(access_type, BLUEZ_AT_ADAPTER))) {
		g_set_error(error, BLUEZ_ERROR, BLUEZ_ERROR_MISSING_ARGUMENT,
			    "missing %s argument",
			    access_type);
		return NULL;
	}

	if (!strcmp(access_type, BLUEZ_AT_DEVICE)) {
		path = type_arg;
		interface = BLUEZ_DEVICE_INTERFACE;
	} else if (!strcmp(access_type, BLUEZ_AT_ADAPTER)) {
		path = type_arg;
		interface = BLUEZ_ADAPTER_INTERFACE;
	} else {
		g_set_error(error, BLUEZ_ERROR, BLUEZ_ERROR_ILLEGAL_ARGUMENT,
			    "illegal %s argument",
			    access_type);
		return NULL;
	}

	cpw = g_malloc(sizeof(*cpw));
	if (!cpw) {
		g_set_error(error, BLUEZ_ERROR, BLUEZ_ERROR_OUT_OF_MEMORY,
			    "out of memory");
		return NULL;
	}
	cpw->ns = ns;
	cpw->user_data = user_data;
	cpw->cancel = g_cancellable_new();
	if (!cpw->cancel) {
		g_free(cpw);
		g_set_error(error, BLUEZ_ERROR, BLUEZ_ERROR_OUT_OF_MEMORY,
			    "out of memory");
		return NULL;
	}
	cpw->callback = callback;

	g_dbus_connection_call(ns->conn,
			       BLUEZ_SERVICE, path, interface, method, params,
			       NULL,	/* reply type */
			       G_DBUS_CALL_FLAGS_NONE, DBUS_REPLY_TIMEOUT,
			       cpw->cancel,	/* cancellable? */
			       bluez_call_async_ready,
			       cpw);

	return cpw;
}

GVariant *bluez_get_properties(struct bluez_state *ns,
			       const char *access_type,
			       const char *path,
			       GError **error)
{
	const char *method = NULL;
	GVariant *reply = NULL;
	const char *interface, *interface2 = NULL;

	if (!strcmp(access_type, BLUEZ_AT_DEVICE) ||
	    !strcmp(access_type, BLUEZ_AT_MEDIAPLAYER) ||
	    !strcmp(access_type, BLUEZ_AT_MEDIATRANSPORT) ||
	    !strcmp(access_type, BLUEZ_AT_ADAPTER) ||
	    !strcmp(access_type, BLUEZ_AT_MEDIACONTROL)) {
		interface = FREEDESKTOP_PROPERTIES;
		method = "GetAll";
	} else if (!strcmp(access_type, BLUEZ_AT_OBJECT)) {
		interface = FREEDESKTOP_OBJECTMANAGER;
		method = "GetManagedObjects";
	}

	if (!strcmp(access_type, BLUEZ_AT_DEVICE))
		interface2 = BLUEZ_DEVICE_INTERFACE;
	else if (!strcmp(access_type, BLUEZ_AT_MEDIAPLAYER))
		interface2 = BLUEZ_MEDIAPLAYER_INTERFACE;
	else if (!strcmp(access_type, BLUEZ_AT_MEDIATRANSPORT))
		interface2 = BLUEZ_MEDIATRANSPORT_INTERFACE;
	else if (!strcmp(access_type, BLUEZ_AT_ADAPTER))
		interface2 = BLUEZ_ADAPTER_INTERFACE;
	else if (!strcmp(access_type, BLUEZ_AT_MEDIACONTROL))
		interface2 = BLUEZ_MEDIACONTROL_INTERFACE;
	else if (!strcmp(access_type, BLUEZ_AT_OBJECT))
		interface2 = NULL;

	if (!method) {
		g_set_error(error, BLUEZ_ERROR, BLUEZ_ERROR_ILLEGAL_ARGUMENT,
			    "illegal %s argument",
			    access_type);
		return NULL;
	}

	reply = g_dbus_connection_call_sync(ns->conn,
					    BLUEZ_SERVICE, path, interface, method,
					    interface2 ? g_variant_new("(s)", interface2) : NULL,
					    NULL, G_DBUS_CALL_FLAGS_NONE, DBUS_REPLY_TIMEOUT,
					    NULL, error);
	if (!reply) {
		g_set_error(error, BLUEZ_ERROR, BLUEZ_ERROR_ILLEGAL_ARGUMENT,
			    "No %s", access_type);
		return NULL;
	}

	return reply;
}

GVariant *bluez_get_property(struct bluez_state *ns,
			     const char *access_type,
			     const char *path,
			     const char *name,
			     GError **error)
{
	// NOTE: Only supporting device properties ATM
	if (!(ns && access_type && path && name) ||
	    strcmp(access_type, BLUEZ_AT_DEVICE)) {
		g_set_error(error, BLUEZ_ERROR, BLUEZ_ERROR_ILLEGAL_ARGUMENT,
			    "Bad argument");
		return NULL;
	}

	GError *get_error = NULL;
	GVariant *reply = bluez_get_properties(ns, access_type, path, &get_error);
	if (get_error || !reply) {
		if (!get_error)
			g_set_error(error, BLUEZ_ERROR, BLUEZ_ERROR_BAD_PROPERTY,
				    "Unexpected error querying properties %s%s%s",
				    access_type,
				    path ? "/" : "",
				    path ? path : "");
		else if (error)
			*error = get_error;
		else
			g_error_free(get_error);
		return NULL;
	}

	GVariantIter *array = NULL;
	g_variant_get(reply, "(a{sv})", &array);
	if (!array) {
		g_variant_unref(reply);
		g_set_error(error, BLUEZ_ERROR, BLUEZ_ERROR_BAD_PROPERTY,
			    "Unexpected reply querying property '%s' on %s%s%s",
			    name,
			    access_type,
			    path ? "/" : "",
			    path ? path : "");
		return NULL;
	}

	// Look for property by name
	gchar *key = NULL;
	GVariant *val = NULL;
	while (g_variant_iter_loop(array, "{sv}", &key, &val)) {
		if (!g_strcmp0(name, key))
			break;
	}
	g_free(key);
	g_variant_unref(reply);

        if (!val)
		g_set_error(error, BLUEZ_ERROR, BLUEZ_ERROR_BAD_PROPERTY,
			    "Bad property '%s' on %s%s%s",
			    name,
			    access_type,
			    path ? "/" : "",
			    path ? path : "");
	return val;
}

gboolean bluez_set_boolean_property(struct bluez_state *ns,
				    const char *access_type,
				    const char *path,
				    const char *name,
				    gboolean value,
				    GError **error)
{
	GVariant *reply, *arg;
	const char *interface;
	gchar *propname;

	g_assert(path);

	/* convert to gvariant */
	arg = g_variant_new_boolean(value);

	/* no variant? error */
	if (!arg)
		return FALSE;

	if (!strcmp(access_type, BLUEZ_AT_DEVICE))
		interface = BLUEZ_DEVICE_INTERFACE;
	else if (!strcmp(access_type, BLUEZ_AT_ADAPTER))
		interface = BLUEZ_ADAPTER_INTERFACE;
	else if (!strcmp(access_type, BLUEZ_AT_AGENT))
		interface = BLUEZ_AGENT_INTERFACE;
	else
		return FALSE;

	propname = g_strdup(name);
	reply = g_dbus_connection_call_sync(ns->conn,
					    BLUEZ_SERVICE, path, FREEDESKTOP_PROPERTIES, "Set",
					    g_variant_new("(ssv)", interface, propname, arg),
					    NULL, G_DBUS_CALL_FLAGS_NONE, DBUS_REPLY_TIMEOUT,
					    NULL, error);
	g_free(propname);

	if (!reply)
		return FALSE;

	g_variant_unref(reply);

	return TRUE;
}

/*
 * NOTE:
 *  At present the autoconnect behavior is to look for a previously
 *  paired device and stop looking once a connect attempt has been
 *  made.  A difference from the binding behavior is that it would
 *  only look for a device once, now the device lookups continue
 *  until a connect attempt is made.
 *
 *  There is perhaps a case to be made that this functionality should
 *  be left to clients of the library.  Or alternatively, rework it
 *  to expose controls such as timeouts and preferred device
 *  specification.
 */
gboolean bluez_autoconnect(gpointer data)
{
	struct bluez_state *ns = data;
	gboolean rc = TRUE;

	GVariant *reply = NULL;
	if (!bluez_adapter_get_devices(NULL, &reply)) {
		// Reschedule until there's an adapter response
		return TRUE;
	}

	GVariantIter *array = NULL;
	g_variant_get(reply, "a{sv}", &array);
	const gchar *key = NULL;
	GVariant *var = NULL;
	while (g_variant_iter_next(array, "{&sv}", &key, &var)) {
		GVariantDict *props_dict = g_variant_dict_new(var);

		// Use the device's adapter rather than assuming it
		gchar *adapter = NULL;
		if (!(g_variant_dict_lookup(props_dict, "Adapter", "&o", &adapter) && adapter)) {
			ERROR("could not find device %s adapter", key);
			continue;
		}

		gboolean paired = FALSE;
		if (g_variant_dict_lookup(props_dict, "Paired", "b", &paired) && paired) {
			gchar *path = g_strconcat("/org/bluez/", adapter, "/", key, NULL);
			GVariant *connect_reply = bluez_call(ns, "device", path, "Connect", NULL, NULL);
			g_free(path);
			if (!connect_reply)
				continue;

			g_variant_unref(connect_reply);

			// We've initiated connection, stop
			rc = FALSE;
		}
		g_variant_dict_unref(props_dict);
		g_variant_unref(var);
		if (!rc)
			break;
	}
	g_variant_iter_free(array);
	g_variant_unref(reply);

	return rc;
}
