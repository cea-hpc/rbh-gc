/* This file is part of rbh-gc
 * Copyright (C) 2020 Commissariat a l'energie atomique et aux energies
 *                    alternatives
 *
 * SPDX-License-Identifer: LGPL-3.0-or-later
 */

#include <assert.h>
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

/*----------------------------------------------------------------------------*
 |                                  usage()                                   |
 *----------------------------------------------------------------------------*/

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

/*----------------------------------------------------------------------------*
 |                                    gc()                                    |
 *----------------------------------------------------------------------------*/

    /*--------------------------------------------------------------------*
     |                          open_by_id_at()                           |
     *--------------------------------------------------------------------*/

static int
open_by_id_at(int mount_fd, const struct rbh_id *id, int flags)
{
    (void)mount_fd;
    (void)id;
    (void)flags;
    error(EX_SOFTWARE, ENOSYS, "open_by_id_at");
    __builtin_unreachable();
}

    /*--------------------------------------------------------------------*
     |                       iter_fsentry2delete()                        |
     *--------------------------------------------------------------------*/

struct fsentry2delete_iterator {
    struct rbh_iterator iterator;

    struct rbh_iterator *fsentries;
    struct rbh_fsevent delete;
};

static const void *
fsentry2delete_iter_next(void *iterator)
{
    struct fsentry2delete_iterator *deletes = iterator;

    while (true) {
        const struct rbh_fsentry *fsentry;
        int fd;

        fsentry = rbh_iter_next(deletes->fsentries);
        if (fsentry == NULL)
            return NULL;
        assert((fsentry->mask & RBH_FP_ID) == RBH_FP_ID);

        errno = 0;
        fd = open_by_id_at(mount_fd, &fsentry->id,
                           O_RDONLY | O_NOFOLLOW | O_PATH);
        if (fd < 0 && errno != ENOENT && errno != ESTALE)
            /* Something happened, something bad... */
            error(EXIT_FAILURE, errno, "open_by_handle_at");

        if (fd >= 0) {
            /* The entry still exists somewhere in the filesystem
             *
             * Let's not delete it yet.
             */
            if (close(fd))
                /* This should never happen */
                error(EXIT_FAILURE, errno, "unexpected error on close");
            continue;
        }

        deletes->delete.id = fsentry->id;
        return &deletes->delete;
    }
}

static void
fsentry2delete_iter_destroy(void *iterator)
{
    struct fsentry2delete_iterator *deletes = iterator;

    rbh_iter_destroy(deletes->fsentries);
    free(deletes);
}

static const struct rbh_iterator_operations FSENTRY2DELETE_ITER_OPS = {
    .next = fsentry2delete_iter_next,
    .destroy = fsentry2delete_iter_destroy,
};

static const struct rbh_iterator FSENTRY2DELETE_ITERATOR = {
    .ops = &FSENTRY2DELETE_ITER_OPS,
};

static struct rbh_iterator *
iter_fsentry2delete(struct rbh_iterator *fsentries)
{
    struct fsentry2delete_iterator *deletes;

    deletes = malloc(sizeof(*deletes));
    if (deletes == NULL)
        error(EXIT_FAILURE, errno, "malloc");

    deletes->iterator = FSENTRY2DELETE_ITERATOR;
    deletes->fsentries = fsentries;
    return &deletes->iterator;
}

    /*--------------------------------------------------------------------*
     |                          iter_constify()                           |
     *--------------------------------------------------------------------*/

/* XXX: maybe this deserves a place in robinhood/itertools.h? */
struct constify_iterator {
    struct rbh_iterator iterator;

    struct rbh_mut_iterator *subiter;
    void *element;
};

static const void *
constify_iter_next(void *iterator)
{
    struct constify_iterator *constify = iterator;

    free(constify->element);
    constify->element = rbh_mut_iter_next(constify->subiter);
    return constify->element;
}

static void
constify_iter_destroy(void *iterator)
{
    struct constify_iterator *constify = iterator;

    free(constify->element);
    rbh_mut_iter_destroy(constify->subiter);
    free(constify);
}

static const struct rbh_iterator_operations CONSTIFY_ITER_OPS = {
    .next = constify_iter_next,
    .destroy = constify_iter_destroy,
};

static const struct rbh_iterator CONSTIFY_ITERATOR = {
    .ops = &CONSTIFY_ITER_OPS,
};

static struct rbh_iterator *
iter_constify(struct rbh_mut_iterator *iterator)
{
    struct constify_iterator *constify;

    constify = malloc(sizeof(*constify));
    if (constify == NULL)
        error(EXIT_FAILURE, errno, "malloc");

    constify->iterator = CONSTIFY_ITERATOR;
    constify->subiter = iterator;
    constify->element = NULL;
    return &constify->iterator;
}

static void
gc(void)
{
    const struct rbh_filter_options OPTIONS = {
        .projection = {
            .fsentry_mask = RBH_FP_ID,
        },
    };
    struct rbh_mut_iterator *fsentries;
    struct rbh_iterator *constify;
    struct rbh_iterator *deletes;

    /* Set the backend in a "garbage collection" mode */
    if (rbh_backend_set_option(backend, RBH_GBO_GC, (bool[]){true},
                               sizeof(bool)))
        error(EXIT_FAILURE, errno, "rbh_backend_set_option");

    fsentries = rbh_backend_filter(backend, NULL, &OPTIONS);
    if (fsentries == NULL)
        error(EXIT_FAILURE, errno, "rbh_backend_filter");

    constify = iter_constify(fsentries);
    deletes = iter_fsentry2delete(constify);

    if (rbh_backend_update(backend, deletes) == -1)
        error(EXIT_FAILURE, errno, "rbh_backend_update");

    rbh_iter_destroy(deletes);
}

/*----------------------------------------------------------------------------*
 |                                    cli                                     |
 *----------------------------------------------------------------------------*/

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
    mount_fd = open(argv[1], O_RDONLY | O_NOFOLLOW | O_PATH);
    if (mount_fd < 0)
        error(EXIT_FAILURE, errno, "open: %s", argv[1]);

    gc();
    return EXIT_SUCCESS;
}
