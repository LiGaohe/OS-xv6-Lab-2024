#include "kernel/types.h"
#include "user/user.h"

int main() {
    int parent[2];
    int child[2];
    pipe(parent);
    pipe(child);

    int pid = fork();
    // 如果创建的子进程进程id小于0，说明创建失败
    if (pid < 0) {
        fprintf(2, "fork failed\n");
        exit(1);
    }

    // 等于0，说明是子进程
    if (pid == 0) {
        char buf;
        // 确保管道单向
        close(parent[1]);
        close(child[0]);

        read(parent[0], &buf, 1);
        printf("%d: received ping\n", getpid());
        write(child[1], "b", 1);
        // 使用完毕后关闭管道
        close(parent[0]);
        close(child[1]);
        exit(0);
    } else {
        char buf;
        close(parent[0]);
        close(child[1]);
        write(parent[1], "a", 1);
        read(child[0], &buf, 1);
        printf("%d: received pong\n", getpid());
        close(parent[1]);
        close(child[0]);
        exit(0);
    }
}