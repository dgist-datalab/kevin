#!/bin/bash

WAREHOUSE_NR=50

runuser -l $tpcc_owner -c "cd ${tpcc_dir}; \time -v ./tpcc_start -h127.0.0.1 -d tpcc -u tpcc -p datalab -w $WAREHOUSE_NR -c100 -l300"
#\time -v ./tpcc_start -h127.0.0.1 -d tpcc -u tpcc -p "datalab" -w $WAREHOUSE_NR -c100 -l10800
