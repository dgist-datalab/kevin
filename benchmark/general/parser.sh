#!/bin/bash

#set -eo pipefail

TMP=/tmp/parser.$(uuidgen)
TMP2=/tmp/parser.$(uuidgen)

cat > $TMP

if grep -q MAPPINGR $TMP; then
  mkdir dev_count
  DEST=dev_count
else
  mkdir fs_count
  DEST=fs_count
fi

cat $TMP | grep 'MAPPINGR\|MDR' | awk '{print $1}' | while read title; do
  (
    grep -A6 "^$title"$'\t' $TMP | tail -n +2 | \
      sed -e 's@EXT4-META@EXT4-M@g' -e 's@EXT4-DATA@EXT4-D@g' | tr '\t' ' ' | tr '\r' ' ' | \
        while read fs a b c d e f g; do
          if [[ "$a" -lt 0 ]]; then a=0; fi
          if [[ "$b" -lt 0 ]]; then b=0; fi
          if [[ "$c" -lt 0 ]]; then c=0; fi
          if [[ "$d" -lt 0 ]]; then d=0; fi
          if [[ "$e" -lt 0 ]]; then e=0; fi
          if [[ "$f" -lt 0 ]]; then f=0; fi
          echo '"'$fs'"' $a $b $c $d $e $f
        done | tee > $TMP2

    grep '^"EXT4-M"' $TMP2
    grep '^"EXT4-D"' $TMP2
    grep '^"XFS"' $TMP2
    grep '^"BTRFS"' $TMP2
    grep '^"F2FS"' $TMP2
    grep '^"LIGHTFS"' $TMP2
  ) > $DEST/$title.dat
done

rm $TMP $TMP2
