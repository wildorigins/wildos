#!/bin/bash
# AES-256-GCM throughput, 4 processes, 16 KB blocks: PTS's own OpenSSL 4.1
# build and the system's.
P=/var/lib/phoronix-test-suite/installed-tests/pts/openssl-4.1.1/openssl-openssl-4.1.0
gb() { awk '/^AES-256-GCM/{printf "%.2f GB/s\n", $NF/1e6}'; }
echo "pts/openssl $(cd $P && LD_LIBRARY_PATH=. ./apps/openssl speed -multi 4 -seconds 10 -evp aes-256-gcm 2>/dev/null | gb)"
echo "system/openssl $(openssl speed -multi 4 -seconds 10 -evp aes-256-gcm 2>/dev/null | gb) ($(openssl version | cut -d' ' -f2))"
