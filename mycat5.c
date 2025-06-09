// mycat5.c
#define _GNU_SOURCE // For posix_fadvise if used in later versions, not strictly needed here but good for fcntl.h
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>   // read, write, close, sysconf, STDIN_FILENO, STDOUT_FILENO
#include <fcntl.h>    // open, O_RDONLY
#include <errno.h>    // errno
#include <stdint.h>   // For uintptr_t
#include <sys/stat.h> // For fstat, struct stat

// This is the "optimal" buffer size determined from experiments (e.g., dd tests)
// or a common well-performing size like 128KB used by GNU cat.
#define OPTIMAL_BUFFER_SIZE (256 * 1024) // 128 KB

// 函数：决定IO操作的块大小（缓冲区大小）
// 在这个版本中，我们直接使用一个实验确定的“最优”大小，
// 但仍然可以保留获取 page_size 和 fs_blk_size 的逻辑作为参考或备用。
long determine_io_blocksize(int fd)
{
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size == -1)
    {
        perror("sysconf(_SC_PAGESIZE) failed");
        // Fallback, though OPTIMAL_BUFFER_SIZE will likely override this
        page_size = 4096;
    }

    struct stat file_stat;
    blksize_t fs_blk_size = 0; // Default to 0 if fstat fails or not used

    if (fstat(fd, &file_stat) == -1)
    {
        perror("fstat failed when trying to get st_blksize");
    }
    else
    {
        fs_blk_size = file_stat.st_blksize;
        if (fs_blk_size <= 0)
        {
            fprintf(stderr, "Warning: Invalid st_blksize (%ld) reported by fstat. \n", (long)fs_blk_size);
            fs_blk_size = 0; // Treat as invalid
        }
    }

    // In mycat5, we prioritize the OPTIMAL_BUFFER_SIZE.
    // We could still compare it with fs_blk_size if fs_blk_size is significantly larger
    // and a power of two, but for simplicity, we'll use OPTIMAL_BUFFER_SIZE.
    // GNU cat uses 128K, but will use st_blksize if it's larger, valid, and a power of two.
    // Let's try to mimic that a bit:
    long chosen_buffer_size = OPTIMAL_BUFFER_SIZE;

    if (fs_blk_size > 0 && (fs_blk_size & (fs_blk_size - 1)) == 0)
    { // Check if power of two
        if ((long)fs_blk_size > chosen_buffer_size)
        {
            // If fs_blk_size is valid, a power of two, and larger than our optimal,
            // it might be beneficial to use it. This is similar to GNU cat's logic.
            chosen_buffer_size = (long)fs_blk_size;
            // fprintf(stdout, "Info: Using fs_blk_size (%ld) as it's larger than OPTIMAL_BUFFER_SIZE.\n", chosen_buffer_size);
        }
    }

    // The prompt implies setting a fixed multiple of base_buf_size,
    // or simply using the experimentally found optimal size.
    // For this task, let's assume OPTIMAL_BUFFER_SIZE is the primary choice.
    // The logic above is a slight refinement. If strictly following "fixed multiple A",
    // you'd calculate base_buf_size (e.g., MAX(page_size, st_blksize_from_mycat4))
    // and then multiply by A.
    // Let's simplify and just use OPTIMAL_BUFFER_SIZE directly for this task's core idea.
    // The task asks to "modify your function io_blocksize" using "experimental results".
    // So setting it to OPTIMAL_BUFFER_SIZE is a direct way.

    // printf("Debug: Page size: %ld, FS block size: %ld, Final Chosen buffer size for mycat5: %ld\n", page_size, (long)fs_blk_size, OPTIMAL_BUFFER_SIZE);
    return OPTIMAL_BUFFER_SIZE; // Directly use the experimentally derived optimal size
}

// 分配对齐的内存 (与 mycat3/4 相同)
void *align_alloc(size_t size, size_t alignment)
{
    if (alignment == 0)
        alignment = 1; // Should not happen with page_size
    // Ensure alignment is a power of two, which page_size should be
    if ((alignment & (alignment - 1)) != 0 && alignment != 1)
    {
        // This case should ideally not be hit if alignment is sysconf(_SC_PAGESIZE)
        fprintf(stderr, "Warning: Alignment %zu is not a power of two. Using default alignment 1.\n", alignment);
        alignment = sysconf(_SC_PAGESIZE); // Fallback to page size if invalid
        if (alignment <= 0 || (alignment & (alignment - 1)) != 0)
            alignment = 4096; // Further fallback
    }

    void *original_ptr;
    // We need space for the original pointer + the requested size + potential alignment padding
    size_t total_size = size + alignment - 1 + sizeof(void *);
    original_ptr = malloc(total_size);

    if (original_ptr == NULL)
    {
        perror("malloc failed in align_alloc");
        return NULL;
    }

    // Calculate the aligned pointer
    // 1. Add sizeof(void*) to leave space to store the original_ptr
    // 2. Add (alignment - 1) for the rounding trick
    // 3. Mask with ~(alignment - 1) to round down to the nearest alignment boundary
    uintptr_t aligned_addr_val = ((uintptr_t)original_ptr + sizeof(void *) + alignment - 1) & ~(alignment - 1);
    void *aligned_ptr = (void *)aligned_addr_val;

    // Store the original pointer just before the aligned block
    *((void **)((uintptr_t)aligned_ptr - sizeof(void *))) = original_ptr;

    return aligned_ptr;
}

// 释放对齐的内存 (与 mycat3/4 相同)
void align_free(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }
    // Retrieve the original pointer stored just before the aligned block
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

    long buffer_size = determine_io_blocksize(fd_in); // Use the new logic

    long system_page_size = sysconf(_SC_PAGESIZE);
    if (system_page_size == -1)
    {
        perror("sysconf(_SC_PAGESIZE) failed for alignment");
        system_page_size = 4096; // Fallback
        fprintf(stderr, "Warning: sysconf(_SC_PAGESIZE) failed for alignment. Using default 4096 for alignment.\n");
    }
    if (system_page_size <= 0 || (system_page_size & (system_page_size - 1)) != 0)
    { // Ensure power of two
        fprintf(stderr, "Warning: Invalid page size %ld for alignment. Using default 4096.\n", system_page_size);
        system_page_size = 4096;
    }

    char *buffer = (char *)align_alloc(buffer_size, system_page_size);
    if (buffer == NULL)
    {
        // align_alloc already prints perror
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
                { // Interrupted by signal, try again
                    continue;
                }
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
        // Don't exit yet, clean up first
    }

    align_free(buffer);
    if (close(fd_in) == -1)
    {
        perror("Error closing input file");
        // If read also failed, main will return failure. If only close fails, also failure.
        if (bytes_read != -1)
            exit(EXIT_FAILURE); // If read was fine, but close failed
    }

    return (bytes_read == -1) ? EXIT_FAILURE : EXIT_SUCCESS;
}