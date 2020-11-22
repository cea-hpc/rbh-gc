/* This file is part of rbh-gc
 * Copyright (C) 2020 Commissariat a l'energie atomique et aux energies
 *                    alternatives
 *
 * SPDX-License-Identifer: LGPL-3.0-or-later
 */

#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include <robinhood/utils.h>

static struct rbh_backend *backend;
static int mount_fd = -1;

static void __attribute__((destructor))
exit_mount_fd(void)
{
    if (mount_fd < 0)
        return;
    if (close(mount_fd))
        error(EXIT_FAILURE, errno, "close");
}

static void __attribute__((destructor))
exit_backend(void)
{
    if (backend)
        rbh_backend_destroy(backend);
}

static void
usage(void)
{
    const char *message =
        "usage: %s [-h] BACKEND PATH\n"
        "\n"
        "Iterate on a robinhood BACKEND's entries ready for garbage collection.\n"
        "If these entries are absent from the filesystem mounted at PATH, delete them\n"
        "from BACKEND for good.\n"
        "\n"
        "Positional arguments:\n"
        "    BACKEND  a URI describing a robinhood backend\n"
        "    PATH     a path in the filesystem which BACKEND mirrors\n"
        "\n"
        "Optional arguments:\n"
        "    -h, --help  print this message and exit\n";

    printf(message, program_invocation_short_name);
}

static void
gc(void)
{
    error(EX_SOFTWARE, ENOSYS, "gc");
}

int
main(int argc, char *argv[])
{
    const struct option LONG_OPTIONS[] = {
        {
            .name = "help",
            .val = 'h',
        },
        {}
    };
    char c;

    /* Parse the command line */
    while ((c = getopt_long(argc, argv, "h", LONG_OPTIONS, NULL)) != -1) {
        switch (c) {
        case 'h':
            usage();
            return 0;
        case '?':
        default:
            /* getopt_long() prints meaningful error messages itself */
            exit(EX_USAGE);
        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 2)
        error(EX_USAGE, 0, "not enough arguments");
    if (argc > 2)
        error(EX_USAGE, 0, "unexpected argument: %s", argv[2]);

    /* Parse BACKEND */
    backend = rbh_backend_from_uri(argv[0]);

    /* Parse PATH */
    mount_fd = open(argv[1], O_RDONLY);
    if (mount_fd < 0)
        error(EXIT_FAILURE, errno, "open: %s", argv[1]);

    gc();
    return EXIT_SUCCESS;
}
