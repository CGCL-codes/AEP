#include "fdlhelper.h"

#define STR_AND_SIZE(s) s, sizeof(s) - 1

#define USE_DIRECTFS_MUSL 0

int a = 0;

static unsigned long hexstrtoul(const char *s)
{
    unsigned long res = 0;
    char c;
    while ((c = *s++)) {
        if (c >= '0' && c <= '9') {
            c -= '0';
        } else {
            c &= ~0x20;
            c -= 'A' - 0xa;
        }
        res = res << 4 | c;
    }
    return res;
}

int add(int x, int y)
{
    return x + y;
}

int foobar(int x)
{
    int b = add(x, a);
    a++;

    return b;
}

#define FILENAME "/root/output.txt"
#define BUFFER_SIZE 256

void test_read_write() 
{
    int fd = open(FILENAME, O_CREAT | O_WRONLY | O_TRUNC);
    if (fd == -1) {
        printf("open file failed\n");
        perror("打开文件失败");
        return;
    }
    printf("open fd: %d\n", fd);

    const char *data = "Hello, this is a test for the read and write functions.\n";
    ssize_t bytes_written = write(fd, data, strlen(data));
    if (bytes_written == -1) {
        perror("写入文件失败");
        close(fd);
        return;
    }
    printf("写入数据：%s\n", data);

    // 关闭文件描述符
    close(fd);

    // 重新打开文件以读取
    fd = open(FILENAME, O_RDONLY);
    if (fd == -1) {
        perror("打开文件失败");
        return;
    }

    printf("读取文件内容：\n");
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    // 循环读取文件内容
    while ((bytes_read = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0'; // 确保缓冲区以 NULL 结尾
        printf("%s", buffer);
    }

    if (bytes_read == -1) {
        perror("读取文件失败");
    }

    // 关闭文件描述符
    close(fd);
}

int shared_memory[2];

#if USE_DIRECTFS_MUSL

void test_lock()
{
    int status = -1;
    // 初始化锁和共享资源
    volatile int *lock = shared_memory;
    int *shared_counter = shared_memory + 1;
    *lock = 0; // 锁初始化为未加锁
    *shared_counter = 0; // 计数器初始化为 0

    int NUM_PROCESSES = 4, NUM_ITERATIONS = 2;
    printf("Starting test with %d processes and %d iterations per process...\n", NUM_PROCESSES, NUM_ITERATIONS);

    for (int i = 0; i < NUM_PROCESSES; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程逻辑
            for (int j = 0; j < NUM_ITERATIONS; ++j) {
                my_lock(lock); // 加锁

                // 临界区，修改共享计数器
                int temp = *shared_counter;
                usleep(rand() % 1000); // 模拟临界区操作耗时
                *shared_counter = temp + 1;
                printf("Process %d: shared counter = %d\n", getpid(), *shared_counter);

                my_unlock(lock); // 解锁
            }

            printf("Process %d finished\n", getpid());
            exit(0); // 子进程结束
        } else if (pid < 0) {
            printf("Failed to create child process\n");
            exit(1);
        }
    }

    // 等待所有子进程结束
    for (int i = 0; i < NUM_PROCESSES; ++i) {
        wait(&status);
    }

    // 验证结果
    if (*shared_counter == NUM_PROCESSES * NUM_ITERATIONS) {
        printf("Test passed! Shared counter = %d\n", *shared_counter);
    } else {
        printf("Test failed! Shared counter = %d (expected %d)\n",
               *shared_counter, NUM_PROCESSES * NUM_ITERATIONS);
    }
}
#endif


#define NUM_OPEN  1
#define NUM_CLOSE 2
#define NUM_READ  3
#define NUM_PV_READ  4
#define NUM_WRITE 5
#define NUM_PV_WRITE 6
#define NUM_SEEK  7
#define NUM_PV_SEEK  8
#define NUM_PV_COUNT  9
#define NUM_PV_MALLOC  10
#define NUM_PV_FREE  11
#define NUM_PV_MALLOC_INIT  12
#define NUM_PV_PRINT_BITMAP  13
#define NUM_PV_KV_GET  14
#define NUM_PV_KV_PUT  15

unsigned long pv_file_operations(int call_number, ...) {
    va_list args;
    va_start(args, call_number);

    unsigned long result = -1;

    switch (call_number) {
#if USE_DIRECTFS_MUSL
        case NUM_OPEN: {
            // get args
            const char *path = va_arg(args, const char *);
            int flags = va_arg(args, int);
            mode_t mode = va_arg(args, mode_t);  // 可选模式
            result = pv_open(path, flags, mode);
            if (result == -1) {
                perror("open failed");
            }
            break;
        }
        case NUM_CLOSE: {
            int fd = va_arg(args, int);
            int group_fd = va_arg(args, int);
            result = pv_close(fd, group_fd);
            if (result == -1) {
                perror("close failed");
            }
            break;
        }
        case NUM_READ: {
            int fd = va_arg(args, int);
            void *buf = va_arg(args, void *);
            size_t count = va_arg(args, size_t);
            result = read(fd, buf, count);
            if (result == -1) {
                perror("read failed");
            }
            break;
        }
	    case NUM_PV_READ: {
            int fd = va_arg(args, int);
            int group_fd = va_arg(args, int);
            void *buf = va_arg(args, void *);
            off_t start = va_arg(args, size_t);
            size_t count = va_arg(args, size_t);
            result = pv_read(fd, group_fd, buf, start, count);
            if (result == -1) {
                perror("read failed");
            }
            break;
        }
        case NUM_WRITE: {
            int fd = va_arg(args, int);
            const void *buf = va_arg(args, const void *);
            size_t count = va_arg(args, size_t);
            result = write(fd, buf, count);
            if (result == -1) {
                perror("write failed");
            }
            break;
        }
        case NUM_PV_WRITE: {
            int fd = va_arg(args, int);
            int group_fd = va_arg(args, int);
            const void *buf = va_arg(args, const void *);
            off_t start = va_arg(args, size_t);
            size_t count = va_arg(args, size_t);
            int append = va_arg(args, int);
            unsigned long struct_file_addr = va_arg(args, unsigned long);
            result = pv_write(fd, group_fd, buf, start, count, append, struct_file_addr);
            if (result == -1) {
                perror("write failed");
            }
            break;
        }
        case NUM_SEEK: {
            int fd = va_arg(args, int);
            off_t offset = va_arg(args, off_t);
            int whence = va_arg(args, int);
            result = lseek(fd, offset, whence);
            if (result == -1) {
                perror("lseek failed");
            }
            break;
        }
        case NUM_PV_SEEK: {
            int fd = va_arg(args, int);
            int group_fd = va_arg(args, int);
            off_t offset = va_arg(args, off_t);
            int whence = va_arg(args, int);
            result = pv_lseek(fd, group_fd, offset, whence);
            if (result == -1) {
                perror("lseek failed");
            }
            break;
        }
#endif
        case NUM_PV_MALLOC: {
            size_t size = va_arg(args, size_t);
            int thread_num = va_arg(args, int);
            return (unsigned long)pv_malloc(size, thread_num);
            break;
        }
        case NUM_PV_FREE: {
            void *p = va_arg(args, void *);
            int thread_num = va_arg(args, int);
            pv_free(p, thread_num);
            return 0;
            break;
        }
        case NUM_PV_MALLOC_INIT: {
            init_pv_allocator();
            return 0;
            break;
        }
        case NUM_PV_PRINT_BITMAP: {
            print_pool_bitmap();
            return 0;
            break;
        }
        default:
            fprintf(stderr, "Invalid call number: %d\n", call_number);
            break;
    }

    va_end(args);
    return result;
}

int main(int argc, char *argv[])
{
    if (argc == 2) {
        void **func = (void**)hexstrtoul(argv[1]);
        func[0] = foobar;
        foobar(100);
        func[1] = test_read_write;

        printf("pv_file_operations: 0x%lx\n", (unsigned long)pv_file_operations);

        printf("finish pv main.\n");
    } else {
        write(1, STR_AND_SIZE("Success\n"));
    }

    return 0;
}

void my_entry_point(int argc, char *argv[]) {
    main(argc, argv); //enter pv lib
}
