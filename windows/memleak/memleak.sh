#!/bin/bash

gdb -x $(dirname $0)/memleak.gdb $1

echo "Porcessing gdb output..."

declare -A ADDRESSES

while read ADDRESS OPERATION ID; do
  if [[ "$OPERATION" == "alloc" ]]; then
    if [[ -n "${ADDRESSES[$ADDRESS]}" ]]; then
      echo "Wrong input: $ADDRESS $OPERATION $ID already allocated"
      continue
    fi
    ADDRESSES[$ADDRESS]=$ID
  elif [[ "$OPERATION" == "free" ]]; then
    unset ADDRESSES[$ADDRESS]
  else
    echo "Wrong operation: $ADDRESS $OPERATION $ID"
  fi
done < <(grep '^0x' memleak.out)

echo "Memory leaks at allocations:"
echo "${ADDRESSES[@]}"
