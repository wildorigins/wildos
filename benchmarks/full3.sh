#!/bin/bash
# Final pass: the whole suite, three runs each, under its own result name.
systemctl stop sdwdate 2>/dev/null
cd /var/tmp/bench-out
RESNAME=final-$(date +%H%M) TIMES=3 bash /var/tmp/bench.sh pts
cp pts.log pts-final.log
bash /var/tmp/ptsres.sh > pts-final.txt
awk '/^    (pts|system)\/[a-z0-9-]+-[0-9.]+ *(\[|$)/{t=$1} /Deviation:/{gsub(/^ +/,""); print t, $0}' pts.log > pts-final-dev.txt
bash /var/tmp/ssl.sh > ssl-final.txt 2>&1
bash /var/tmp/bench.sh fio_run > /dev/null 2>&1; cp fio.txt fio-final.txt
systemctl start sdwdate 2>/dev/null
echo "== $(uname -r) final"; cat pts-final.txt; echo "-- deviations"; cat pts-final-dev.txt; echo "-- openssl"; cat ssl-final.txt; echo "-- fio"; cat fio-final.txt
