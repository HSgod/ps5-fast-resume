#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
. ./env.sh
: "${PS5_HOST:?set PS5_HOST in env.local.sh}"
elf="$1"
[ -f "$elf" ] || elf="payloads/$1/$1.elf"
make -s -C "$(dirname "$elf")"
exec "$PS5_PAYLOAD_SDK/bin/prospero-deploy" -h "$PS5_HOST" -p "$PS5_PORT" "$elf"
