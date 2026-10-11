/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared by the application and assembly startup. Explicit build overrides win. */
#ifndef MELODEE_BUILD_OPTIONS_H
#define MELODEE_BUILD_OPTIONS_H
#ifndef MELODEE_DUAL_CORE
#define MELODEE_DUAL_CORE 1     /* paired FM6/Prophet kernels; serial fallback if CPU1 is unavailable */
#endif
#endif
