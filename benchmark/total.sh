#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi

cd fragtest
#../asdf.sh 2nd-fragtest-nofrag -f
#../frag_filebench.sh frag3.f frag3 -f
#../frag_filebench.sh frag2.f frag2 -f
../frag_filebench.sh frag1.f 2nd-frag1 -f
