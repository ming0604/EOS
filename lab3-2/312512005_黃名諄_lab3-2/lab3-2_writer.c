#include <stdio.h>      // fprintf(), perror()
#include <stdlib.h>     // exit()
#include <string.h>     // strlen()
#include <fcntl.h>     // open()
#include <unistd.h>    // read(), write(), close() sleep()


int main(int argc, char *argv[])
{   
    char* seg_num[10] =
    {   
        //dp,a,b,c,d,e,f,g
        //dp = 0
        "01111110", // 0: a, b, c, d, e, f
        "00110000", // 1: b, c
        "01101101", // 2: a, b, d, e, g
        "01111001", // 3: a, b, c, d, g
        "00110011", // 4: b, c, f, g
        "01011011", // 5: a, c, d, f, g
        "01011111", // 6: a, c, d, e, f, g 
        "01110000", // 7: a, b, c
        "01111111", // 8: a, b, c, d, e, f, g
        "01111011"  // 9: a, b, c, d, f, g
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
        if(write(fd, seg_num[num], 8) == -1)
        {
            perror("write()");
            exit(EXIT_FAILURE);
        }
        sleep(1);
    }

    close(fd);
}