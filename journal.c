#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define BLOCK_SIZE 4096
#define JOURNAL_START 1
#define INODE_BMAP_BLOCK 17
#define INODE_TABLE_START 19
#define ROOT_DIR_DATA_BLOCK 21

#define JOURNAL_MAGIC 0x4A524E4C
#define REC_DATA 1
#define REC_COMMIT 2


struct journal_header {
    uint32_t magic;
    uint32_t nbytes_used;
};

struct rec_header {
    uint16_t type;
    uint16_t size;
};

struct inode {
    uint16_t type;
    uint16_t links;
    uint32_t size;
    uint32_t direct[8];
    uint32_t ctime;
    uint32_t mtime;
    uint8_t _pad[128 - (2+2+4+8*4+4+4)];
};

struct dirent {
    uint32_t inode;
    char name[28];
};

void *disk_ptr = NULL;

void* get_blk(uint32_t blk_num) {
    size_t off = (size_t)blk_num * 4096;
    char *base = (char*)disk_ptr;
    void *result = (void*)(base + off);
    return result;
}

int open_vsfs() {
    int file_desc = open("vsfs.img", O_RDWR);
    
    int total_blks = 85;
    size_t map_size = total_blks * 4096;
    disk_ptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, file_desc, 0);
    close(file_desc);
    
    return 0;
}

void cmd_create(const char *fname) {
    struct journal_header *j_hdr = (struct journal_header*)get_blk(1);
    
    if (j_hdr->magic != 0x4A524E4C) {
        printf("Init new journal\n");
        j_hdr->magic = 0x4A524E4C;
        j_hdr->nbytes_used = sizeof(struct journal_header);
    }

    uint8_t *bitmap = (uint8_t*)get_blk(17);
    int free_inum = -1;
    int i;
    for (i = 1; i < 64; i++) {
        int byte_idx = i / 8;
        int bit_idx = i % 8;
        int bit_check = bitmap[byte_idx] & (1 << bit_idx);
        if (!bit_check) {
            free_inum = i;
            break;
        }
    }

    struct dirent *root_dir = (struct dirent*)get_blk(21);
    int free_slot = -1;
    int j;
    for (j = 2; j < 128; j++) {
        int ino_check = root_dir[j].inode;
        char first_char = root_dir[j].name[0];
        if (ino_check == 0 && first_char == '\0') {
            free_slot = j;
            break;
        }
    }

    uint8_t bmap_buf[4096];
    uint8_t inode_buf[4096]; 
    uint8_t dir_buf[4096];

    memcpy(bmap_buf, get_blk(17), 4096);
    memcpy(inode_buf, get_blk(19), 4096);
    memcpy(dir_buf, get_blk(21), 4096);

    int byte_pos = free_inum / 8;
    int bit_pos = free_inum % 8;
    int bit_mask = 1 << bit_pos;
    bmap_buf[byte_pos] |= bit_mask;
    
    int inode_sz = 128;
    int inode_off = free_inum * inode_sz;
    uint8_t *inode_base = inode_buf + inode_off;
    struct inode *new_ino = (struct inode*)inode_base;
    new_ino->type = 1; 
    new_ino->links = 1;
    new_ino->size = 0;
    uint32_t curr_time = (uint32_t)time(NULL);
    new_ino->ctime = curr_time;
    new_ino->mtime = curr_time;

    struct dirent *dir_ents = (struct dirent*)dir_buf;
    dir_ents[free_slot].inode = free_inum;
    strcpy(dir_ents[free_slot].name, fname);

    struct inode *root_ino = (struct inode*)inode_buf;
    int entry_sz = 32;
    int new_size = (free_slot + 1) * entry_sz;
    if (root_ino->size < new_size) {
        root_ino->size = new_size;
    }

    uint32_t blk_nums[] = {17, 19, 21};
    void* data_bufs[] = {bmap_buf, inode_buf, dir_buf};

    int k;
    for (k = 0; k < 3; k++) {
        char *j_base = (char*)get_blk(1);
        uint32_t curr_off = j_hdr->nbytes_used;
        char *rec_pos = j_base + curr_off;
        struct rec_header *rec_hdr = (struct rec_header*)rec_pos;
        
        rec_hdr->type = 1;
        int hdr_sz = sizeof(struct rec_header);
        int blkno_sz = 4;
        int data_sz = 4096;
        int total_sz = hdr_sz + blkno_sz + data_sz;
        rec_hdr->size = total_sz;
        
        char *rec_base = (char*)rec_hdr;
        char *blk_ptr_pos = rec_base + hdr_sz;
        uint32_t *blk_ptr = (uint32_t*)blk_ptr_pos;
        *blk_ptr = blk_nums[k];
        
        char *data_pos = rec_base + hdr_sz + blkno_sz;
        memcpy(data_pos, data_bufs[k], 4096);
        
        j_hdr->nbytes_used += rec_hdr->size;
    }

    char *j_base = (char*)get_blk(1);
    uint32_t commit_off = j_hdr->nbytes_used;
    char *commit_pos = j_base + commit_off;
    struct rec_header *commit_rec = (struct rec_header*)commit_pos;
    commit_rec->type = 2;
    commit_rec->size = sizeof(struct rec_header);
    j_hdr->nbytes_used += commit_rec->size;

    printf("Created %s (inode %d) in journal\n", fname, free_inum);
    int total_sz = 85 * 4096;
    msync(disk_ptr, total_sz, MS_SYNC);
}

void cmd_install() {
    struct journal_header *j_hdr = (struct journal_header*)get_blk(1);

    uint32_t pos = sizeof(struct journal_header);
    while (pos < j_hdr->nbytes_used) {
        char *j_base = (char*)get_blk(1);
        char *rec_pos = j_base + pos;
        struct rec_header *rec = (struct rec_header*)rec_pos;
        
        if (rec->type == 1) {
            char *rec_base = (char*)rec;
            int hdr_sz = sizeof(struct rec_header);
            char *blk_num_pos = rec_base + hdr_sz;
            uint32_t *blk_num_ptr = (uint32_t*)blk_num_pos;
            uint32_t target_blk = *blk_num_ptr;
            
            char *data_start = rec_base + hdr_sz + 4;
            void *dest = get_blk(target_blk);
            memcpy(dest, data_start, 4096);
        }
        
        pos += rec->size;
    }

    j_hdr->nbytes_used = sizeof(struct journal_header);
    int total_sz = 85 * 4096;
    msync(disk_ptr, total_sz, MS_SYNC);
    printf("Journal installed\n");
}

int main(int argc, char **argv) {
    
    open_vsfs();

    if (strcmp(argv[1], "create") == 0) {
        cmd_create(argv[2]);
    } 
    else if (strcmp(argv[1], "install") == 0) {
        cmd_install();
    }

    int total_sz = 85 * 4096;
    munmap(disk_ptr, total_sz);
    return 0;
}
