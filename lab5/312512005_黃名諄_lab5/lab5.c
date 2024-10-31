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

int ser_socket_fd, acc_socket_fd;
void handler(int signum)
{   
    while(waitpid(-1, NULL, WNOHANG) > 0);
}
void sigint_handler(int signum)
{   
    close(ser_socket_fd);
}

void handle_client(int acc_fd)
{   
    //Redirect standard output to the client socket
    if(dup2(acc_fd, STDOUT_FILENO) == -1)
    {
        perror("dup2 failed");
        exit(EXIT_FAILURE);
    }
    // Execute the sl process
    if(execlp("sl", "sl", "-l", NULL) == -1)
    {
        perror("execlp failed");
        exit(EXIT_FAILURE);
    }
    close(acc_fd);
}

int main(int argc, char *argv[])
{   
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if(argc != 2) 
    {
        fprintf(stderr, "Usage: ./lab5 <port>");
        exit(EXIT_FAILURE);
    }

    //prevent zombie processes
    signal(SIGCHLD, handler);
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
    if (listen(ser_socket_fd, 5) == -1)    //max. connection in waiting queue is 5
    {
        perror("socket listen failed");
        exit(EXIT_FAILURE);
    }

    while(1)
    {   
        //accept client connection
        acc_socket_fd = accept(ser_socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if(acc_socket_fd == -1)
        {
            perror("socket accept failed");
            continue;
        }

        //create child process
        pid_t child_pid = fork();
        if(child_pid == 0)  //child process
        {
            handle_client(acc_socket_fd);
            exit(0);         //child process exit after handling the client request
        }
        else if(child_pid > 0) //parent process
        {   
            close(acc_socket_fd);
            printf("Train ID: %d\n", child_pid);
        }
        else //fork failed will return -1
        {
            perror("fork failed");
            exit(EXIT_FAILURE);
        }
    }

    close(ser_socket_fd);
    return 0;

}