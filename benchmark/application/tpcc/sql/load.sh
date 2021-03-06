#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi

WAREHOUSE_NR=50


sudo mysql -uroot -e "drop database tpcc"
sudo mysql -uroot -e "create database tpcc"
sudo mysql -uroot tpcc < create_table.sql
sudo mysql -uroot -e "GRANT ALL PRIVILEGES ON tpcc.* TO 'tpcc'@'localhost' IDENTIFIED BY 'datalab';"
sudo mysql -uroot -e "set global innodb_flush_log_at_trx_commit = 0; set global sync_binlog = 0;"

runuser -l $tpcc_owner -c "cd ${tpcc_dir}; \time -v ./tpcc_load -h127.0.0.1 -d tpcc -u tpcc -p datalab -w $WAREHOUSE_NR"
#\time -v ./tpcc_load -h127.0.0.1 -d tpcc -u tpcc -p "datalab" -w $WAREHOUSE_NR

sudo mysql -uroot -e "set global innodb_flush_log_at_trx_commit = 1; set global sync_binlog = 1;"
sudo mysql -uroot tpcc < add_fkey_idx.sql
