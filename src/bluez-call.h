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

#ifndef BLUEZ_CALL_H
#define BLUEZ_CALL_H

#include <glib.h>

#define BLUEZ_DEFAULT_ADAPTER			"hci0"
#define BLUEZ_DEFAULT_PLAYER			"player0"

typedef enum {
	BLUEZ_ERROR_BAD_TECHNOLOGY,
	BLUEZ_ERROR_BAD_SERVICE,
	BLUEZ_ERROR_OUT_OF_MEMORY,
	BLUEZ_ERROR_NO_TECHNOLOGIES,
	BLUEZ_ERROR_NO_SERVICES,
	BLUEZ_ERROR_BAD_PROPERTY,
	BLUEZ_ERROR_UNIMPLEMENTED,
	BLUEZ_ERROR_UNKNOWN_PROPERTY,
	BLUEZ_ERROR_UNKNOWN_TECHNOLOGY,
	BLUEZ_ERROR_UNKNOWN_SERVICE,
	BLUEZ_ERROR_MISSING_ARGUMENT,
	BLUEZ_ERROR_ILLEGAL_ARGUMENT,
	BLUEZ_ERROR_CALL_IN_PROGRESS,
} NBError;

#define BLUEZ_ERROR (bluez_error_quark())

extern GQuark bluez_error_quark(void);

#define BLUEZ_SERVICE                 		"org.bluez"
#define BLUEZ_ADAPTER_INTERFACE			BLUEZ_SERVICE ".Adapter1"
#define BLUEZ_AGENT_INTERFACE			BLUEZ_SERVICE ".Agent1"
#define BLUEZ_AGENTMANAGER_INTERFACE		BLUEZ_SERVICE ".AgentManager1"
#define BLUEZ_DEVICE_INTERFACE			BLUEZ_SERVICE ".Device1"
#define BLUEZ_MEDIAPLAYER_INTERFACE		BLUEZ_SERVICE ".MediaPlayer1"
#define BLUEZ_MEDIATRANSPORT_INTERFACE		BLUEZ_SERVICE ".MediaTransport1"
#define BLUEZ_MEDIACONTROL_INTERFACE		BLUEZ_SERVICE ".MediaControl1"

#define BLUEZ_OBJECT_PATH			"/"
#define BLUEZ_PATH				"/org/bluez"

#define BLUEZ_ERRMSG(error) \
     (error ? error->message : "unspecified")

#define FREEDESKTOP_INTROSPECT			"org.freedesktop.DBus.Introspectable"
#define FREEDESKTOP_PROPERTIES			"org.freedesktop.DBus.Properties"
#define FREEDESKTOP_OBJECTMANAGER		"org.freedesktop.DBus.ObjectManager"

#define DBUS_REPLY_TIMEOUT			(120 * 1000)
#define DBUS_REPLY_TIMEOUT_SHORT		(10 * 1000)

#define BLUEZ_AT_OBJECT				"object"
#define BLUEZ_AT_ADAPTER			"adapter"
#define BLUEZ_AT_DEVICE				"device"
#define BLUEZ_AT_AGENT				"agent"
#define BLUEZ_AT_AGENTMANAGER			"agent-manager"
#define BLUEZ_AT_MEDIAPLAYER			"mediaplayer"
#define BLUEZ_AT_MEDIATRANSPORT			"mediatransport"
#define BLUEZ_AT_MEDIACONTROL			"mediacontrol"

#define BLUEZ_ROOT_PATH(_t) \
    ({ \
     call_work_lock(ns); \
     const char *__t = (_t); \
     size_t __len = strlen(BLUEZ_PATH) + 1 + \
     strlen(__t) + 1; \
     char *__tpath; \
     __tpath = alloca(__len + 1 + 1); \
     snprintf(__tpath, __len + 1, \
             BLUEZ_PATH "/%s", __t); \
     call_work_unlock(ns); \
             __tpath; \
     })

struct bluez_state;

static inline int split_length(const char *path) {
	gchar **strings = g_strsplit(path, "/", -1);
	int ret = g_strv_length(strings) ;

	g_strfreev(strings);
	return ret;
}

static inline gchar *find_index(const char *path, int idx)
{
	gchar **strings = g_strsplit(path, "/", -1);
	gchar *item = NULL;

	if (g_strv_length(strings) > idx)
		item = g_strdup(strings[idx]);

	g_strfreev(strings);
	return item;
}

static inline gchar *bluez_return_adapter(const char *path)
{
	return find_index(path, 3);
}

static inline gchar *bluez_return_device(const char *path)
{
	return find_index(path, 4);
}

static inline gchar *bluez_return_endpoint(const char *path)
{
	gchar **strings = g_strsplit(path, "/", -1);
	gchar *item = NULL;

	if (g_strv_length(strings) > 6)
		item = g_strconcat(strings[5], "/", strings[6], NULL);

	g_strfreev(strings);
	return item;
}


static inline gboolean is_mediaplayer1_interface(const char *path)
{
	gchar *data = NULL;
	gboolean ret;

	// Don't trigger on NowPlaying, Item, etc paths
	if (split_length(path) != 6)
		return FALSE;

	// Check for 'playerX' suffix, not always player0
	data = find_index(path, 5);
	ret = !strncmp(data, BLUEZ_DEFAULT_PLAYER, sizeof(BLUEZ_DEFAULT_PLAYER) - 1);
	g_free(data);

	return ret;
}

static inline gboolean is_mediatransport1_interface(const char *path)
{
	gchar *data = NULL;
	gboolean ret;

	// Don't trigger on NowPlaying, Item, etc paths
	if (split_length(path) != 6)
		return FALSE;

	data = find_index(path, 5);
	ret = g_str_has_prefix(data, "fd");
	g_free(data);

	return ret;
}

GVariant *bluez_call(struct bluez_state *ns,
		     const char *access_type,
		     const char *type_arg,
		     const char *method,
		     GVariant *params,
		     GError **error);

GVariant *bluez_get_properties(struct bluez_state *ns,
			       const char *access_type,
			       const char *path,
			       GError **error);

GVariant *bluez_get_adapter_properties(struct bluez_state *ns,
				       const char *adapter,
				       GError **error);

GVariant *bluez_get_property(struct bluez_state *ns,
			     const char *access_type,
			     const char *path,
			     const char *name,
			     GError **error);

gboolean bluez_set_boolean_property(struct bluez_state *ns,
				    const char *access_type,
				    const char *type_arg,
				    const char *name,
				    gboolean value,
				    GError **error);

gboolean bluez_autoconnect(gpointer data);

struct bluez_pending_work {
	struct bluez_state *ns;
	void *user_data;
	GCancellable *cancel;
	void (*callback)(void *user_data, GVariant *result, GError **error);
};

void bluez_cancel_call(struct bluez_state *ns,
		       struct bluez_pending_work *cpw);

struct bluez_pending_work *
bluez_call_async(struct bluez_state *ns,
		 const char *access_type,
		 const char *type_arg,
		 const char *method,
		 GVariant *params,
		 GError **error,
		 void (*callback)(void *user_data, GVariant *result, GError **error),
		 void *user_data);

void bluez_decode_call_error(struct bluez_state *ns,
			     const char *access_type,
			     const char *type_arg,
			     const char *method,
			     GError **error);

#endif /* BLUEZ_CALL_H */
