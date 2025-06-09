#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

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

    char ch;
    ssize_t bytes_read;
    ssize_t bytes_written;

    while ((bytes_read = read(fd_in, &ch, 1)) > 0)
    {
        bytes_written = write(STDOUT_FILENO, &ch, 1);
        if (bytes_written != 1)
        {
            perror("Error writing to stdout");
            close(fd_in);
            exit(EXIT_FAILURE);
        }
    }

    if (bytes_read == -1)
    {
        perror("Error reading from input file");
        close(fd_in);
        exit(EXIT_FAILURE);
    }

    if (close(fd_in) == -1)
    {
        perror("Error closing input file");
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}