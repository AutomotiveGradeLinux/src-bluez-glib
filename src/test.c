#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "bluez-glib.h"

void adapter_event_cb(gchar *adapter, bluez_event_t event, GVariant *properties, gpointer user_data)
{
	switch(event) {
	case BLUEZ_EVENT_ADD:
		printf("adapter %s add: ", adapter);
		break;
	case BLUEZ_EVENT_REMOVE:
		printf("adapter %s remove: ", adapter);
		break;
	case BLUEZ_EVENT_CHANGE:
		printf("adapter %s change: ", adapter);
		break;
	default:
		break;
	}
	gchar *p = g_variant_print(properties, TRUE);
	printf("%s\n\n", p);
	g_free(p);
}

void device_event_cb(gchar *adapter, gchar *device, bluez_event_t event, GVariant *properties, gpointer user_data)
{
	switch(event) {
	case BLUEZ_EVENT_ADD:
		printf("adapter %s, device %s add: ", adapter, device);
		break;
	case BLUEZ_EVENT_REMOVE:
		printf("adapter %s, device %s remove: ", adapter, device);
		break;
	case BLUEZ_EVENT_CHANGE:
		printf("adapter %s, device %s change: ", adapter, device);
		break;
	default:
		break;
	}
	gchar *p = g_variant_print(properties, TRUE);
	printf("%s\n\n", p);
	g_free(p);
}

void media_control_event_cb(gchar *adapter, gchar *device, gchar *endpoint, bluez_event_t event, GVariant *properties, gpointer user_data)
{
	switch(event) {
	case BLUEZ_EVENT_ADD:
		printf("adapter %s, device %s, endpoint %s add: ", adapter, device, endpoint);
		break;
	case BLUEZ_EVENT_REMOVE:
		printf("adapter %s, device %s, endpoint %s remove: ", adapter, device, endpoint);
		break;
	case BLUEZ_EVENT_CHANGE:
		printf("adapter %s, device %s, endpoint %s change: ", adapter, device, endpoint);
		break;
	default:
		break;
	}
	gchar *p = g_variant_print(properties, TRUE);
	printf("%s\n\n", p);
	g_free(p);
}

void media_player_event_cb(gchar *adapter, gchar *device, gchar *player, bluez_event_t event, GVariant *properties, gpointer user_data)
{
	switch(event) {
	case BLUEZ_EVENT_ADD:
		printf("adapter %s, device %s, player %s add: ", adapter, device, player);
		break;
	case BLUEZ_EVENT_REMOVE:
		printf("adapter %s, device %s, player %s remove: ", adapter, device, player);
		break;
	case BLUEZ_EVENT_CHANGE:
		printf("adapter %s, device %s, player %s change: ", adapter, device, player);
		break;
	default:
		break;
	}
	gchar *p = g_variant_print(properties, TRUE);
	printf("%s\n\n", p);
	g_free(p);
}


int main(int argc, char *argv[])
{
	gboolean rc;
	char *adapter;
	GVariant *reply = NULL;
	GArray *adapters = NULL;

	bluez_add_adapter_event_callback(adapter_event_cb, NULL);
	bluez_add_device_event_callback(device_event_cb, NULL);
	bluez_add_media_control_event_callback(media_control_event_cb, NULL);
	bluez_add_media_player_event_callback(media_player_event_cb, NULL);

	// Should ideally pass callback here and wait for it to report success
	rc = bluez_init(TRUE, FALSE, NULL, NULL);
	if (!rc) {
		printf("bluez_init failed!\n");
		exit(1);
	}

	sleep(2);

	GVariant *objects;
	rc = bluez_get_managed_objects(&objects);
	if(rc) {
		printf("managed objects: %s\n", objects ? g_variant_print(objects, TRUE) : "(null)");
	}

	rc = bluez_get_adapters(&adapters);
	if(rc && adapters) {
		printf("# adapters = %d\n", adapters->len);
		printf("adapters: ");
		for(int i = 0; i < adapters->len; i++) {
			gchar *adapter = g_array_index(adapters, gchar*, i);
			printf("%s ", adapter ? adapter : "(null)");
		}
		printf("\n");
	}

	adapter = bluez_get_default_adapter();
	if(!adapter) {
		printf("adapter == NULL!\n");
		return -1;
	}
	printf("default adapter = %s\n", adapter);

	rc = bluez_adapter_get_state(NULL, &reply);

	rc = bluez_adapter_get_devices(NULL, &reply);
	if(rc) {
		printf("\nDevices: %s\n\n", reply ? g_variant_print(reply, TRUE) : "(null)");
	}

	printf("Setting discoverable!\n");
	rc = bluez_adapter_set_discoverable(adapter, TRUE);

	printf("Starting scan!\n");
	rc = bluez_adapter_set_discovery(adapter, TRUE);

	rc = bluez_adapter_get_state(NULL, &reply);
	printf("\n");

	sleep(20);

	rc = bluez_adapter_get_state(NULL, &reply);
	if(rc) {
		printf("adapter %s state: %s\n",
		       adapter, reply ? g_variant_print(reply, TRUE) : "(null)");
		GVariantDict *props_dict = g_variant_dict_new(reply); 
		gboolean powered = FALSE; 
		if (g_variant_dict_lookup(props_dict, "Powered", "b", &powered)) { 
			printf("powered = %d\n", (int) powered);
		} 
		g_variant_dict_unref(props_dict);
		g_variant_unref(reply);
	}
	printf("\n");

	rc = bluez_adapter_get_devices(NULL, &reply);
	if(rc) {
		printf("adapter %s devices: %s\n",
		       adapter, reply ? g_variant_print(reply, TRUE) : "(null)");

		GVariantIter *array = NULL;
		g_variant_get(reply, "a{sv}", &array);
		const gchar *key = NULL;
		GVariant *var = NULL;
		int i = 0;
		while (g_variant_iter_next(array, "{&sv}", &key, &var)) {
			printf("Device %d = %s\n", i++, key);

			GVariantDict *props_dict = g_variant_dict_new(var);
			if (!props_dict) {
				g_variant_unref(var);
				continue;
			}

			const char *dev_str = key;
			char *name_str = NULL;
			char *address_str = NULL;
			char *paired_str = NULL;
			char *connected_str = NULL;

			gchar *p = NULL;
			if (g_variant_dict_lookup(props_dict, "Address", "s", &p)) {
				address_str = g_strdup(p);
				g_free(p);
			}

			p = NULL;
			if (g_variant_dict_lookup(props_dict, "Name", "s", &p)) {
				name_str = g_strdup(p);
				g_free(p);
			}

			gboolean paired = FALSE;
			if (g_variant_dict_lookup(props_dict, "Paired", "b", &paired)) {
				paired_str = paired ? "yes" : "no";
			}

			gboolean connected = FALSE;
			if (g_variant_dict_lookup(props_dict, "Connected", "b", &connected)) {
				connected_str = connected ? "yes" : "no";
			}

			printf("Device: %s, Address: %s, Name: %s, Paired: %s, Connected: %s\n",
			       dev_str, address_str, name_str, paired_str, connected_str);

			g_variant_unref(var);
		}
		g_variant_iter_free(array);
	}
	printf("\n");

	printf("Stopping scan!\n");
	rc = bluez_adapter_set_discovery(adapter, FALSE);

	printf("\n");
	rc = bluez_adapter_get_state(NULL, &reply);
	if(rc) {
		printf("adapter %s state: %s\n",
		       adapter, reply ? g_variant_print(reply, TRUE) : "(null)");
	}
	
	return 0;
}
