#include <stdio.h>      // fprintf(), perror()
#include <stdlib.h>     // exit()
#include <string.h>     // strlen()
#include <fcntl.h>     // open()
#include <unistd.h>    // read(), write(), close() sleep()


int main(int argc, char *argv[])
{   
    char* number_binary[10] =
    {
        "0000", // 0
        "0001", // 1
        "0010", // 2
        "0011", // 3
        "0100", // 4
        "0101", // 5
        "0110", // 6
        "0111", // 7
        "1000", // 8
        "1001", // 9
    };

    int fd;
    if(argc!= 2)
    {
        fprintf(stderr, "Usage: ./writer <student ID>");
        exit(EXIT_FAILURE);
    }

    if((fd = open("/dev/etx_device", O_RDWR)) < 0) {
        perror("/dev/etx_device");
        exit(EXIT_FAILURE);
    }

    int len = strlen(argv[1]);
    int num = 0;
    for(int i=0; i<len; i++)
    {   
        num = argv[1][i] - '0';
        if(write(fd, number_binary[num], 4) == -1)
        {
            perror("write()");
            exit(EXIT_FAILURE);
        }
        sleep(1);
    }

    close(fd);
}