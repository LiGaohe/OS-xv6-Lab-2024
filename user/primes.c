#include "kernel/types.h"
#include "user/user.h"

// 添加 noreturn 属性
void sieve(int pfd[2]) __attribute__((noreturn));

void sieve(int pfd[2]) {
    close(pfd[1]); // 只读
    int prime;
    if (read(pfd[0], &prime, sizeof(prime)) == 0) {
        // 没有数据，结束
        close(pfd[0]);
        exit(0);
    }
    printf("prime %d\n", prime);

    int next_pfd[2];
    pipe(next_pfd);

    int pid = fork();
    if (pid == 0) {
        // 子进程递归处理
        close(pfd[0]);      // 子进程不再需要父 pipe
        sieve(next_pfd);
    } else {
        // 父进程：过滤掉 prime 的倍数，传递给下一个 pipe
        close(next_pfd[0]); // 父进程只写
        int n;
        while (read(pfd[0], &n, sizeof(n)) > 0) {
            if (n % prime != 0) {
                write(next_pfd[1], &n, sizeof(n));
            }
        }
        close(pfd[0]);
        close(next_pfd[1]); // 写完要关闭，通知下游
        wait(0);            // 等待子进程
        exit(0);
    }
}

int main(void) {
    int pfd[2];
    pipe(pfd);

    int pid = fork();
    if (pid == 0) {
        // 子进程递归筛选
        sieve(pfd);
    } else {
        // 父进程：写入2~280
        close(pfd[0]);
        for (int i = 2; i <= 280; i++) {
            write(pfd[1], &i, sizeof(i));
        }
        close(pfd[1]); // 写完要关闭，通知下游
        wait(0);       // 等待所有子进程结束
    }
    exit(0);
}