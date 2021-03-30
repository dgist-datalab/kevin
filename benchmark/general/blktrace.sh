#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi
#usage: ./blktrace <device> <output_file>

blktrace $1 -a complete -o - | blkparse -f "%T.%t %d %S %N\n" -i - >> $2 &
