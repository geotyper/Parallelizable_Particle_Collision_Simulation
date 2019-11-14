#!/bin/bash
n=$1
src=$2
mpicc ${src}.c -o ${src}
salloc -n ${n} mpirun -np ${n} ${src} < inputs_20.txt > tmp.txt