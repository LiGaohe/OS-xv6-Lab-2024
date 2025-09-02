#!/bin/bash
cd /home/elaina/xv6-labs-2024
gdb-multiarch -quiet -ex "set architecture riscv:rv64" -ex "target remote 127.0.0.1:26000" -ex "symbol-file kernel/kernel" -ex "set disassemble-next-line auto" -ex "set riscv use-compressed-breakpoints yes"
