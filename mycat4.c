#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>   // read, write, close, sysconf, STDIN_FILENO, STDOUT_FILENO
#include <fcntl.h>    // open, O_RDONLY
#include <errno.h>    // errno
#include <stdint.h>   // For uintptr_t
#include <sys/stat.h> // For fstat, struct stat

// 函数：决定IO操作的块大小（缓冲区大小）
// 综合考虑页大小和文件系统块大小
// 参数: fd - 打开文件的文件描述符
long io_blocksize(int fd)
{
    // 1. 获取系统的内存页大小
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size == -1)
    {
        perror("sysconf(_SC_PAGESIZE) failed");
        fprintf(stderr, "Warning: sysconf(_SC_PAGESIZE) failed. Using default 4096 for page size and buffer size.\n");
        return 4096; // sysconf 失败，返回一个默认的合理值作为缓冲区大小
    }

    struct stat file_stat;
    // 2. 获取文件的元数据，包括 st_blksize
    if (fstat(fd, &file_stat) == -1)
    {
        perror("fstat failed when trying to get st_blksize");
        fprintf(stderr, "Warning: fstat failed. Using page size %ld as buffer size.\n", page_size);
        return page_size; // fstat 失败，回退到仅使用页大小作为缓冲区大小
    }

    blksize_t fs_blk_size = file_stat.st_blksize;

    // 3. 处理注意事项2：检查虚假或无效的文件系统块大小
    if (fs_blk_size <= 0)
    {
        fprintf(stderr, "Warning: Invalid st_blksize (%ld) reported by fstat. Using page size %ld as buffer size.\n", (long)fs_blk_size, page_size);
        return page_size; // 无效的 fs_blk_size，回退到仅使用页大小
    }
    // 可选的更严格检查：确保 fs_blk_size 是2的整数次幂
    // if ((fs_blk_size > 0) && (fs_blk_size & (fs_blk_size - 1)) != 0) {
    //     fprintf(stderr, "Warning: st_blksize (%ld) is not a power of two. Using page size %ld as buffer size.\n", (long)fs_blk_size, page_size);
    //     return page_size;
    // }

    // 4. "既考虑到内存页大小也考虑到文件系统的块大小"
    // 策略：取两者中的较大者作为最终的缓冲区大小。
    long chosen_buffer_size = page_size;
    if ((long)fs_blk_size > page_size)
    {
        chosen_buffer_size = (long)fs_blk_size;
    }
    // printf("Debug: Page size: %ld, FS block size: %ld, Chosen buffer size: %ld\n", page_size, (long)fs_blk_size, chosen_buffer_size);
    return chosen_buffer_size;
}

// 分配对齐的内存 (与 mycat3 相同)
void *align_alloc(size_t size, size_t alignment)
{
    if (alignment == 0)
        alignment = 1;
    if ((alignment & (alignment - 1)) != 0 && alignment != 1)
    {
        fprintf(stderr, "Alignment for align_alloc must be a power of two.\n");
        return NULL;
    }
    void *original_ptr = NULL;
    size_t total_size = size + alignment - 1 + sizeof(void *);
    original_ptr = malloc(total_size);
    if (original_ptr == NULL)
    {
        return NULL;
    }
    uintptr_t aligned_addr_val = ((uintptr_t)original_ptr + sizeof(void *) + alignment - 1) & ~(alignment - 1);
    void *aligned_ptr = (void *)aligned_addr_val;
    *((void **)((uintptr_t)aligned_ptr - sizeof(void *))) = original_ptr;
    return aligned_ptr;
}

// 释放对齐的内存 (与 mycat3 相同)
void align_free(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }
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

    // 调用 io_blocksize 获取最终的缓冲区大小
    long buffer_size = io_blocksize(fd_in);

    // 获取系统页大小，用于 align_alloc 的对齐参数
    long system_page_size = sysconf(_SC_PAGESIZE);
    if (system_page_size == -1)
    { // 再次获取以确保对齐参数有效
        perror("sysconf(_SC_PAGESIZE) failed for alignment");
        system_page_size = 4096; // Fallback
        fprintf(stderr, "Warning: sysconf(_SC_PAGESIZE) failed for alignment. Using default 4096 for alignment.\n");
    }

    // 缓冲区仍然按内存页对齐。
    // align_alloc 的第一个参数是实际分配的大小 (buffer_size)，第二个是对齐基准 (system_page_size)
    char *buffer = (char *)align_alloc(buffer_size, system_page_size);
    if (buffer == NULL)
    {
        perror("Failed to allocate aligned buffer");
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
                {
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
    }

    align_free(buffer);
    if (close(fd_in) == -1)
    {
        perror("Error closing input file");
    }

    return (bytes_read == -1) ? EXIT_FAILURE : EXIT_SUCCESS;
}