#!/bin/bash
REP=5
OUT=resultados_rede.csv
echo "clientes,repeticao,mbps,status" > $OUT
for n in $(seq 1 10); do
  for r in $(seq 1 $REP); do
    echo ">> N=$n  repeticao=$r"
    res=$(./server 5000 $n 2 30 65536 2>>log_detalhado.txt)
    code=$?
    echo "$n,$r,${res#*,},$code" >> $OUT
  done
done
echo "FIM"
