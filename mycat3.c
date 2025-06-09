#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h> // For uintptr_t

// 函数：获取页大小 (同 mycat2)
long io_blocksize()
{
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size == -1)
    {
        perror("sysconf_SC_PAGESIZE failed");
        fprintf(stderr, "Warning: sysconf(_SC_PAGESIZE) failed. Using default 4096.\n");
        return 4096;
    }
    return page_size;
}

// 分配对齐的内存
void *align_alloc(size_t size, size_t alignment)
{
    if (alignment == 0)
        alignment = 1; // Avoid division by zero if alignment is bad
                       // Ensure alignment is a power of two
    if ((alignment & (alignment - 1)) != 0)
    {
        fprintf(stderr, "Alignment must be a power of two.\n");
        return NULL;
    }

    void *original_ptr = NULL;
    // We need to store the original pointer right before the aligned pointer.
    // So, we allocate 'size' + 'alignment - 1' (for finding the aligned spot)
    // + 'sizeof(void*)' (to store the original pointer).
    size_t total_size = size + alignment - 1 + sizeof(void *);
    original_ptr = malloc(total_size);

    if (original_ptr == NULL)
    {
        return NULL;
    }

    // Calculate the aligned pointer.
    // Add sizeof(void*) to leave space for storing the original_ptr.
    // Then align this new address.
    uintptr_t aligned_addr_val = ((uintptr_t)original_ptr + sizeof(void *) + alignment - 1) & ~(alignment - 1);
    void *aligned_ptr = (void *)aligned_addr_val;

    // Store the original pointer just before the aligned block.
    *((void **)((uintptr_t)aligned_ptr - sizeof(void *))) = original_ptr;

    return aligned_ptr;
}

// 释放对齐的内存
void align_free(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }
    // Retrieve the original pointer stored just before the aligned block.
    void *original_ptr = *((void **)((uintptr_t)ptr - sizeof(void *)));
    free(original_ptr);
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int fd_in = open(argv[1], O_RDONLY);
    if (fd_in == -1)
    {
        perror("Error opening input file");
        exit(EXIT_FAILURE);
    }

    long page_size = io_blocksize();
    // 使用 align_alloc
    char *buffer = (char *)align_alloc(page_size, page_size);
    if (buffer == NULL)
    {
        perror("Failed to allocate aligned buffer");
        close(fd_in);
        exit(EXIT_FAILURE);
    }

    ssize_t bytes_read;
    ssize_t bytes_written_total;
    ssize_t bytes_written_one;

    while ((bytes_read = read(fd_in, buffer, page_size)) > 0)
    {
        bytes_written_total = 0;
        while (bytes_written_total < bytes_read)
        {
            bytes_written_one = write(STDOUT_FILENO, buffer + bytes_written_total, bytes_read - bytes_written_total);
            if (bytes_written_one == -1)
            {
                perror("Error writing to stdout");
                align_free(buffer);
                close(fd_in);
                exit(EXIT_FAILURE);
            }
            bytes_written_total += bytes_written_one;
        }
    }

    if (bytes_read == -1)
    {
        perror("Error reading from input file");
    }

    align_free(buffer);
    if (close(fd_in) == -1)
    {
        perror("Error closing input file");
        exit(EXIT_FAILURE);
    }

    return (bytes_read == -1) ? EXIT_FAILURE : EXIT_SUCCESS;
}