#!/bin/bash

set -eo pipefail

TMP=/tmp/$(uuidgen)
TMP2=/tmp/$(uuidgen)
TARGET=/tmp/$(uuidgen)

mkdir $TMP $TMP2

TARGET=$(realpath "$1")
FILESIZE=$(stat -c%s "$TARGET")

cd $TMP
split -C $(($FILESIZE / 12)) "$TARGET"
# ls -al x*
while read f; do grep '^[0-9]' $f | awk '{print $2, $3/8, $4/4096}' > $TMP2/$f & done < <(ls)
wait
cd - > /dev/null

xa=0
xb=0
xc=0
xd=0
xe=0
xf=0
xg=0
xh=0
xi=0
xj=0
xk=0
xl=0
xm=0
xn=0
xo=0
xp=0

while read a b c d e f g h i j k l m n o p; do
  xa=$(($xa + $a))
  xb=$(($xb + $b))
  xc=$(($xc + $c))
  xd=$(($xd + $d))
  xe=$(($xe + $e))
  xf=$(($xf + $f))
  xg=$(($xg + $g))
  xh=$(($xh + $h))
  xi=$(($xi + $i))
  xj=$(($xj + $j))
  xk=$(($xk + $k))
  xl=$(($xl + $l))
  xm=$(($xm + $m))
  xn=$(($xn + $n))
  xo=$(($xo + $o))
  xp=$(($xp + $p))
done < <(ls -d $TMP2/* | parallel ./02_trace_map.py)

printf "R_SB\tR_GD\tR_BB\tR_IB\tR_IT\tR_DE\tR_DA\tW_SB\tW_GD\tW_BB\tW_IB\tW_IT\tW_DE\tW_DA\tW_DJ\tDEL\n"
printf "$xa\t$xb\t$xc\t$xd\t$xe\t$xf\t$xg\t$xh\t$xi\t$xj\t$xk\t$xl\t$xm\t$xn\t$xo\t$xp\n"

rm -rf $TMP $TMP2
