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

#ifndef BLUEZ_CONF_H
#define BLUEZ_CONF_H

//#include <stddef.h>

//#define _GNU_SOURCE
#include <glib.h>
/*
#include <stdlib.h>
#include <gio/gio.h>
#include <glib-object.h>
*/

gchar *get_default_adapter(void);
gboolean set_default_adapter(const char *adapter);

gboolean set_pincode(const char *pincode, char **error);
gchar *get_pincode(void);

#endif /* BLUEZ_CONF_H */
