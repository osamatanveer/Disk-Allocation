#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include "simplefs.h"

#define MAX_NAME 110
#define BITMAP_MAX 4096
#define ROOT_MAX 128
#define FCB_MAX 128
#define OFT_MAX 16
#define INDEX_MAX 1024
#define SUPERBLOCK_INDEX 0
#define BITMAP_INDEX 4
#define ROOT_INDEX 8
#define FCB_TABLE_INDEX 12
#define SetBit(A,k)     ( A[(k/32)] |= (1 << (k%32)) ) 
#define ClearBit(A,k)   ( A[(k/32)] &= ~(1 << (k%32)) )
#define TestBit(A,k)    ( A[(k/32)] & (1 << (k%32)) ) 

// Global Variables =======================================
int vdisk_fd; // Global virtual disk file descriptor. Global within the library.
              // Will be assigned with the vsfs_mount call.
              // Any function in this file can use this.
              // Applications will not use  this directly. 
// ========================================================

struct superblock{
    char vdiskname[MAX_NAME];
    int blocks;
    int blocks_free;
    int blocks_occupied;
};

struct bitmap{
    int bitmap[BITMAP_MAX];
};

struct root_entry{
    char filename[109];
    int fcb_index;
    int filler1, filler2, filler3; // to allign 1 entry to 128 bytes
};

struct fcb{
    char filler[112];
    int filesize;
    int index_table_ptr; // not actually a pointer, will contain an index to the disk block with the index table
    int block_last_written;
    int offset_of_last_wrt_block;
};

struct oft_entry{ // oft means open file table
    char filler[120];
    int fcb_index;
    int open_mode;
};

struct open_file_table{
    struct oft_entry oft_entries[OFT_MAX];
};

struct fcb_table{
    struct fcb fcb_entries[FCB_MAX];
};

struct index_table{
    int index_entries[INDEX_MAX];
};

struct root{
    struct root_entry root_entries[ROOT_MAX];
};


struct superblock *superblock;
struct bitmap *bitmap;
struct root *root;
struct fcb_table *fcb_table;
struct open_file_table *oft;
struct index_table *index_table;

// allocate space for buffers using malloc

// read block k from disk (virtual disk) into buffer block.
// size of the block is BLOCKSIZE.
// space for block must be allocated outside of this function.
// block numbers start from 0 in the virtual disk. 
int read_block (void *block, int k)
{
    int n;
    int offset;

    offset = k * BLOCKSIZE;
    lseek(vdisk_fd, (off_t) offset, SEEK_SET);
    n = read (vdisk_fd, block, BLOCKSIZE);
    if (n != BLOCKSIZE) {
	perror ("read error");
	return -1;
    }
    return (0); 
}

// write block k into the virtual disk. 
int write_block (void *block, int k)
{
    int n;
    int offset;

    offset = k * BLOCKSIZE;
    lseek(vdisk_fd, (off_t) offset, SEEK_SET);
    n = write (vdisk_fd, block, BLOCKSIZE);
    // printf("%d\n", n);
    if (n != BLOCKSIZE) {
	perror ("write error");
	return (-1);
    }
    return 0; 
}

void read_struct (void* ptr, int upper_limit) {
    read_block(ptr, (upper_limit - 3));
    ptr += BLOCKSIZE;
    read_block(ptr, (upper_limit - 2));
    ptr += BLOCKSIZE;
    read_block(ptr, (upper_limit - 1));
    ptr += BLOCKSIZE;
    read_block(ptr, (upper_limit));
}

void write_struct (void* ptr, int upper_limit) {
    write_block(ptr, (upper_limit - 3));
    ptr += BLOCKSIZE;
    write_block(ptr, (upper_limit - 2));
    ptr += BLOCKSIZE;
    write_block(ptr, (upper_limit - 1));
    ptr += BLOCKSIZE;
    write_block(ptr, (upper_limit));
}

/**********************************************************************
   The following functions are to be called by applications directly. 
***********************************************************************/

// this function is partially implemented.
int create_format_vdisk (char *vdiskname, unsigned int m)
{
    char command[1000];
    int size;
    int num = 1;
    int count;
    size  = num << m;
    count = size / BLOCKSIZE;
    //    printf ("%d\n", sizeof(struct fcb));
    sprintf (command, "dd if=/dev/zero of=%s bs=%d count=%d",
             vdiskname, BLOCKSIZE, count);
    // printf ("executing command = %s\n", command);
    system (command);

    sfs_mount(vdiskname); // mount will malloc all pointers
    
    strcpy (superblock->vdiskname, vdiskname); 
    superblock->blocks = count;
    superblock->blocks_free = count;
    superblock->blocks_occupied = 0;

    for (int i = 0; i < BITMAP_MAX; i++)
    {
        bitmap->bitmap[i] = 0; // clearing array
    }
    for (int i = 0; i < 13; i++)
    {
        ClearBit(bitmap->bitmap, i); // 0 means used, 1 means free
    }
    for (int i = 13; i < BITMAP_MAX * 32; i++)
    {
        SetBit(bitmap->bitmap, i); // 0 means used, 1 means free
    }

    for (int i = 0; i < ROOT_MAX; i++)
    {
        strcpy(root->root_entries[i].filename, "");
        root->root_entries[i].fcb_index = -1;
    }

    for (int i = 0; i < FCB_MAX; i++)
    {
        fcb_table->fcb_entries[i].filesize = -1;
        fcb_table->fcb_entries[i].index_table_ptr = -1;
        fcb_table->fcb_entries[i].block_last_written = -1;
        fcb_table->fcb_entries[i].offset_of_last_wrt_block = -1;
    }
    
    write_block(superblock, SUPERBLOCK_INDEX);

    write_struct(((void*)bitmap), BITMAP_INDEX);

    write_struct(((void*)root), ROOT_INDEX);

    write_struct(((void*)fcb_table), FCB_TABLE_INDEX);

    sfs_umount(); // unmount will free all pointers

    return (0); 
}


// already implemented
int sfs_mount (char *vdiskname)
{
    // simply open the Linux file vdiskname and in this
    // way make it ready to be used for other operations.
    // vdisk_fd is global; hence other function can use it. 
    vdisk_fd = open(vdiskname, O_RDWR); 

    superblock = (struct superblock*) malloc(sizeof(struct superblock));
    bitmap = (struct bitmap*) malloc(sizeof(struct bitmap));
    root = (struct root*) malloc(sizeof(struct root));
    fcb_table = (struct fcb_table*) malloc(sizeof(struct fcb_table));
    oft = (struct open_file_table*) malloc(sizeof(struct open_file_table));
    index_table = (struct index_table*) malloc(sizeof(struct index_table));

    for (int i = 0; i < OFT_MAX; i++)
    {
        oft->oft_entries[i].fcb_index = -1; // signifies all entries are unused (-1 for free)
    }

    return(0);
}


// already implemented
int sfs_umount ()
{
    fsync (vdisk_fd); // copy everything in memory to disk
    close (vdisk_fd);
    
    free(superblock);
    free(bitmap);
    free(root);
    free(fcb_table);
    free(oft);
    free(index_table);

    return (0); 
}


int sfs_create(char *filename)
{
    if(strlen(filename) > 109 || strlen(filename) == 0) {
        printf("Filenmame should be a maximum of 110 characters including \\0 and not empty\n");
        return -1;
    }

    read_block(superblock, SUPERBLOCK_INDEX);
    read_struct(((void*)bitmap), BITMAP_INDEX);
    read_struct(((void*)root), ROOT_INDEX);
    read_struct(((void*)fcb_table), FCB_TABLE_INDEX);
    
    int index;
    bool free = false;

    for (int i = 0; i < ROOT_MAX; i++)
    {
        if (strcmp(filename, root->root_entries[i].filename)==0)
        {
            puts("File already exists");
            return -1;
        }
        
        if (root->root_entries[i].fcb_index == -1)
        {
            index = i;
            free = true;
            break;
        }
    }

    if (free == false) {
        puts("Directory is full");
        return -1;
    }
    
    free = false;
    int fcb_index;
    
    for (int i = 0; i < FCB_MAX; i++)
    {
        if (fcb_table->fcb_entries[i].index_table_ptr == -1)
        {
            fcb_index = i;
            free = true;
            break;
        }
    }

    if (free == false) {
        puts("File Control Block is full");
        return -1;
    }

    free = false;
    int indextbl_index;

    for (int i = 0; i < superblock->blocks; i++)
    {
        if (TestBit(bitmap->bitmap, i))
        {
            indextbl_index = i;
            free = true;
            break;
        }
    }

    if (free == false) {
        puts("All Disk Blocks Are Occupied");
        return -1;
    }
    
    strcpy(root->root_entries[index].filename, filename); 
    root->root_entries[index].fcb_index = fcb_index;
    fcb_table->fcb_entries[fcb_index].filesize = 0;
    fcb_table->fcb_entries[fcb_index].index_table_ptr = indextbl_index;
    fcb_table->fcb_entries[fcb_index].block_last_written = -1;
    fcb_table->fcb_entries[fcb_index].offset_of_last_wrt_block = -1;
    ClearBit(bitmap->bitmap, indextbl_index);
    superblock->blocks_free -= 1;
    superblock->blocks_occupied += 1;

    for (int i = 0; i < INDEX_MAX; i++)
    {
        index_table->index_entries[i] = -1;
    }
    
    write_block(superblock, SUPERBLOCK_INDEX);
    write_struct(((void*)bitmap), BITMAP_INDEX);
    write_struct(((void*)root), ROOT_INDEX);
    write_struct(((void*)fcb_table), FCB_TABLE_INDEX);
    write_block(index_table, indextbl_index);

    return (0);
}

int sfs_open(char *file, int mode)
{
    bool exists = false;
    int fcb_index;
    
    read_struct(((void*)root), ROOT_INDEX);
    for (int i = 0; i < ROOT_MAX; i++)
    {
        if (strcmp(root->root_entries[i].filename, file) == 0)
        {
            exists = true;
            fcb_index = root->root_entries[i].fcb_index;
            break;
        }
    }

    if (exists == false)
    {
        puts("There is no file with the specified filename, check again");
        return -1;
    }

    bool free = false;
    int free_oft_index;

    for (int i = 0; i < OFT_MAX; i++)
    {
        if (oft->oft_entries[i].fcb_index == -1)
        {
            free = true;
            free_oft_index = i;
            break;
        }
    }

    if (free == false)
    {
        puts("Max number of files open already");
        return -1;
    }

    oft->oft_entries[free_oft_index].fcb_index = fcb_index;
    oft->oft_entries[free_oft_index].open_mode = mode;

    return (free_oft_index); 
}

int sfs_close(int fd){
    if (oft->oft_entries[fd].fcb_index == -1)
    {
        puts("File not open, open file first");
        return -1;
    }
    oft->oft_entries[fd].fcb_index = -1;
    oft->oft_entries[fd].open_mode = -1;
    
    return (0); 
}

int sfs_getsize (int  fd)
{
    if (oft->oft_entries[fd].fcb_index == -1)
    {
        puts("File not open, open file first");
        return -1;
    }

    read_struct(((void*)fcb_table), FCB_TABLE_INDEX);
    return fcb_table->fcb_entries[oft->oft_entries[fd].fcb_index].filesize;  
}

int sfs_read(int fd, void *buf, int n){
    if (oft->oft_entries[fd].fcb_index == -1)
    {
        puts("File not open, open file first");
        return -1;
    }
    if (oft->oft_entries[fd].open_mode != MODE_READ)
    {
        puts("File is open in append mode, close and open in read mode");
        return -1;
    }

    int filesize = sfs_getsize(fd);
    if (n > filesize)
    {
        puts("Number of bits to read is greater than filesize");
        printf("Filesize = %d\n", filesize);
        return -1;
    }
    
    char file_buffer[BLOCKSIZE];
    read_struct(((void*)fcb_table), FCB_TABLE_INDEX);
    int index_tbl_ptr =  fcb_table->fcb_entries[oft->oft_entries[fd].fcb_index].index_table_ptr;
    read_block(index_table, index_tbl_ptr);
    int bytes_copied = 0;

    for (int i = 0; i < INDEX_MAX; i++)
    {
        if (index_table->index_entries[i] != -1)    
        {
            read_block(((void*)file_buffer), index_table->index_entries[i]);
            int to_copy;
            if (n > BLOCKSIZE)
            {
                to_copy = BLOCKSIZE;
                n -= BLOCKSIZE;
            }
            else {
                to_copy = n;
                n -= n;
            }
            memcpy(buf + bytes_copied, file_buffer, to_copy);
            bytes_copied += to_copy;
        }
        if (n <= 0)
        {
            break;
        }
    }
    return (bytes_copied); 
}


int sfs_append(int fd, void *buf, int n)
{
    if (oft->oft_entries[fd].fcb_index == -1)
    {
        puts("File not open, open file first");
        return -1;
    }
    if (oft->oft_entries[fd].open_mode != MODE_APPEND)
    {
        puts("File is open in read mode, close and open in append mode");
        return -1;
    }
    read_struct(((void*)fcb_table), FCB_TABLE_INDEX);
    int fcb_index = oft->oft_entries[fd].fcb_index;
    int filesize = fcb_table->fcb_entries[fcb_index].filesize;
    if ((n + filesize) > 4194304) // 4Mb written in bytes
    {
        puts("Appending data will exceed max file size (4 Mb), function will not write");
        return -1;
    }
    int bytes_copied = 0;
    int last_block = fcb_table->fcb_entries[fcb_index].block_last_written;
    int offset = fcb_table->fcb_entries[fcb_index].offset_of_last_wrt_block;
    int fill_last_written_block = BLOCKSIZE - offset;
    char temp[BLOCKSIZE];
    if ((filesize != 0) && ((filesize % BLOCKSIZE) != 0))
    {   
        read_block(temp, last_block);
        int to_copy;
        if (n > fill_last_written_block)
        {
            to_copy = fill_last_written_block;
        }
        else {
            to_copy = n;
        }
        
        memcpy(temp + offset, buf + bytes_copied, to_copy); // -1 for null termination;
        bytes_copied += to_copy;
        n -= to_copy;
        fcb_table->fcb_entries[fcb_index].block_last_written = last_block;
        fcb_table->fcb_entries[fcb_index].offset_of_last_wrt_block = offset + to_copy;
        write_block(temp, last_block);
    }
    
    int new_blocks = ceil((double)n / BLOCKSIZE);
    read_block(superblock, SUPERBLOCK_INDEX);
    read_struct(((void*)bitmap), BITMAP_INDEX);
    read_block(index_table, fcb_table->fcb_entries[fcb_index].index_table_ptr);
    for (int j = 0; j < new_blocks; j++)
    {
        int index_ent_ptr;
        int free_disk_block;
        bool free;
        for (int i = 0; i < BITMAP_MAX * 32; i++)
        {
            if (TestBit(bitmap->bitmap, i))
            {
                free = true;
                free_disk_block = i;
                break;
            }
        }
        if (free == false)
        {
            puts("All Disk Blocks are occupado");
            return -1;
        }
        
        free = false;
        for (int i = 0; i < INDEX_MAX; i++)
        {
            if (index_table->index_entries[i] == -1)
            {
                free = true;
                index_ent_ptr = i;
                break;
            }
        }
        if (free == false)
        {
            puts("Index table is full");
            return -1;
        }

        index_table->index_entries[index_ent_ptr] = free_disk_block;
        int to_copy;
        if (n > BLOCKSIZE)
        {
            to_copy = BLOCKSIZE;
            n -= BLOCKSIZE;
        }
        else{
            to_copy = n;
            n -= n;
        }
        memcpy(temp, buf + bytes_copied, to_copy);
        bytes_copied += to_copy;
        ClearBit(bitmap->bitmap, free_disk_block);
        fcb_table->fcb_entries[fcb_index].block_last_written = free_disk_block;
        fcb_table->fcb_entries[fcb_index].offset_of_last_wrt_block = to_copy;
        superblock->blocks_free -= 1;
        superblock->blocks_occupied += 1;
        write_block(temp, free_disk_block);
    }
    
    fcb_table->fcb_entries[fcb_index].filesize += bytes_copied;
    write_block(superblock, SUPERBLOCK_INDEX);
    write_block(index_table, fcb_table->fcb_entries[fcb_index].index_table_ptr);
    write_struct(((void*)bitmap), BITMAP_INDEX);
    write_struct(((void*)fcb_table), FCB_TABLE_INDEX);

    return (bytes_copied); 
}

int sfs_delete(char *filename)
{
    read_block(superblock, SUPERBLOCK_INDEX);
    read_struct(((void*)bitmap), BITMAP_INDEX);
    read_struct(((void*)root), ROOT_INDEX);
    read_struct(((void*)fcb_table), FCB_TABLE_INDEX);
    bool exists = false;
    int fcb_index;
    char erase[BLOCKSIZE] = {0};
    char filename_erase[109] = {0};

    for (int i = 0; i < ROOT_MAX; i++)
    {
        if (strcmp(filename, root->root_entries[i].filename) == 0)
        {
            fcb_index = root->root_entries[i].fcb_index;
            memcpy(root->root_entries[i].filename, filename_erase, 109);
            root->root_entries[i].fcb_index = -1;
            exists = true;
            break;
        }
    }
    if (exists == false)
    {
        puts("File does not exist");
        return -1;
    }

    for (int i = 0; i < OFT_MAX; i++)
    {
        if (oft->oft_entries[i].fcb_index == fcb_index)
        {
            sfs_close(i);
        }
    }
    
    int index_table_ptr = fcb_table->fcb_entries[fcb_index].index_table_ptr;
    fcb_table->fcb_entries[fcb_index].index_table_ptr = -1;
    fcb_table->fcb_entries[fcb_index].filesize = -1;
    fcb_table->fcb_entries[fcb_index].block_last_written = -1;
    fcb_table->fcb_entries[fcb_index].offset_of_last_wrt_block = -1;

    read_block(index_table, index_table_ptr);
    for (int i = 0; i < INDEX_MAX; i++)
    {
        if (index_table->index_entries[i] != -1)
        {
            write_block(erase, index_table->index_entries[i]);
            SetBit(bitmap->bitmap, index_table->index_entries[i]);
            superblock->blocks_free += 1;
            superblock->blocks_occupied -= 1;        
        }
    }
    
    write_block(erase, index_table_ptr);
    SetBit(bitmap->bitmap, index_table_ptr);
    write_block(superblock, SUPERBLOCK_INDEX);
    write_struct(((void*)bitmap), BITMAP_INDEX);
    write_struct(((void*)root), ROOT_INDEX);
    write_struct(((void*)fcb_table), FCB_TABLE_INDEX);

    return (0); 
}

