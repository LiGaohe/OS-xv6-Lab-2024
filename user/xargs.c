#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[]) {
  char buf[512];
  char *cmd[MAXARG];
  int i;

  // 复制命令参数
  // i从1开始，因argv[0]为程序本身名字
  // i小于MAXARG -1, 因exec的最后一个元素必须为NULL，以便识别参数列表的结束
  for(i = 1; i < argc && i < MAXARG-1; i++){
    cmd[i-1] = argv[i];
  }
  int base = i-1; // 基础参数个数

  // 逐行读取标准输入
  int n = 0;
  while(1){
    n = 0;
    char c;
    int got = 0;
    // 读取一行
    while(read(0, &c, 1) == 1){
      got = 1;
      if (c == '\n') break;

      if(n < sizeof(buf)-1)
        buf[n++] = c;
    }
    if(n == 0 && !got) // EOF
      break;
    buf[n] = 0;

    // 构造参数
    int argi = base;
    int j = 0;
    while(j < n){
      // 跳过前导空格
      while(buf[j] == ' ' || buf[j] == '\t') j++;
      if(j >= n) break;
      cmd[argi++] = &buf[j];
      // 找到下一个空格或结尾
      while(j < n && buf[j] != ' ' && buf[j] != '\t') j++;
      buf[j++] = 0;
      if(argi >= MAXARG-1) break;
    }
    cmd[argi] = 0;

    // fork+exec
    if(fork() == 0){
      // 如果是子进程，执行命令
      exec(cmd[0], cmd);
      // 如果exec失败
      fprintf(2, "exec failed\n");
      exit(1);
    }
    wait(0);

    if(c != '\n') // EOF
      break;
  }

  exit(0);
}