GLib BlueZ interface library
----------------------------
Derived from AGL Bluetooth binding source at
[https://git.automotivelinux.org/apps/agl-service-bluetooth/]

Source refactoring from the binding (not necessarily exhaustive):
* bluetooth-agent.c -> bluez-agent.c
* bluetooth-api.c -> api.c, call_work.c
* bluetooth-bluez.c -> bluez-call.c
* bluetooth-common.h -> common.h, bluez-call.h, call_work.h
* bluetooth-conf.c -> conf.c
* bluetooth-util.c -> mostly redundant, required things moved to api.c, bluez-call.c

Copyright dates in files reflect their original creation dates and include
revisions made in agl-service-bluetooth before December 2021.

Building
--------
The build requirements are:
* glib 2.0 headers and libraries (from e.g. glib2-devel on Fedora or CentOS,
  libglib2.0-dev on Debian or Ubuntu).
* meson

To build:
```
meson build/
ninja -C build/
```

Usage Notes
-----------
* Users only need include `bluez-glib.h` and link to the library.
* API calls generally return a gboolean, with `FALSE` indicating failure.
* `bluez_init` must be called before any other API calls except
  `bluez_set_log_level` or one of the callback registration functions
  (e.g. `bluez_add_adapter_event_callback`).
* A return code of `TRUE` from `bluez_init` indicates D-Bus connection to
  **BlueZ** has succeeded, but other API calls will not be useful until the
  provided callback has been called indicating a success status.  This
  behavior stems from the possibility of running on systems without Bluetooth
  adapters present.
* Callbacks may be registered after calling `bluez_init`, but note that there
  is a possibility that registration calls may briefly block if they occur
  during processing of an associated event.
* It is advised that only one primary user of the library enable agent and
  autoconnect support to avoid conflicts.

Contributing
------------
Questions can be sent to the agl-dev-community mailing list at
<https://lists.automotivelinux.org/g/agl-dev-community>.

Bugs can be filed on the AGL JIRA instance at <https://jira.automotivelinux.org>.

Source contributions need to go through the AGL Gerrit instance, see
<https://wiki.automotivelinux.org/agl-distro/contributing>.
