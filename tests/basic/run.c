#include <sys/syscall.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>

#include <fcntl.h>

#define SYS_AEP_INIT 463
#define SYS_AEP_EXIT 464
#define SYS_AEP_JOIN 465
#define SYS_PV_BRK 466


int call_loader(char *write_string, int choose)
{
    char *argv[] = {"../../loader/pv_loader", "fdlhelper"};
    char *envp[] = {NULL};

    if (execve("../../loader/pv_loader", argv, envp) == -1) {
        perror("execve failed");
        return -1;
    }
	
    return 0; // 不会执行到这里
}

int run_the_test()
{
    char *program_path = "./test";
    char *args[] = {program_path, NULL};
    char *env[] = { NULL };

    if (execve(program_path, args, env) == -1) {
        perror("execve"); 
        exit(EXIT_FAILURE); 
    }

    return 0; 
}


int main(int argc, char *argv[])
{
	char test_string1[64] = "Hello world, this is app1\n";
	FILE *test_fp;
	pid_t child_pid;
	int ret = 0;
	int status;
	int aep_template_id1;

	
	//init template
	aep_template_id1 = syscall(SYS_AEP_INIT);
	if (aep_template_id1 == -1)
		return -1;

	child_pid = syscall(SYS_AEP_JOIN, aep_template_id1);
	if (!child_pid) {//child1
		call_loader(test_string1, 1); //load the pv lib
		return 0;
	}
	wait(&status);
	
	//syscall(SYS_AEP_EXIT, aep_template_id1);

	return 0;
}
