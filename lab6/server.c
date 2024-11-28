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
#include <pthread.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <errno.h>

#define BUF_SIZE 256
#define SEM_KEY 6666

int ser_socket_fd;
int s_id; 
int total_money = 0;

void sigint_handler(int signum)
{   
    close(ser_socket_fd);
    if (semctl(s_id, 0, IPC_RMID, 0) < 0) 
    {
        perror("Failed to remove semaphore");
        exit(EXIT_FAILURE);
    }
    exit(0);
}

/* P () acquire semaphore - returns 0 if OK; -1 if there was a problem */
int P(int s)
{
    struct sembuf sop; /* the operation parameters */
    sop.sem_num = 0;
    /* access the 1st (and only) sem in the array */
    sop.sem_op = -1;
    /* wait..*/
    sop.sem_flg = 0;
    /* no special options needed */
    if (semop (s, &sop, 1) < 0)
    {
        fprintf(stderr,"P(): semop failed: %s\n",strerror(errno));
        return -1;
    } 
    else 
    {   
        return 0;
    }
}
/* V() release semaphore - returns 0 if OK; -1 if there was a problem */
int V(int s)
{
    struct sembuf sop; /* the operation parameters */
    sop.sem_num = 0;
    /* the 1st (and only) sem in the array */
    sop.sem_op = 1;
    /* signal */
    sop.sem_flg = 0;
    /* no special options needed */
    if (semop(s, &sop, 1) < 0) 
    {
        fprintf(stderr,"V(): semop failed: %s\n",strerror(errno));
        return -1;
    } 
    else 
    {
        return 0;
    }
}

void* handle_client(void* client_fd_ptr)
{   
    int client_fd = *((int*)client_fd_ptr);
    char recv_buf[BUF_SIZE];
    char commands[][20]=
    {
        "deposit",
        "withdraw"
    };
    char command[20];
    int amount;
    int sop_rt;

    free(client_fd_ptr); //free the memory allocated for client_fd

    while(1)
    {
        //flush the buffer
        memset(recv_buf, 0, sizeof(recv_buf));
        //receive the msg from client
        int recv_rt = recv(client_fd, recv_buf, sizeof(recv_buf), 0);
        if( recv_rt == -1)
        {  
            perror("recv failed");
            close(client_fd);
            exit(EXIT_FAILURE);
        }
        //client closed the connection (recv() returns 0)
        else if( recv_rt == 0)
        {
            break;
        }

        //get the command and amount from client's msg
        sscanf(recv_buf, "%s %d", command, &amount);
        //handle command
        if(strcmp(command,commands[0]) == 0)    //deposit
        {   
            //acquire semaphore to enter the critical section
            sop_rt = P(s_id);
            if(sop_rt == -1)
            {
                printf("P() failed\n");
                close(client_fd);
                exit(EXIT_FAILURE);
            }
            //critical section
            total_money += amount;
            printf("After deposit: %d\n", total_money);
            //release semaphore after exiting the critical section
            sop_rt = V(s_id);
            if(sop_rt == -1)
            {
                printf("V() failed\n");
                close(client_fd);
                exit(EXIT_FAILURE);
            }

            
        }
        else if(strcmp(command,commands[1]) == 0)   //withdraw
        {
            //acquire semaphore to enter the critical section
            sop_rt = P(s_id);
            if(sop_rt == -1)
            {
                printf("P() failed\n");
                close(client_fd);
                exit(EXIT_FAILURE);
            }
            //critical section
            total_money -= amount;
            printf("After withdraw: %d\n", total_money);
            //release semaphore after exiting the critical section
            sop_rt = V(s_id);
            if(sop_rt == -1)
            {
                printf("V() failed\n");
                close(client_fd);
                exit(EXIT_FAILURE);
            }
               
        }
        else
        {
            printf("invalid command from client\n");
        }
    }

    //close the client socket
    close(client_fd);
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{   
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if(argc != 2) 
    {
        fprintf(stderr, "Usage: ./server <port>");
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
    if (listen(ser_socket_fd, 5) == -1)    //max. connection in waiting queue is 5
    {
        perror("socket listen failed");
        exit(EXIT_FAILURE);
    }

    //create 1 semaphore
    s_id = semget(SEM_KEY, 1, IPC_CREAT | IPC_EXCL | 666);
    if (s_id < 0)
    {
        fprintf(stderr,"Creation of semaphore key %d failed: %s\n",SEM_KEY, strerror(errno));
        exit(EXIT_FAILURE);
    }
    printf("Semaphore of key %d created\n", SEM_KEY);

    //initialize semaphore to 1
    int init_val = 1;
    if (semctl(s_id, 0, SETVAL, init_val) < 0)
    {
        fprintf(stderr,"Unable to initialize semaphore: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    printf("Semaphore of key %d has been initialized to %d\n", SEM_KEY, init_val);
    

    while(1)
    {   
        int* acc_socket_fd = (int*)malloc(sizeof(int));  //in order to let every thread use its own client fd (different memory location)
        //accept client connection
        *acc_socket_fd = accept(ser_socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if(*acc_socket_fd == -1)
        {
            perror("socket accept failed");
            free(acc_socket_fd);  //free the memory allocated for acc_socket_fd
            continue;
        }
        //create a thread to handle the client
        pthread_t client_thread;
        if(pthread_create(&client_thread, NULL, handle_client, (void*)acc_socket_fd) != 0)
        {
            perror("pthread_create failed");
            close(*acc_socket_fd);
            free(acc_socket_fd);  //free the memory allocated for acc_socket_fd
            exit(EXIT_FAILURE);
        }
        //detach the thread from the main thread 
        if (pthread_detach(client_thread) != 0) 
        {
            perror("pthread_detach failed");
            close(*acc_socket_fd);
            free(acc_socket_fd); // free the memory allocated for acc_socket_fd
            exit(EXIT_FAILURE);
        } 
    }

    //destroy the semaphore
    if (semctl(s_id, 0, IPC_RMID, 0) < 0) 
    {
        perror("Failed to remove semaphore");
        close(ser_socket_fd);
        exit(EXIT_FAILURE);
    }

    close(ser_socket_fd);
    return 0;

}