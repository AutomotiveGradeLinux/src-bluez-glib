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

#ifndef BLUEZ_AGENT_H
#define BLUEZ_AGENT_H

#define _GNU_SOURCE
#include <glib.h>
/*
#include <stdlib.h>
#include <gio/gio.h>
#include <glib-object.h>
*/

#include "common.h"

struct agent_data {
	int pin_code;
	gchar *fixed_pincode;
	gchar *device_path;
};

gboolean bluez_register_agent(struct init_data *id);

void bluez_unregister_agent(struct bluez_state *ns);

#endif /* BLUEZ_AGENT_H */
