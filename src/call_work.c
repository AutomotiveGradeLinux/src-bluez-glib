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

#include "common.h"
#include "call_work.h"
#include "bluez-call.h"

void call_work_lock(struct bluez_state *ns)
{
	g_mutex_lock(&ns->cw_mutex);
}

void call_work_unlock(struct bluez_state *ns)
{
	g_mutex_unlock(&ns->cw_mutex);
}

struct call_work *call_work_lookup_unlocked(
		struct bluez_state *ns,
		const char *access_type,
		const char *type_arg,
		const char *method)
{
	struct call_work *cw;
	GSList *list;

	/* we can only allow a single pending call */
	for (list = ns->cw_pending; list; list = g_slist_next(list)) {
		cw = list->data;
		if (!g_strcmp0(access_type, cw->access_type) &&
		    !g_strcmp0(type_arg, cw->type_arg) &&
		    !g_strcmp0(method, cw->method))
			return cw;
	}
	return NULL;
}

struct call_work *call_work_lookup(
		struct bluez_state *ns,
		const char *access_type,
		const char *type_arg,
		const char *method)
{
	struct call_work *cw;

	g_mutex_lock(&ns->cw_mutex);
	cw = call_work_lookup_unlocked(ns, access_type, type_arg, method);
	g_mutex_unlock(&ns->cw_mutex);

	return cw;
}

int call_work_pending_id(
		struct bluez_state *ns,
		const char *access_type,
		const char *type_arg,
		const char *method)
{
	struct call_work *cw;
	int id = -1;

	g_mutex_lock(&ns->cw_mutex);
	cw = call_work_lookup_unlocked(ns, access_type, type_arg, method);
	if (cw)
		id = cw->id;
	g_mutex_unlock(&ns->cw_mutex);

	return id;
}

struct call_work *call_work_lookup_by_id_unlocked(struct bluez_state *ns, int id)
{
	struct call_work *cw;
	GSList *list;

	/* we can only allow a single pending call */
	for (list = ns->cw_pending; list; list = g_slist_next(list)) {
		cw = list->data;
		if (cw->id == id)
			return cw;
	}
	return NULL;
}

struct call_work *call_work_lookup_by_id(struct bluez_state *ns, int id)
{
	struct call_work *cw;

	g_mutex_lock(&ns->cw_mutex);
	cw = call_work_lookup_by_id_unlocked(ns, id);
	g_mutex_unlock(&ns->cw_mutex);

	return cw;
}

struct call_work *call_work_create_unlocked(struct bluez_state *ns,
					    const char *access_type,
					    const char *type_arg,
					    const char *method,
					    const char *bluez_method,
					    GError **error)
{

	struct call_work *cw = NULL;

	cw = call_work_lookup_unlocked(ns, access_type, type_arg, method);
	if (cw) {
		g_set_error(error, BLUEZ_ERROR, BLUEZ_ERROR_CALL_IN_PROGRESS,
			    "another call in progress (%s/%s/%s)",
			    access_type, type_arg, method);
		return NULL;
	}

	/* no other pending; allocate */
	cw = g_malloc0(sizeof(*cw));
	cw->ns = ns;
	do {
		cw->id = ns->next_cw_id;
		if (++ns->next_cw_id < 0)
			ns->next_cw_id = 1;
	} while (call_work_lookup_by_id_unlocked(ns, cw->id));

	cw->access_type = g_strdup(access_type);
	cw->type_arg = g_strdup(type_arg);
	cw->method = g_strdup(method);
	cw->bluez_method = g_strdup(bluez_method);

	ns->cw_pending = g_slist_prepend(ns->cw_pending, cw);

	return cw;
}

struct call_work *call_work_create(struct bluez_state *ns,
				   const char *access_type,
				   const char *type_arg,
				   const char *method,
				   const char *bluez_method,
				   GError **error)
{

	struct call_work *cw;

	g_mutex_lock(&ns->cw_mutex);
	cw = call_work_create_unlocked(ns,
				       access_type, type_arg, method, bluez_method,
				       error);
	g_mutex_unlock(&ns->cw_mutex);

	return cw;
}

void call_work_destroy_unlocked(struct call_work *cw)
{
	struct bluez_state *ns = cw->ns;
	struct call_work *cw2;

	/* verify that it's something we know about */
	cw2 = call_work_lookup_by_id_unlocked(ns, cw->id);
	if (cw2 != cw) {
		ERROR("Bad call work to destroy");
		return;
	}

	/* remove it */
	ns->cw_pending = g_slist_remove(ns->cw_pending, cw);

	/* agent struct data */
	g_free(cw->agent_data.device_path);
	cw->agent_data.device_path = NULL;
	g_free(cw->access_type);
	g_free(cw->type_arg);
	g_free(cw->method);
	g_free(cw->bluez_method);
	g_free(cw->agent_data.fixed_pincode);
	cw->agent_data.fixed_pincode = NULL;
}

void call_work_destroy(struct call_work *cw)
{
	struct bluez_state *ns = cw->ns;

	g_mutex_lock(&ns->cw_mutex);
	call_work_destroy_unlocked(cw);
	g_mutex_unlock(&ns->cw_mutex);
}
