/*
 * Copyright 2021 Konsulko Group
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

#ifndef BLUEZ_GLIB_H
#define BLUEZ_GLIB_H

#include <glib.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum {
	BLUEZ_LOG_LEVEL_ERROR,
	BLUEZ_LOG_LEVEL_WARNING,
	BLUEZ_LOG_LEVEL_INFO,
	BLUEZ_LOG_LEVEL_DEBUG
} bluez_log_level_t;

// Hook to allow users to override the default level
#ifndef BLUEZ_LOG_LEVEL_DEFAULT
#define BLUEZ_LOG_LEVEL_DEFAULT BLUEZ_LOG_LEVEL_ERROR
#endif

typedef enum {
	BLUEZ_EVENT_ADD,
	BLUEZ_EVENT_REMOVE,
	BLUEZ_EVENT_CHANGE
} bluez_event_t;

typedef enum {
	BLUEZ_AGENT_EVENT_REQUEST_CONFIRMATION,
	BLUEZ_AGENT_EVENT_AUTHORIZE_SERVICE,
	BLUEZ_AGENT_EVENT_CANCELLED_PAIRING
} bluez_agent_event_t;

typedef enum {
	BLUEZ_MEDIA_CONTROL_CONNECT,
	BLUEZ_MEDIA_CONTROL_DISCONNECT,
	BLUEZ_MEDIA_CONTROL_PLAY,
	BLUEZ_MEDIA_CONTROL_PAUSE,
	BLUEZ_MEDIA_CONTROL_STOP,
	BLUEZ_MEDIA_CONTROL_NEXT,
	BLUEZ_MEDIA_CONTROL_PREVIOUS,
	BLUEZ_MEDIA_CONTROL_FASTFORWARD,
	BLUEZ_MEDIA_CONTROL_REWIND
} bluez_media_control_t;

typedef void (*bluez_init_cb_t)(gchar *adapter, gboolean status, gpointer user_data);

typedef void (*bluez_adapter_event_cb_t)(gchar *adapter,
					 bluez_event_t event,
					 GVariant *properties,
					 gpointer user_data);

typedef void (*bluez_device_event_cb_t)(gchar *adapter,
					gchar *device,
					bluez_event_t event,
					GVariant *properties,
					gpointer user_data);

typedef void (*bluez_media_control_event_cb_t)(gchar *adapter,
					       gchar *device,
					       gchar *endpoint,
					       bluez_event_t event,
					       GVariant *properties,
					       gpointer user_data);

typedef void (*bluez_media_player_event_cb_t)(gchar *adapter,
					      gchar *device,
					      gchar *player,
					      bluez_event_t event,
					      GVariant *properties,
					      gpointer user_data);

typedef void (*bluez_agent_event_cb_t)(gchar *device,
				       bluez_agent_event_t event,
				       GVariant *properties,
				       gpointer user_data);

typedef void (*bluez_device_connect_cb_t)(gchar *device, gboolean status, gpointer user_data);

typedef void (*bluez_device_pair_cb_t)(gchar *device, gboolean status, gpointer user_data);

void bluez_add_adapter_event_callback(bluez_adapter_event_cb_t cb, gpointer user_data);

void bluez_add_device_event_callback(bluez_device_event_cb_t cb, gpointer user_data);

void bluez_add_media_control_event_callback(bluez_media_control_event_cb_t cb, gpointer user_data);

void bluez_add_media_player_event_callback(bluez_media_player_event_cb_t cb, gpointer user_data);

void bluez_add_agent_event_callback(bluez_agent_event_cb_t cb, gpointer user_data);

void bluez_set_log_level(bluez_log_level_t level);

gboolean bluez_init(gboolean register_agent,
		    gboolean autoconnect,
		    bluez_init_cb_t cb,
		    gpointer user_data);

char *bluez_get_default_adapter(void);

gboolean bluez_set_default_adapter(const char *adapter, char **adapter_new);

gboolean bluez_get_managed_objects(GVariant **reply);

gboolean bluez_get_adapters(GArray **reply);

gboolean bluez_adapter_get_state(const char *adapter, GVariant **reply);

gboolean bluez_adapter_get_devices(const char *adapter, GVariant **reply);

gboolean bluez_adapter_set_discovery(const char *adapter, gboolean scan);

gboolean bluez_adapter_set_discovery_filter(const char *adapter, gchar **uuids, gchar *transport);

gboolean bluez_adapter_set_discoverable(const char *adapter, gboolean discoverable);

gboolean bluez_adapter_set_powered(const char *adapter, gboolean powered);

gboolean bluez_device_connect(const char *device,
			      const char *uuid,
			      bluez_device_connect_cb_t cb,
			      gpointer user_data);

gboolean bluez_device_disconnect(const char *device, const char *uuid);

gboolean bluez_device_pair(const char *device,
			   bluez_device_pair_cb_t cb,
			   gpointer user_data);

gboolean bluez_cancel_pairing(void);

gboolean bluez_confirm_pairing(const char *pincode_str);

gboolean bluez_device_remove(const char *device);

gboolean bluez_device_avrcp_controls(const char *device, bluez_media_control_t action);

gboolean bluez_set_pincode(const char *pincode);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* BLUEZ_GLIB_H */
