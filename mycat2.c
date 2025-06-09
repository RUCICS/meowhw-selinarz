#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// 函数：获取合适的IO块大小（这里是页大小）
long io_blocksize()
{
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size == -1)
    {
        perror("sysconf_SC_PAGESIZE failed");
        // Fallback to a common page size if sysconf fails
        fprintf(stderr, "Warning: sysconf(_SC_PAGESIZE) failed. Using default 4096.\n");
        return 4096;
    }
    return page_size;
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

    long buffer_size = io_blocksize();
    char *buffer = (char *)malloc(buffer_size);
    if (buffer == NULL)
    {
        perror("Failed to allocate buffer");
        close(fd_in);
        exit(EXIT_FAILURE);
    }

    ssize_t bytes_read;
    ssize_t bytes_written_total;
    ssize_t bytes_written_이번; // "이번" means "current" or "this time" in Korean, good variable name for current write

    while ((bytes_read = read(fd_in, buffer, buffer_size)) > 0)
    {
        bytes_written_total = 0;
        while (bytes_written_total < bytes_read)
        {
            bytes_written_이번 = write(STDOUT_FILENO, buffer + bytes_written_total, bytes_read - bytes_written_total);
            if (bytes_written_이번 == -1)
            {
                perror("Error writing to stdout");
                free(buffer);
                close(fd_in);
                exit(EXIT_FAILURE);
            }
            bytes_written_total += bytes_written_이번;
        }
    }

    if (bytes_read == -1)
    {
        perror("Error reading from input file");
    }

    free(buffer);
    if (close(fd_in) == -1)
    {
        perror("Error closing input file");
        // Even if close fails, we should have already processed or reported read/write errors
        exit(EXIT_FAILURE); // Or just return EXIT_FAILURE if read error already caused exit
    }

    return (bytes_read == -1) ? EXIT_FAILURE : EXIT_SUCCESS;
}