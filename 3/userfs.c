#include "userfs.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
	/** Block memory. */
	char *memory;
	/** How many bytes are occupied. */
	int occupied;
	/** Next block in the file. */
	struct block *next;
	/** Previous block in the file. */
	struct block *prev;

	/* PUT HERE OTHER MEMBERS */
};

void init_block(struct block *block) {
    block->memory = calloc(BLOCK_SIZE, sizeof(char));
    block->occupied = 0;
    block->next = NULL;
    block->prev = NULL;
}

struct file {
	/** Double-linked list of file blocks. */
	struct block *block_list;
	/**
	 * Last block in the list above for fast access to the end
	 * of file.
	 */
	struct block *last_block;
	/** How many file descriptors are opened on the file. */
	int refs;
	/** File name. */
	const char *name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;

    int need_delete;

    int total_bytes;
};

void init_file(struct file* file) {
    file->block_list = calloc(1, sizeof(struct block));
    init_block(file->block_list );

    file->last_block = file->block_list;
    file->refs = 0;

    file->next = NULL;
    file->prev = NULL;

    file->need_delete = 0;
    file->total_bytes = 0;
}
/** List of all files. */
static struct file *file_list = NULL;

struct place {
    int number_block;
    int number_byte;
};

struct filedesc {
	struct file *file;

    struct place write;
//    struct place read;

    int is_write;
    int is_read;

	/* PUT HERE OTHER MEMBERS */
};

void init_filedesc(struct filedesc *filedesc) {
//   filedesc->read.number_block = 0;
//   filedesc->read.number_byte = 0;

   filedesc->write.number_byte = 0;
   filedesc->write.number_byte = 0;

   filedesc->is_write = 0;
   filedesc->is_read = 0;

   filedesc->file = calloc(1, sizeof(struct file));
   init_file(filedesc->file);
}

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

enum ufs_error_code
ufs_errno()
{
	return ufs_error_code;
}

int
ufs_open(const char *filename, int flags)
{
    int is_exist = 0;
    struct file *copy_file_list = file_list;

    if (flags == 0) {
        flags = UFS_READ_WRITE;
    }
    if (flags == UFS_CREATE) {
        flags |= UFS_READ_WRITE;
    }

    while (copy_file_list) {
        if (strcmp(copy_file_list->name, filename) == 0) {
            is_exist = 1;
            break;
        }
        copy_file_list = copy_file_list->next;
    }

    if ((flags & UFS_CREATE) == 0 && is_exist == 0) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    if (is_exist == 0) {
        copy_file_list = calloc(1, sizeof(struct file));
        init_file(copy_file_list);
        copy_file_list->name = strdup(filename);

        if (file_list == NULL) {
            file_list = copy_file_list;
        } else {
            copy_file_list->next = file_list;
            file_list->prev = copy_file_list;
            file_list = copy_file_list;
        }
    }

    if (is_exist == 1 && (flags & UFS_CREATE) && copy_file_list->need_delete) {
        copy_file_list = calloc(1, sizeof(struct file));
        init_file(copy_file_list);
    }

    if (copy_file_list->need_delete) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    if (file_descriptors == NULL) {
        file_descriptor_capacity = 4;
        file_descriptors = calloc(file_descriptor_capacity, sizeof(struct filedesc*));
    }

    int fd = -1;
    // find open descriptor
    for(int i = 0; i < file_descriptor_capacity; ++i) {
        if (file_descriptors[i] == NULL) {
            fd = i;
            break;
        }
    }

    if (fd == -1) {
        file_descriptor_capacity *= 2;
        file_descriptors = realloc(file_descriptors, sizeof(struct filedesc*) * file_descriptor_capacity);
        fd = file_descriptor_count + 1;
    }

    file_descriptors[fd] = calloc(1, sizeof(struct filedesc));
    init_filedesc(file_descriptors[fd]);
    file_descriptors[fd]->file = copy_file_list;
    file_descriptors[fd]->file->refs++;

    if (flags & UFS_READ_ONLY) {
        file_descriptors[fd]->is_read = 1;
    }
    if (flags & UFS_WRITE_ONLY) {
        file_descriptors[fd]->is_write = 1;
    }
    if (flags & UFS_READ_WRITE) {
        file_descriptors[fd]->is_write = 1;
        file_descriptors[fd]->is_read = 1;
    }

    file_descriptor_count++;
	return fd;
}

int check_exist_fd(int fd) {
    if (fd > file_descriptor_capacity || fd == -1 || file_descriptors[fd] == NULL) {
        return 0;
    }
    return 1;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
    if (!check_exist_fd(fd)) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *pFiledesc = file_descriptors[fd];
    if (pFiledesc->is_write == 0) {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }

    struct block *pBlock = pFiledesc->file->block_list;

    for(int i = 0; i < pFiledesc->write.number_block; ++i) {
        pBlock = pBlock->next;
    }

    if (pBlock->occupied == pFiledesc->write.number_byte && pFiledesc->file->total_bytes == MAX_FILE_SIZE) {
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }

    ssize_t writer = 0;
    for(int i = 0; i < size; ++i) {
        pBlock->memory[pFiledesc->write.number_byte++] = buf[i];
        if (pBlock->occupied < pFiledesc->write.number_byte) {
            pFiledesc->file->total_bytes++;
            pBlock->occupied = pBlock->occupied > pFiledesc->write.number_byte ? pBlock->occupied : pFiledesc->write.number_byte;
        }
        ++writer;
        if (pFiledesc->file->total_bytes == MAX_FILE_SIZE) {
            return writer;
        }

        if (pBlock->occupied == BLOCK_SIZE) {
            pBlock->next = calloc(1, sizeof(struct block));
            init_block(pBlock->next);
            pBlock->next->prev = pBlock;
            pBlock = pBlock->next;

            pFiledesc->file->last_block = pBlock;

            pFiledesc->write.number_block++;
            pFiledesc->write.number_byte = 0;
        }

    }

    return writer;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
    if (!check_exist_fd(fd)) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *pFiledesc = file_descriptors[fd];
    if (pFiledesc->is_read == 0) {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }

    struct block *pBlock = pFiledesc->file->block_list;

    for(int i = 0; i < pFiledesc->write.number_block; ++i) {
        pBlock = pBlock->next;
    }

    ssize_t number_size_read = 0;
    int current_byte_block = pFiledesc->write.number_byte;
    for(int i = 0; i < size && pBlock && (pBlock != pFiledesc->file->last_block || current_byte_block < pBlock->occupied); ++i) {
        buf[i] = pBlock->memory[pFiledesc->write.number_byte++];
        ++number_size_read;
        ++current_byte_block;
        if (pFiledesc->write.number_byte == BLOCK_SIZE) {
            pFiledesc->write.number_byte = 0;
            pFiledesc->write.number_block++;
            pBlock = pBlock->next;
            current_byte_block = 0;
        }
    }

    return number_size_read;
}

int
ufs_close(int fd)
{
    if (!check_exist_fd(fd)) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    file_descriptors[fd]->file->refs--;
    file_descriptor_count--;

    if (file_descriptors[fd]->file->refs == 0 && file_descriptors[fd]->file->need_delete == 1) {
        return ufs_delete(file_descriptors[fd]->file->name);
    }

    free(file_descriptors[fd]);
    file_descriptors[fd] = NULL;
    return 0;
}

int
ufs_delete(const char *filename)
{
    struct file* copy_file_list = file_list;
    while(copy_file_list) {
        if (strcmp(copy_file_list->name, filename) == 0) {
            if (copy_file_list->refs != 0) {
                copy_file_list->need_delete = 1;
                return 0;
            }

           if (copy_file_list->next != NULL) {
               if (copy_file_list->prev != NULL) {
                   copy_file_list->prev->next = copy_file_list->next;
                   copy_file_list->next->prev = copy_file_list->prev;
               } else {
                   copy_file_list->next->prev = NULL;
               }
           } else {
               if (copy_file_list->prev == NULL) {
                   file_list = NULL;
               } else {
                   copy_file_list->prev->next = NULL;
               }
           }

           free(copy_file_list);
           return 0;
        }
        copy_file_list = copy_file_list->next;
    }

    ufs_error_code = UFS_ERR_NO_FILE;
    return -1;
}

int
ufs_resize(int fd, size_t new_size) {
    ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
    return -1;
}