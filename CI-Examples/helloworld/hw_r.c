#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main()
{  
    int fd;
    int i = 1048576;
    char path[] = "./helloworld.txt";
    char buff[4097];
    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        printf("open r failed");
    }
    int ret;
    while (i > 0) {
        ret = read(fd, buff, 4096);
        if (ret < 0)
            break;
        i--;
    }
    close( fd );
    if (i != 0)
        printf("Read error!\n");
}
