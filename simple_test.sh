#!/bin/bash
# Simple compile test
echo "Testing basic compilation..."
cd /home/elaina/xv6-labs-2024
make clean >/dev/null 2>&1
echo "Compiling with LAB=mmap..."
make LAB=mmap 2>&1 | head -50
