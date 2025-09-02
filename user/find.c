#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *filename);

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf (2, "需要目录和文件名参数。");
        exit(1);
    }

    find(argv[1], argv[2]);
    exit(0);
}

/*
1. 打开指定目录
2. 搜索目录
    2.1 遍历
    2.2 名匹配
3. 子目录递归进行搜索
*/
void find(char *path, char *filename) {
    // 参数：fd表示打开的目录，de用于存储读取的信息，st用于获取文件类型
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    // 错误检查
    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "无法打开目录 %s\n", path);
        return;
    }

    if ((fstat(fd, &st)) < 0) {
        fprintf(2, "无法获取目录 %s 的状态\n", path);
        close(fd);
        return;
    }

    if (st.type != T_DIR) {
        fprintf(2, "%s 不是一个目录\n", path);
        close(fd);
        return;
    }

    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        fprintf(2, "路径太长\n");
        close(fd);
        return;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';

    // 遍历当前目录
    while (read(fd, &de, sizeof(de))) {
        // 跳过无效的目录项，只处理实际存在的文件和目录
        if (de.inum == 0) continue;

        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;

        if (stat(buf, &st) < 0) {
            fprintf(2, "无法获取文件 %s 的状态\n", buf);
            continue;
        }

        // 跳过当前目录和父目录
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;

        // 文件名匹配
        if (strcmp(de.name, filename) == 0) {
            printf("%s\n", buf);
        }

        // 递归搜索子目录
        if (st.type == T_DIR) {
            find(buf, filename);
        }
    }

    close(fd);
}