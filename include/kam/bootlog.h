#ifndef KAM_BOOTLOG_H
#define KAM_BOOTLOG_H

/* In-memory boot log mirrored to ConOut (which reaches serial where the
 * firmware routes it there). Events are recorded during the
 * boot-services phase and flushed to KAM/BOOT.LOG before ExitBS (or
 * before returning to firmware on chain/probe paths).
 * Never logs secrets: the password gate logs only outcomes. */

#include "types.h"

#define KAM_LOG_SIZE 8192u

struct kam_system_table;
struct kam_boot_services;
struct kam_file_proto;

void kam_log_init(struct kam_system_table *st);
void kam_log_no_conout(void);
void kam_log(const char *s);
void kam_log_u64(kam_u64 v);
void kam_log_hex(kam_u64 v);

/* Write the buffer to KAM/BOOT.LOG on the given volume root. */
int kam_log_flush(struct kam_boot_services *bs, struct kam_file_proto *root);

#endif
