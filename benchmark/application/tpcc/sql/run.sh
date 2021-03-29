#!/bin/bash

WAREHOUSE_NR=50

runuser -l ssjy806 -c "cd /home/ssjy806/tpcc; \time -v ./tpcc_start -h127.0.0.1 -d tpcc -u tpcc -p datalab -w $WAREHOUSE_NR -c100 -l300"
#\time -v ./tpcc_start -h127.0.0.1 -d tpcc -u tpcc -p "datalab" -w $WAREHOUSE_NR -c100 -l10800
