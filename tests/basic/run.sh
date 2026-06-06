#!/bin/bash

rm ./fdlhelper
rm ./run

/mnt/musl-for-aep/obj/musl-gcc -g -nostartfiles -static -o fdlhelper fdlhelper.c -Wl,-T,/mnt/custom.ld -e my_entry_point -mcmodel=medium -Wl,--no-relax
gcc -o run run.c

./run

