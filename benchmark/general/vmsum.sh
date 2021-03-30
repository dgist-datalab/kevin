#!/bin/bash

mkdir -p vmsum
ls vmstat/ | sort | while read workload; do
  #LINES=$(cat vmstat/$workload | awk '{print $16, $17}' | grep '^[0-9]' | awk '{print $1+$2}' | grep -v '^100' | wc -l)
  #echo "("$(cat vmstat/$workload | awk '{print $16, $17}' | grep '^[0-9]' | awk '{print $1+$2}' | grep -v '^100' | tr '\n' '+')0") / $LINES" | bc > vmsum/$workload
  #LINES=$(cat vmstat/$workload | awk '{print $15, $17, $18}' | grep -v '^0' | awk '{print $2+$3}' | grep -v '^0' | wc -l)
  #echo "("$(cat vmstat/$workload | awk '{print $15, $17, $18}' | grep -v '^0' | awk '{print $2+$3}' | grep -v '^0' | tr '\n' '+')0") / $LINES" | bc > vmsum/$workload
  LINES=$(cat vmstat/$workload | awk '{print $15, $18}' | grep -v '^0' | awk '{print $2 + 0}' | grep -v '^0' | wc -l)
  echo "("$(cat vmstat/$workload | awk '{print $15, $18}' | grep -v '^0' | awk '{print $2 + 0}' | grep -v '^0' | tr '\n' '+')0") / $LINES" | bc > vmsum/$workload
done

grep . vmsum/* | tr ':' '\t' | sed \
  -e "s@vmsum/@@g" \
  -e "s/micro_copyfiles.f/cp()/g" \
  -e "s/micro_createfiles_fsync.f/creat()-4K-FSYNC/g" \
  -e "s/micro_createfiles.f/creat()-4K/g" \
  -e "s/micro_createfiles_empty.f/creat()/g" \
  -e "s/micro_delete.f/unlink()-4K/g" \
  -e "s/micro_delete_empty.f/unlink()/g" \
  -e "s/micro_makedirs.f/mkdir()/g" \
  -e "s/micro_removedirs.f/rmdir()/g" \
  -e "s/micro_listdirs_empty.f/readdir()-C/g" \
  -e "s/micro_listdirs.f/readdir()-W/g" \
  -e "s/real_fileserver.f/Fileserver/g" \
  -e "s/real_varmail.f/Varmail/g" \
  -e "s/real_webproxy.f/Webproxy/g" \
  -e "s/real_webserver.f/Webserver/g" \
  -e "s/real_oltp.f/Oltp/g" \
