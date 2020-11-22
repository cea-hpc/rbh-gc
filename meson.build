# This file is part of rbh-gc
# Copyright (C) 2020 Commissariat a l'energie atomique et aux energies
#                    alternatives
#
# SPDX-License-Identifer: LGPL-3.0-or-later

project(
    'rbh-gc',
    'c',
    version: '0.0.0',
    license: 'LGPL3.0-or-later',
    default_options: [
        'warning_level=2',
        'werror=true',
    ],
)

# GNU extensions
add_project_arguments(['-D_GNU_SOURCE',], language: 'c')

librobinhood = dependency('robinhood', version: '>=0.0.0')

executable(
    'rbh-gc',
    sources: [
        'rbh-gc.c',
    ],
    dependencies: [librobinhood],
    install: true,
)
