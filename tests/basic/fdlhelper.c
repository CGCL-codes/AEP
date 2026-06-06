#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/syscall.h>

#define STR_AND_SIZE(s) s, sizeof(s) - 1
#define TOTAL_SIZE (8ULL * 1024)

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

#define SYS_AEP_UNMAP_SHADOW_ENTRY 467

int main(int argc, char *argv[])
{
	printf("enter AEP pv lib.\n");
    if (argc == 2) {
        void **func = (void**)hexstrtoul(argv[1]);
        func[0] = foobar;
        foobar(100);

        printf("foobar function address: 0x%lx\n", (unsigned long)foobar);
        printf("MAP_USE_PV: %lx\n", MAP_USE_PV);
        
        printf("emulate unmap shasow entry\n");
        
        syscall(SYS_AEP_UNMAP_SHADOW_ENTRY, 1);
        
        printf("unmap shasow entry success!\n");
        
        char *map = mmap(NULL, TOTAL_SIZE, PROT_NONE, MAP_PRIVATE|MAP_ANON|MAP_USE_PV, -1, 0);
        
        printf("8GB memory alloc address: %lx\n", map);
        
        size_t total_pages = TOTAL_SIZE / 4096;
        for (size_t i = 0; i < total_pages; i++) {
        	map[i * 4096] = (char)(i % 256);
    	}
    	printf("8GB memory access success\n");

        printf("exit AEP pv lib.\n");
    } else {
        write(1, STR_AND_SIZE("Success\n"));
    }

    return 0;
}

__attribute__((section(".text.my_entry_point")))
void my_entry_point(int argc, char *argv[]) {
	printf("enter shadow entry\n");
    main(argc, argv); //enter pv lib
    printf("exit shadow entry\n");
}
