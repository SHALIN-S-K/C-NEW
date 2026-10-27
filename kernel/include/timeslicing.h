/**
 * Copyright (c) 2026 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 */


#ifndef ZEPHYR_INCLUDE_KERNEL_TIMESLICING_H
#define ZEPHYR_INCLUDE_KERNEL_TIMESLICING_H

void z_time_slice(void);
void z_time_slice_reset(struct k_thread *curr);
#ifndef CONFIG_SMP
void z_time_slice_switch(struct k_thread *new_thread);
#endif /* !CONFIG_SMP */

#endif /* ZEPHYR_INCLUDE_KERNEL_TIMESLICING_H */
