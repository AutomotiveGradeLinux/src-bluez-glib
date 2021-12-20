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

#include "common.h"
#include "call_work.h"
#include "bluez-glib.h"
#include "bluez-call.h"

static bluez_agent_event_cb_t agent_event_cb = NULL;
static gpointer agent_event_cb_data = NULL;
static GMutex agent_event_cb_mutex;

EXPORT void bluez_add_agent_event_callback(bluez_agent_event_cb_t cb, gpointer user_data)
{
	g_mutex_lock(&agent_event_cb_mutex);
	if (agent_event_cb == NULL) {
		agent_event_cb = cb;
		agent_event_cb_data = user_data;
	} else {
		ERROR("Agent event callback already set");
	}
	g_mutex_unlock(&agent_event_cb_mutex);
}

static void run_callback(gchar *device,
			 bluez_agent_event_t event,
			 GVariant *properties)
{
	g_mutex_lock(&agent_event_cb_mutex);
	if (agent_event_cb) {
		(*agent_event_cb)(device, event, properties, agent_event_cb_data);
	}
	g_mutex_unlock(&agent_event_cb_mutex);
}


/* Introspection data for the agent service */
static const gchar introspection_xml[] =
"<node>"
"   <interface name='org.bluez.Agent1'>"
"       <method name='RequestPinCode'>"
"           <arg name='device' direction='in' type='o'/>"
"           <arg name='pincode' direction='out' type='s'/>"
"       </method>"
"      <method name='RequestConfirmation'>"
"          <arg name='device' direction='in' type='o'/>"
"          <arg name='passkey' direction='in' type='u'/>"
"      </method>"
"      <method name='AuthorizeService'>"
"          <arg name='device' direction='in' type='o'/>"
"          <arg name='uuid' direction='in' type='s'/>"
"      </method>"
"      <method name='Cancel'>"
"      </method>"
"   </interface>"
"</node>";

static void handle_method_call(
		GDBusConnection *connection,
		const gchar *sender_name,
		const gchar *object_path,
		const gchar *interface_name,
		const gchar *method_name,
		GVariant *parameters,
		GDBusMethodInvocation *invocation,
		gpointer user_data)
{
	struct bluez_state *ns = user_data;
	struct call_work *cw;
	GError *error = NULL;
	const gchar *path = NULL;
	char *device = NULL;

#ifdef BLUEZ_GLIB_DEBUG
	INFO("sender=%s", sender_name);
	INFO("object_path=%s", object_path);
	INFO("interface=%s", interface_name);
	INFO("method=%s", method_name);
	DEBUG("parameters: %s", g_variant_print(parameters, TRUE));
#endif

	if (!g_strcmp0(method_name, "RequestConfirmation")) {
		int pin;

		g_variant_get(parameters, "(ou)", &path, &pin);

		call_work_lock(ns);

		/* can only occur while a pair is issued */
		cw = call_work_lookup_unlocked(ns, "device", NULL, "RequestConfirmation");

		/* if nothing is pending return an error */
		/* TODO: allow client side pairing */
		if (!cw)
			cw = call_work_create_unlocked(ns, "device", NULL,
				"RequestConfirmation", NULL, &error);

		if (!cw) {
			call_work_unlock(ns);
			g_dbus_method_invocation_return_dbus_error(invocation,
					"org.bluez.Error.Rejected",
					"No connection pending");
			return;
		}

		cw->agent_data.pin_code = pin;
		cw->agent_data.device_path = g_strdup(path);
		cw->invocation = invocation;

		call_work_unlock(ns);

		run_callback(path, BLUEZ_AGENT_EVENT_REQUEST_CONFIRMATION, parameters);

		return;

	} else if (!g_strcmp0(method_name, "RequestPinCode")) {

		g_variant_get(parameters, "(o)", &device);

		call_work_lock(ns);
		cw = call_work_lookup_unlocked(ns, BLUEZ_AT_DEVICE, device, "pair_device");

		if (!cw)
			g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", "No connection pending");
		else
			g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", cw->agent_data.fixed_pincode));

		call_work_unlock(ns);

		return;

	} else if (!g_strcmp0(method_name, "AuthorizeService")) {
#if 0
		// TODO: add some service validation here.
		//g_variant_get(parameters, "(os)", &path, &service);
#endif
		return g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (!g_strcmp0(method_name, "Cancel")) {
		
		call_work_lock(ns);

		/* can only occur while a pair is issued */
		cw = call_work_lookup_unlocked(ns, "device", NULL, "RequestConfirmation");
		if (!cw) {
			call_work_unlock(ns);
			g_dbus_method_invocation_return_dbus_error(invocation,
								   "org.bluez.Error.Rejected",
								   "No connection pending");
			return;
		}

		run_callback(path, BLUEZ_AGENT_EVENT_CANCELLED_PAIRING, parameters);

		call_work_destroy_unlocked(cw);
		call_work_unlock(ns);

		return g_dbus_method_invocation_return_value(invocation, NULL);
	}

	g_dbus_method_invocation_return_dbus_error(invocation,
						   "org.freedesktop.DBus.Error.UnknownMethod",
						   "Unknown method");
}

static const GDBusInterfaceVTable interface_vtable = {
	.method_call  = handle_method_call,
	.get_property = NULL,
	.set_property = NULL,
};

static void on_bus_acquired(GDBusConnection *connection,
			    const gchar *name, gpointer user_data)
{
	struct init_data *id = user_data;
	struct bluez_state *ns = id->ns;
	GVariant *result;
	GError *error = NULL;

	INFO("agent bus acquired - registering %s", ns->agent_path);

	ns->registration_id = g_dbus_connection_register_object(connection,
			ns->agent_path,
			ns->introspection_data->interfaces[0],
			&interface_vtable,
			ns,	/* user data */
			NULL,	/* user_data_free_func */
			NULL);

	if (!ns->registration_id) {
		ERROR("failed to register agent to dbus");
		goto err_unable_to_register_bus;

	}

	result = bluez_call(ns, BLUEZ_AT_AGENTMANAGER, NULL,
			    "RegisterAgent",
			    g_variant_new("(os)", ns->agent_path, "KeyboardDisplay"),
			    &error);
	if (!result) {
		ERROR("failed to register agent to bluez");
		goto err_unable_to_register_bluez;
	}
	g_variant_unref(result);

	result = bluez_call(ns, BLUEZ_AT_AGENTMANAGER, NULL,
			    "RequestDefaultAgent",
			    g_variant_new("(o)", ns->agent_path),
			    &error);
	if (!result) {
		ERROR("failed to request default agent to bluez");
		goto err_unable_to_request_default_agent_bluez;
	}
	g_variant_unref(result);

	ns->agent_registered = TRUE;

	INFO("agent registered at %s", ns->agent_path);
	if (id->init_done_cb)
		(*id->init_done_cb)(id, TRUE);

	DEBUG("exit!");
	return;

err_unable_to_request_default_agent_bluez:
	result = bluez_call(ns, BLUEZ_AT_AGENTMANAGER, NULL,
			    "UnregisterAgent",
			    g_variant_new("(o)", ns->agent_path),
			    &error);

err_unable_to_register_bluez:
	g_dbus_connection_unregister_object(ns->conn, ns->registration_id);
	ns->registration_id = 0;

err_unable_to_register_bus:
	if (id->init_done_cb)
		(*id->init_done_cb)(id, FALSE);
	return;
}

gboolean bluez_register_agent(struct init_data *id)
{
	struct bluez_state *ns = id->ns;

	ns->agent_path = g_strdup_printf("%s/agent%d",
					 BLUEZ_PATH, getpid());
	if (!ns->agent_path) {
		ERROR("can't create agent path");
		goto out_no_agent_path;
	}

	ns->introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
	if (!ns->introspection_data) {
		ERROR("can't create introspection data");
		goto out_no_introspection_data;
	}

	ns->agent_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
			BLUEZ_AGENT_INTERFACE,
			G_BUS_NAME_OWNER_FLAGS_REPLACE | G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT,
			on_bus_acquired,
			NULL,
			NULL,
			id,
			NULL);
	if (!ns->agent_id) {
		ERROR("can't create agent bus instance");
		goto out_no_bus_name;
	}

	return TRUE;

out_no_bus_name:
	g_dbus_node_info_unref(ns->introspection_data);
out_no_introspection_data:
	g_free(ns->agent_path);
out_no_agent_path:
	return FALSE;
}

void bluez_unregister_agent(struct bluez_state *ns)
{
	g_bus_unown_name(ns->agent_id);
	g_dbus_node_info_unref(ns->introspection_data);
	g_free(ns->agent_path);
}
