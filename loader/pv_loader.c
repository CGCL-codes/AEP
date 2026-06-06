#include "z_utils.h"
#include "z_syscalls.h"
#include "foreign_dlopen.h"

int main(int argc, char *argv[])
{
	if (argc != 2){
		z_printf("pv loader should pass one parameter!\n");
		z_exit(0);
	}
	
	z_printf("pv loader prepare to load lib at: %s\n", argv[1]);
	
	init_foreign_pv_function(argv[1]);

	z_printf("pv loader can call AEP pv lib foobar function:\n");

	void *p = foobar;
	z_printf("foobar: 0x%lx\n", (unsigned long)p);
	int a = foobar(1);
	z_printf("a: %d\n", a);
	a = foobar(2);
	z_printf("a: %d\n", a);
	a = foobar(2);
	z_printf("a: %d\n", a);

	// test_read_write();
	z_printf("finish foreign_dlopen_demo\n");

	z_exit(0);
}
