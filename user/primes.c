#include "kernel/types.h"
#include "user/user.h"

// 添加 noreturn 属性
void sieve(int pfd[2]) __attribute__((noreturn));

void sieve(int pfd[2]) {
    // 当前进程从上一个管道读取数据进行筛选
    close(pfd[1]); // 关闭写端，只读
    int prime;
    if (read(pfd[0], &prime, sizeof(prime)) == 0) {
        // 没有数据，结束
        close(pfd[0]);
        exit(0);
    }

    // 输出读到的素数
    printf("prime %d\n", prime);

    // 创建下一个管道
    // 剩下的数将通过下一个管道传递给下一进程
    int next_pfd[2];
    pipe(next_pfd);

    int pid = fork();
    if (pid == 0) {
        // 子进程递归处理
        close(pfd[0]);
        sieve(next_pfd);
    } else {
        // 父进程：过滤掉prime的倍数，传递给下一个pipe
        close(next_pfd[0]);
        int n;
        while (read(pfd[0], &n, sizeof(n)) > 0) {
            // 将不是当前素数倍数的数写入下一管道，过滤掉当前素数的倍数
            if (n % prime != 0) {
                write(next_pfd[1], &n, sizeof(n));
            }
        }
        close(pfd[0]);
        close(next_pfd[1]);
        wait(0);
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
        close(pfd[1]);
        wait(0);
    }
    exit(0);
}