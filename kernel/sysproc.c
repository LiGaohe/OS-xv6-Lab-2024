#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  backtrace();
  
  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// sigalarm函数，用于中断处理程序的注册
// ticks: 时钟中断间隔
// handler: 用户空间的函数入口地址
uint64
sys_sigalarm(void) 
{
  int ticks;
  uint64 handler;
  // 获取参数
  argint(0, &ticks);
  argaddr(1, &handler);
  // 设置当前进程的alarm相关字段
  struct proc *p = myproc();  // 获取当前进程
	  p->alarm_interval = ticks;  // 设置间隔
	  p->alarm_handler = (void(*)())handler; // 设置处理函数 
	  p->alarm_ticks_left = ticks;  // 重置计数器
	  p->alarm_running = 0;  // 还没运行handler
	  return 0;
}

// sys_sigreturn函数，用于从信号处理程序返回
// 恢复用户态寄存器，并返回到被中断的系统调用处
uint64
sys_sigreturn(void) 
{
  struct proc *p = myproc();
  memmove(p->trapframe, p->alarm_trapframe, sizeof(struct trapframe));
  p->alarm_running = 0;
 
  return p->trapframe->a0;   // 保证系统调用返回值不破坏 a0
}
  