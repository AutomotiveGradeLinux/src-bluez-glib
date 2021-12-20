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

#ifndef BLUETOOTH_CALL_H
#define BLUETOOTH_CALL_H

#include "bluez-agent.h"

struct call_work {
	struct bluez_state *ns;
	int id;
	gchar *access_type;
	gchar *type_arg;
	gchar *method;
	gchar *bluez_method;
	struct bluez_pending_work *cpw;
	gpointer request_cb;
	gpointer request_user_data;
	struct agent_data agent_data;
	GDBusMethodInvocation *invocation;
};

void call_work_lock(struct bluez_state *ns);

void call_work_unlock(struct bluez_state *ns);

struct call_work *call_work_lookup_unlocked(struct bluez_state *ns,
					    const char *access_type,
					    const char *type_arg,
					    const char *method);

struct call_work *call_work_create_unlocked(struct bluez_state *ns,
					    const char *access_type,
					    const char *type_arg,
					    const char *method,
					    const char *bluez_method,
					    GError **error);

struct call_work *call_work_create(struct bluez_state *ns,
				   const char *access_type,
				   const char *type_arg,
				   const char *method,
				   const char *bluez_method,
				   GError **error);

void call_work_destroy(struct call_work *cw);

void call_work_destroy_unlocked(struct call_work *cw);

#endif /* BLUETOOTH_CALL_H */
