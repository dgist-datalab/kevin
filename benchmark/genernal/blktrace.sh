#!/bin/bash
#usage: ./blktrace <device> <output_file>

blktrace $1 -a complete -o - | blkparse -f "%T.%t %d %S %N\n" -i - >> $2 &
