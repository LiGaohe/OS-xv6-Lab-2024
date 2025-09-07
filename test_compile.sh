#!/bin/bash
echo "Testing xv6 compilation with mmap support..."
make clean
make CPUS=1 LAB=mmap 2>&1 | tee compile.log

if [ $? -eq 0 ]; then
    echo "Compilation successful!"
    echo "Running mmaptest..."
    make qemu CPUS=1 LAB=mmap &
    QEMU_PID=$!
    
    # Wait a bit for qemu to start
    sleep 5
    
    # Send commands to qemu
    echo "mmaptest" | nc -w 5 localhost 1234 || echo "Could not connect to qemu"
    
    # Kill qemu
    kill $QEMU_PID 2>/dev/null
else
    echo "Compilation failed. Check compile.log for errors."
fi
