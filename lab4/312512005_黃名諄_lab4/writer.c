#include <stdio.h>      // fprintf(), perror()
#include <stdlib.h>     // exit()
#include <string.h>     // strlen()
#include <fcntl.h>     // open()
#include <unistd.h>    // read(), write(), close() sleep()


int main(int argc, char *argv[])
{   
    int fd;
    if(argc!= 2)
    {
        fprintf(stderr, "Usage: ./writer <name>");
        exit(EXIT_FAILURE);
    }

    if((fd = open("/dev/mydev", O_RDWR)) < 0) {
        perror("/dev/mydev");
        exit(EXIT_FAILURE);
    }

    int len = strlen(argv[1]);
    for(int i=0; i<len; i++)
    {
        if(write(fd, argv[1]+i, 1) == -1)
        {
            perror("write()");
            exit(EXIT_FAILURE);
        }
        sleep(1);
    }

    close(fd);
}