/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2014 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stddef.h>

#include "env-util.h"
#include "log.h"
#include "macro.h"
#include "string-util.h"
#include "verbs.h"
#include "virt.h"

/* Wraps running_in_chroot() which is used in various places,
 * but also adds an environment variable check so external processes
 * can reliably force this on.
 */
bool running_in_chroot_or_offline(void) {
        int r;

        /* Added to support use cases like rpm-ostree, where from %post
         * scripts we only want to execute "preset", but not "start"/"restart"
         * for example.
         *
         * See ENVIRONMENT.md for docs.
         */
        r = getenv_bool("SYSTEMD_OFFLINE");
        if (r < 0)
                log_debug_errno(r, "Parsing SYSTEMD_OFFLINE: %m");
        else if (r == 0)
                return false;
        else
                return true;

        /* We've had this condition check for a long time which basically
         * checks for legacy chroot case like Fedora's
         * "mock", which is used for package builds.  We don't want
         * to try to start systemd services there, since without --new-chroot
         * we don't even have systemd running, and even if we did, adding
         * a concept of background daemons to builds would be an enormous change,
         * requiring considering things like how the journal output is handled, etc.
         * And there's really not a use case today for a build talking to a service.
         *
         * Note this call itself also looks for a different variable SYSTEMD_IGNORE_CHROOT=1.
         */
        r = running_in_chroot();
        if (r < 0)
                log_debug_errno(r, "running_in_chroot(): %m");
        else if (r > 0)
                return true;

        return false;
}

int dispatch_verb(int argc, char *argv[], const Verb verbs[], void *userdata) {
        const Verb *verb;
        const char *name;
        unsigned i;
        int left, r;

        assert(verbs);
        assert(verbs[0].dispatch);
        assert(argc >= 0);
        assert(argv);
        assert(argc >= optind);

        left = argc - optind;
        name = argv[optind];

        for (i = 0;; i++) {
                bool found;

                /* At the end of the list? */
                if (!verbs[i].dispatch) {
                        if (name)
                                log_error("Unknown operation %s.", name);
                        else
                                log_error("Requires operation parameter.");
                        return -EINVAL;
                }

                if (name)
                        found = streq(name, verbs[i].verb);
                else
                        found = !!(verbs[i].flags & VERB_DEFAULT);

                if (found) {
                        verb = &verbs[i];
                        break;
                }
        }

        assert(verb);

        if (!name)
                left = 1;

        if (verb->min_args != VERB_ANY &&
            (unsigned) left < verb->min_args) {
                log_error("Too few arguments.");
                return -EINVAL;
        }

        if (verb->max_args != VERB_ANY &&
            (unsigned) left > verb->max_args) {
                log_error("Too many arguments.");
                return -EINVAL;
        }

        if ((verb->flags & VERB_ONLINE_ONLY) && running_in_chroot_or_offline()) {
                if (name)
                        log_info("Running in chroot, ignoring request: %s", name);
                else
                        log_info("Running in chroot, ignoring request.");
                return 0;
        }

        if (verb->flags & VERB_MUST_BE_ROOT) {
                r = must_be_root();
                if (r < 0)
                        return r;
        }

        if (name)
                return verb->dispatch(left, argv + optind, userdata);
        else {
                char* fake[2] = {
                        (char*) verb->verb,
                        NULL
                };

                return verb->dispatch(1, fake, userdata);
        }
}
