#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi
#usage: ./dumpfs <device>

sudo dumpe2fs $1
