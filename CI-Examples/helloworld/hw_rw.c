#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main()
{  
    int fd;
    int i = 1048576;
    char buff[4097];
    char path[] = "./helloworld.txt";
    char text[] = "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";
    int ret;
    fd = open(path, O_CREAT | O_WRONLY, 0664);
    if (fd < 0)
    {
        printf("open w failed");
    }
    while (i > 0) {
        ret = write( fd, &text, strlen(text));
        if (ret < 0)
            break;
        i--;
    }
    close( fd );
    if (i != 0) {
        printf("Write error!\n");
        exit(-1);
    }
    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        printf("open r failed");
    }

    i = 1048576;
    while (i > 0) {
        ret = read(fd, buff, strlen(text));
        if (ret < 0)
            break;
        i--;
    }
    close( fd );
    if (i != 0) {
        printf("Read error!\n");
        exit(-1);
    }
}
