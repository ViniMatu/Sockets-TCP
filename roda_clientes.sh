#!/bin/bash
SERVER=192.168.1.101
REP=5
for n in $(seq 1 10); do
  for r in $(seq 1 $REP); do
    echo ">> N=$n  repeticao=$r"
    for i in $(seq 1 $n); do
      ( until ./client $SERVER 5000 34 65536 2>>log_clientes.txt; do sleep 0.5; done ) &
    done
    wait
    sleep 2
  done
done
echo "FIM"
