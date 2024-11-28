#include <stdio.h>      // fprintf(), perror()
#include <stdlib.h>     // exit()
#include <string.h>     // memset()
#include <signal.h>    // signal()
#include <unistd.h>    
#include <sys/types.h> 
#include <sys/socket.h> // socket(), bind(), listen(), accept()
#include <netinet/in.h> // struct sockaddr_in
#include <arpa/inet.h>  // htons()
#include <stdint.h> //uint16_t

#define BUF_SIZE 256

int client_socket_fd;
void sigint_handler(int signum)
{   
    close(client_socket_fd);
}

int main(int argc, char *argv[])
{   
    struct sockaddr_in server_addr;

    if(argc != 6) 
    {
        fprintf(stderr, "Usage: ./client <ip> <port> <deposit/withdraw> <amount> <times>");
        exit(EXIT_FAILURE);
    }
    
    int amount = atoi(argv[4]);
    int times = atoi(argv[5]);

    //close socket when catching SIGINT signal
    signal(SIGINT, sigint_handler);

    //create client socket
    if ((client_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) 
    {
        perror("create socket failed");
        exit(EXIT_FAILURE);
    }
    
    //Force using socket address already in use
    int yes = 1;
    if (setsockopt(client_socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) 
    {
        perror("setsockopt failed");
        close(client_socket_fd);
        exit(EXIT_FAILURE);
    }
    //define server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    server_addr.sin_port = htons((uint16_t)atoi(argv[2]));

    //connect to server
    if(connect(client_socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("connect failed");
        close(client_socket_fd);
        exit(EXIT_FAILURE);
    }

    //send request to client
    char send_buf[BUF_SIZE];
    memset(send_buf, 0, sizeof(send_buf));
    snprintf(send_buf, sizeof(send_buf), "%s %d", argv[3], amount);
    for(int i=0; i<times; i++)
    {   
        if(send(client_socket_fd, send_buf, sizeof(send_buf), 0) == -1)
        {
            perror("send failed");
            close(client_socket_fd);
            exit(EXIT_FAILURE);
        }
        
    }

    //close client
    close(client_socket_fd);
    return 0;
}


