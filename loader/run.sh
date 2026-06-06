make clean

make STD_LIB=0 DEBUG=1

cp ./fdlhelper /root/fdlhelper

gcc /mnt/malloc-test/malloc-thread-test.c -o /mnt/malloc-test/malloc-thread-test -Wl,--no-relax -lpthread -lrt
# gcc /mnt/malloc-test/malloc-test.c -o /mnt/malloc-test/malloc-test -lpthread
# gcc /mnt/quill-for-directfs/test/test_write.c -o /mnt/quill-for-directfs/test/helloworld
# gcc /mnt/quill-for-directfs/test/helloworld.c -o /mnt/quill-for-directfs/test/helloworld
# gcc /mnt/directfs-test/filebench-test/filebench.c -o /mnt/directfs-test/filebench-test/filebench
gcc p3.c -o p3

ls -all /root

sleep 5

./p3

