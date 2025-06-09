// mycat6.c
#define _GNU_SOURCE // For posix_fadvise
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h> // open, O_RDONLY, posix_fadvise declarations
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <string.h> // <--- 添加这一行以声明 strerror
#define OPTIMAL_BUFFER_SIZE (256 * 1024)

long determine_io_blocksize_mycat6(int fd)
{
    // Same logic as mycat5, can be simplified to just return OPTIMAL_BUFFER_SIZE
    // or retain the fs_blk_size check if desired.
    // For this task, fadvise is the key change, buffer size logic can be identical to mycat5.
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size == -1)
        page_size = 4096;

    struct stat file_stat;
    blksize_t fs_blk_size = 0;

    if (fstat(fd, &file_stat) == 0)
    {
        fs_blk_size = file_stat.st_blksize;
        if (fs_blk_size <= 0 || (fs_blk_size & (fs_blk_size - 1)) != 0)
        {                    // Not positive or not power of 2
            fs_blk_size = 0; // Treat as invalid for comparison
        }
    }

    long chosen_buffer_size = OPTIMAL_BUFFER_SIZE;
    if (fs_blk_size > 0 && (long)fs_blk_size > chosen_buffer_size)
    {
        chosen_buffer_size = (long)fs_blk_size;
    }
    // printf("Debug mycat6: Buffer size: %ld\n", chosen_buffer_size);
    return chosen_buffer_size;
}

// align_alloc and align_free are identical to mycat5
void *align_alloc(size_t size, size_t alignment)
{
    if (alignment == 0)
        alignment = 1;
    if ((alignment & (alignment - 1)) != 0 && alignment != 1)
    {
        alignment = sysconf(_SC_PAGESIZE);
        if (alignment <= 0 || (alignment & (alignment - 1)) != 0)
            alignment = 4096;
    }
    void *original_ptr;
    size_t total_size = size + alignment - 1 + sizeof(void *);
    original_ptr = malloc(total_size);
    if (original_ptr == NULL)
    {
        perror("malloc failed in align_alloc");
        return NULL;
    }
    uintptr_t aligned_addr_val = ((uintptr_t)original_ptr + sizeof(void *) + alignment - 1) & ~(alignment - 1);
    void *aligned_ptr = (void *)aligned_addr_val;
    *((void **)((uintptr_t)aligned_ptr - sizeof(void *))) = original_ptr;
    return aligned_ptr;
}

void align_free(void *ptr)
{
    if (ptr == NULL)
        return;
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

    // --- Add posix_fadvise call ---
    // Advise the kernel that we will be reading this file sequentially.
    // offset = 0, len = 0 means advise for the entire file.
    int ret_fadvise = posix_fadvise(fd_in, 0, 0, POSIX_FADV_SEQUENTIAL);
    if (ret_fadvise != 0)
    {
        // fadvise failure is not critical for cat's core functionality,
        // so we just print a warning and continue.
        fprintf(stderr, "Warning: posix_fadvise (SEQUENTIAL) failed: %s\n", strerror(ret_fadvise));
    }
    // --- End of posix_fadvise call ---

    long buffer_size = determine_io_blocksize_mycat6(fd_in);

    long system_page_size = sysconf(_SC_PAGESIZE);
    if (system_page_size == -1)
    {
        perror("sysconf(_SC_PAGESIZE) failed for alignment");
        system_page_size = 4096;
    }
    if (system_page_size <= 0 || (system_page_size & (system_page_size - 1)) != 0)
    {
        fprintf(stderr, "Warning: Invalid page size %ld for alignment. Using default 4096.\n", system_page_size);
        system_page_size = 4096;
    }

    char *buffer = (char *)align_alloc(buffer_size, system_page_size);
    if (buffer == NULL)
    {
        close(fd_in);
        exit(EXIT_FAILURE);
    }

    ssize_t bytes_read;
    ssize_t bytes_written_total;
    ssize_t bytes_written_one;

    while ((bytes_read = read(fd_in, buffer, buffer_size)) > 0)
    {
        bytes_written_total = 0;
        while (bytes_written_total < bytes_read)
        {
            bytes_written_one = write(STDOUT_FILENO, buffer + bytes_written_total, bytes_read - bytes_written_total);
            if (bytes_written_one == -1)
            {
                if (errno == EINTR)
                    continue;
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

    // It can be beneficial to advise POSIX_FADV_DONTNEED after reading,
    // especially if the file is large and not expected to be accessed again soon.
    // This tells the kernel it can free pages associated with this file from cache.
    // For `cat`, this might be useful.

    align_free(buffer);
    if (close(fd_in) == -1)
    {
        perror("Error closing input file");
        if (bytes_read != -1)
            exit(EXIT_FAILURE);
    }

    return (bytes_read == -1) ? EXIT_FAILURE : EXIT_SUCCESS;
}