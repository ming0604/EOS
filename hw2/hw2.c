#include <stdio.h>      // fprintf(), perror()
#include <stdlib.h>     // exit()
#include <string.h>     // memset()
#include <signal.h>    // signal()
#include <unistd.h>    
#include <sys/types.h> 
#include <sys/wait.h>   // waitpid()
#include <sys/socket.h> // socket(), bind(), listen(), accept()
#include <netinet/in.h> // struct sockaddr_in
#include <arpa/inet.h>  // htons()
#include <stdint.h> //uint16_t

#define BUF_SIZE 256

typedef struct food
{
    char name[20];
    int price;
}food;

typedef struct shop
{   
    char name[20];
    int distance;
    food items[2];
}shop;

//setup the data of the shops   
shop shops[3] = {
    {"Dessert shop", 3, {{"cookie", 60}, {"cake", 80}}},        
    {"Beverage shop", 5, {{"tea", 40}, {"boba", 70}}},          
    {"Diner", 8, {{"fried rice", 120}, {"egg-drop soup", 50}}}  
};

int ser_socket_fd, acc_socket_fd;
void sigint_handler(int signum)
{   
    close(ser_socket_fd);
}


void handle_client(int client_fd)
{
    char recv_buf[BUF_SIZE];
    //define the commands that client can use
    char commands[][20]=
    {
        "shop list",
        "order",
        "confirm",
        "cancel",
    };
    //flush the buffer
    memset(recv_buf, 0, sizeof(recv_buf));
    
    while(1)
    {   
        //receive the command from client
        recv(client_fd, recv_buf, sizeof(recv_buf), 0);
        
        //shop list
        if(strcmp(recv_buf, commands[0]) == 0)
        {

        }
        //order
        else if(strcmp(recv_buf, commands[1]) == 0)
        {

        }
        //confirm
        else if(strcmp(recv_buf, commands[2]) == 0)
        {

        }
        //cancel
        else if(strcmp(recv_buf, commands[3]) == 0)
        {

        }
        else
        {
            printf("invalid command from client\n");
        }


    }

}

int main(int argc, char *argv[])
{   
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if(argc != 2) 
    {
        fprintf(stderr, "Usage: ./hw2 <port>");
        exit(EXIT_FAILURE);
    }

    //close socket when catching SIGINT signal
    signal(SIGINT, sigint_handler);

    //create server socket
    if ((ser_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) 
    {
        perror("create socket failed");
        exit(EXIT_FAILURE);
    }
    
    //Force using socket address already in use
    int yes = 1;
    if (setsockopt(ser_socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) 
    {
        perror("setsockopt failed");
        close(ser_socket_fd);
        exit(EXIT_FAILURE);
    }
    //define server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;   //do not assign the ip
    server_addr.sin_port = htons((uint16_t)atoi(argv[1]));

    //bind the socket to the server address
    if (bind(ser_socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) 
    {
        perror("bind socket failed");
        exit(EXIT_FAILURE);
    }

    //listen for incoming connections
    if (listen(ser_socket_fd, 1) == -1)    //max. connection in waiting queue is 1
    {
        perror("socket listen failed");
        exit(EXIT_FAILURE);
    }

    while(1)
    {   
        //accept client connection(will block server until accept a client connection)
        acc_socket_fd = accept(ser_socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if(acc_socket_fd == -1)
        {
            perror("socket accept failed");
            continue;
        }
        printf("Client connected.\n");

        handle_client(acc_socket_fd);

    }

    close(ser_socket_fd);
    return 0;

}