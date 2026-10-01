#!/bin/sh
# boot_test.sh: drive the UEFI shell over the serial console, run our loader,
# and pass only if KAM output appears. No expect(1) needed.
# Usage: boot_test.sh LOGFILE EFI_PATH SECONDS -- qemu-system-... [args...]
# EFI_PATH uses UEFI backslashes, e.g. '\EFI\BOOT\BOOTX64.EFI'.
set -u
log=$1; efi=$2; secs=$3; shift 3
[ "$1" = "--" ] && shift
# Firmware init + 5s shell countdown take ~10s; then select FS0 and run EFI.
( sleep 12; printf 'FS0:\r'; sleep 2; printf '%s\r' "$efi"; sleep 6 ) \
  | timeout "$secs" "$@" > "$log" 2>&1
if grep -a -q "KAM" "$log"; then
  echo "PASS: KAM output found in $log"
  exit 0
else
  echo "FAIL: no KAM output in $log"
  tr -d '\0' < "$log" | tail -n 12
  exit 1
fi
