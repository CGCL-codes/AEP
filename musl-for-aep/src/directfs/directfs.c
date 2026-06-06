#include <directfs.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>

struct DirectNode* hashTable[TABLE_SIZE];
int __log_lock[1] = {0};
struct log_entry *log_entrys = (struct log_entry *)0xffffe88002106000;

int __alloc_page_lock[1] = {0};
struct user_template_metadata *user_template = (struct user_template_metadata *)0xffffe880000000e0;

unsigned long *groupfd_addr = (unsigned long *)0xffffe88000002000;
unsigned long *groupfd_size = (unsigned long *)0xffffe88000083000;
unsigned char *pv_bit_maps  = (unsigned char *)0xffffe88000104000;
unsigned char *ra_bit_maps  = (unsigned char *)0xffffe88001105000;

int hash(int group_fd) {
    return group_fd % TABLE_SIZE;
}

void open_direct_pre_handle(unsigned long _fd, int flags, int *fd, int *group_fd){																
	*fd = (int)(_fd & FD_MASK);															
	if (_fd >= MIN_DIRECT_FD){															
		*group_fd = (int)((_fd & G_FD_MASK) >> 32);	
        DEBUG_PRINT("musl lib: open_direct_pre_handle open direct file, _fd: %lx\n", (unsigned long)_fd);								
		get_DirectNode(*group_fd, flags);																				
	}else
        *group_fd = 0;
}

void del_DirectNode_from_hash(struct DirectNode* node) {
    int index = hash(node->group_fd);
    struct DirectNode* current = hashTable[index];
    struct DirectNode* pre = NULL;

    while (current != NULL) {
        if (current == node) {
            if (pre == NULL) 
                hashTable[index] = current->next;
            else 
                pre->next = current->next;
            
            //DEBUG_PRINT("musl lib: del_DirectNode_from_hash 1\n");
            return;
        }
        pre = current;
        current = current->next;
    }

    DEBUG_PRINT("musl lib: delete file node failed\n");
}

void _put_DirectNode(struct DirectNode *node){
    int count;
    if (!is_pv_addr(node)) return;

    count = ref_dec(node);
    if (node->count == 0){
        del_DirectNode_from_hash(node);
        //__syscall(SYS_DIRECTFS_FLUSH_LOGS);
        memset(node, 0, sizeof(struct DirectNode));
        free(node);
    }else if(node->count < 0){
        DEBUG_PRINT("musl lib ERROR: DirectNode: %lx double free!\n", (unsigned long)node);
    }
    
}

void put_DirectNode(int group_fd){
    if (group_fd <= 0)
        return;

    printf("musl lib: close, group_fd: %d\n", group_fd);

    struct DirectNode* node = find_file_node(group_fd);
    if (!is_pv_addr(node))
        return;
    
    _put_DirectNode(node);
}

struct DirectNode* get_DirectNode(int group_fd, int flags) {
    DEBUG_PRINT("musl lib: enter get_DirectNode, groupfd:%d, \n", group_fd);
    struct DirectNode* f_node = find_file_node(group_fd);

    if (f_node == NULL) {
        int index = hash(group_fd);
        f_node = (struct DirectNode*)malloc(sizeof(struct DirectNode));
        if (!is_pv_addr(f_node)){
            DEBUG_PRINT("musl lib: alloc groupfd failed\n");
            return NULL;
        }
        f_node->group_fd = group_fd;
        f_node->next = hashTable[index];
        f_node->lock = 0;
        f_node->count = 0;
        hashTable[index] = f_node;

        f_node->length = groupfd_size[group_fd];
        f_node->true_length = groupfd_size[group_fd];

        if(FLAGS_INCLUDE(flags, O_TRUNC) && f_node->true_length){
		    DEBUG_PRINT("We just opened a file with O_TRUNC that was already "
			    "open with nonzero length %li.  Updating length.\n",
			    f_node->true_length);
		    f_node->true_length = 0;
	    }

        DEBUG_PRINT("musl lib: alloc new DirectNode, addr: %lx, groupfd:%d, length: %lx\n", (unsigned long)f_node, group_fd, (unsigned long)groupfd_size[group_fd]);
    }

    ref_add(f_node);

    return f_node;	
}

struct DirectNode* find_file_node(int group_fd) {
    DEBUG_PRINT("musl lib: enter find_file_node\n");
    if (!is_legal_group_fd(group_fd)) return NULL;

    int index = hash(group_fd);
    DEBUG_PRINT("musl lib: enter find_file_node, groupfd:%d, index: %d\n", group_fd, index);
    struct DirectNode* current = hashTable[index];
    while (current != NULL) {
        if (current->group_fd == group_fd) {
            DEBUG_PRINT("musl lib: find a directfile, addr: %lx\n", (unsigned long)current);
            return current;
        }
        DEBUG_PRINT("musl lib: find_file_node loop, current: %lx\n", (unsigned long)current);
        current = current->next;
    }

    DEBUG_PRINT("musl lib: find_file_node cannot find\n");
    return NULL; // no find
}

void ref_add(struct DirectNode* node) {
    node_lock(node->lock);
    node->count++;
    node_unlock(node->lock);
}

int ref_dec(struct DirectNode* node) {
    int count;
    node_lock(node->lock);
    node->count--;
    count = node->count;
    node_unlock(node->lock);
    return count;
}

int test_file_in_pv(struct DirectNode* node, off_t offset, size_t count)
{
    size_t real_count = count;
    unsigned long start = groupfd_addr[node->group_fd] + offset;

    if (offset+count > node->length) {
        real_count = PAGE_ALIGN_UP(node->length) - offset;        
        DEBUG_PRINT("musl lib: offset %d + count %d is larger than file length %d, real count %d\n", 
                                    offset, count, node->length, real_count);
    }

    // check if all pages within length are in pagecache
    for (size_t i = 0; i < real_count; i += PAGE_SIZE) {
        if (!pv_test_bit((unsigned long)start + i)) {
            //DEBUG_PRINT("musl lib: page %lx not in pagecache\n", start + i);
            return 0;
        }

        if (ra_test_bit((unsigned long)start + i)) {
            //printf("musl lib: page %lx hit readahead mark\n", start + i);
            return 0;
        }
    }
    return 1;
}

int pv_test_bit(unsigned long addr) {
    unsigned long page_index = (addr - (unsigned long)PRIVATE_VMALLOC_START) / PAGE_SIZE;
    unsigned long byte_index = page_index / 8;
    unsigned long bit_index = page_index % 8;
    return (pv_bit_maps[byte_index] & (1 << bit_index)) != 0; // test bit
}

int ra_test_bit(unsigned long addr) {
    unsigned long page_index = (addr - (unsigned long)PRIVATE_VMALLOC_START) / PAGE_SIZE;
    unsigned long byte_index = page_index / 8;
    unsigned long bit_index = page_index % 8;
    return (ra_bit_maps[byte_index] & (1 << bit_index)) != 0; // test bit
}

static inline void log_lock()
{
	LOCK(__log_lock);
}

static inline void log_unlock()
{
	UNLOCK(__log_lock);
}

void append_log_entry(unsigned long groupfd, off_t offset, size_t count, 
                    bool is_new_alloc, unsigned long addr, unsigned long struct_file_addr) {
    //printf("musl lib: enter append_log_entry.\n");
    log_lock();

    struct log_entry* entry = &log_entrys[user_template->log_index];
    user_template->log_index = (user_template->log_index + 1);
    entry->groupfd = groupfd;
    entry->offset = offset;
    entry->count = count;
    entry->is_new_alloc = is_new_alloc;
    entry->addr = addr;
    entry->struct_file_addr = struct_file_addr;

    if (user_template->log_index >= max_log_index) {
        printf("musl lib: write log full.\n");
        __syscall(SYS_DIRECTFS_FLUSH_LOGS);
    }

    log_unlock();
    //printf("musl lib: leave append_log_entry.\n");
}


static inline void alloc_page_lock()
{
	LOCK(__alloc_page_lock);
}

static inline void alloc_page_unlock()
{
	UNLOCK(__alloc_page_lock);
}

#define PREALLOC_PAGES 32
unsigned long alloc_pages(size_t count) {
    unsigned long addr, len = count << PAGE_SHIFT;
    alloc_page_lock();

    if (user_template->user_pages_end_addr - len < user_template->user_pages_start_addr) {
        syscall_cp(SYS_DIRECTFS_ALLOC_PAGES, PREALLOC_PAGES+count);

        if (user_template->user_pages_end_addr - len < user_template->user_pages_start_addr) {
            alloc_page_unlock();
            printf("alloc_pages: alloc pages failed\n");
            return 0;
        }
    } 

    user_template->user_pages_end_addr -= len;
    addr = user_template->user_pages_end_addr;

    alloc_page_unlock();
    return addr;
}