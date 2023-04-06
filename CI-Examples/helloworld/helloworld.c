#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main()
{  
    int fd;
    char buff[1024];
    //char path[] = "/home/svenkat9/dev/gramine/CI-Examples/helloworld/helloworld.txt";
    char path[] = "./helloworld.txt";
    char text[] ="Knowledge of the various open, read, and write functions is essential because we will always need them to store or dispose of information stored in files. The C language has two basic functions you can use to open files, open() and fopen(), each with different calling methods.";
    fd = open(path, O_CREAT | O_WRONLY, 0664);
    if (fd < 0)
    {
        printf("open w failed");
    }
    write( fd, &text, strlen(text));
    close( fd );
    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        printf("open r failed");
    }
    read(fd, buff, strlen(text));
    close(fd);
    printf("\n\n%s\n\n",buff);
}
