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
#include <stdbool.h> //true false
#include <pthread.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <errno.h>

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
    {"Diner", 8, {{"fried-rice", 120}, {"Egg-drop-soup", 50}}}  
};
//how many shops
int shops_num = sizeof(shops)/sizeof(shop);
//socket fd
int ser_socket_fd, acc_socket_fd;
//record 2 Food delivery rider's current remaining delivery time
int food_delivery_rider_time[2] = {0,0};
int rider_num = sizeof(food_delivery_rider_time)/sizeof(int);
//define mutex
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;




void sigint_handler(int signum)
{   
    close(ser_socket_fd);
    pthread_mutex_destroy(&mutex);
    exit(0);
}

void strip_newline(char* str) {
    char* pos;
    pos = strchr(str, '\n');
    if (pos != NULL) 
    {
        *pos = '\0';
    }
}

void* food_delivery_countdown(void* arg)
{
    while(1)
    {   
        sleep(1);
        //lock mutex
        pthread_mutex_lock(&mutex);
        //if any rider's remaining delivery time > 0, decrease the remaining time by 1
        for(int i=0; i<rider_num; i++)
        {
            if(food_delivery_rider_time[i] > 0)
            {
                food_delivery_rider_time[i]--;
            }
        }
        //unlock mutex
        pthread_mutex_unlock(&mutex);
        //sleep 1 second
    }
    pthread_exit(NULL);
}

void send_shop_list(int client_fd)
{
    char send_buf[BUF_SIZE] = "";
    //create shop list
    for(int i=0; i<shops_num; i++)
    {   
        char temp[BUF_SIZE];
        //create shop data string
        snprintf(temp, sizeof(temp), "%s:%dkm\n- ", shops[i].name, shops[i].distance);

        //how many types of foods does this restaurant offer
        int food_num = sizeof(shops[i].items)/sizeof(food);
        //create foods string of this shop
        for(int j=0; j<food_num; j++)
        {   
            char foods_temp[BUF_SIZE];
            if(j == food_num-1)
            {
                snprintf(foods_temp, sizeof(foods_temp), "%s:$%d\n",shops[i].items[j].name, shops[i].items[j].price);
            }
            else
            {
                snprintf(foods_temp, sizeof(foods_temp), "%s:$%d|",shops[i].items[j].name, shops[i].items[j].price);
            }
            //concatenate shop string with food string
            strcat(temp, foods_temp);
        }
        //concatenate shop string with final send string
        strcat(send_buf, temp);
    }
    send(client_fd, send_buf, sizeof(send_buf),0);
}

int find_order_shop(char* order_food)
{   

    for(int i=0; i<shops_num; i++)
    {   
        int food_num = sizeof(shops[i].items)/sizeof(food);
        for(int j=0; j<food_num; j++)
        {   
            //find which shop has this order's food 
            if(strcmp(order_food, shops[i].items[j].name) == 0)
            {
                return i;
            }
        }
    }
    return -1;
}

int find_order_food_index(shop order_shop, int food_num, char* order_food)
{
    for(int i=0; i<food_num; i++)
    {   
        //find the index of the order's food
        if(strcmp(order_food, order_shop.items[i].name) == 0)
        {
            return i;
        }
    }
    return -1;
}

void send_order(int client_fd, shop order_shop,int* food_order_arr,int food_num)
{
    char send_buf[BUF_SIZE] = "";

    //create the order record string
    if(food_order_arr[0] == 0) 
    {
        snprintf(send_buf, sizeof(send_buf), "%s %d\n",order_shop.items[1].name,food_order_arr[1]);
    }
    else if(food_order_arr[1] == 0)
    {
        snprintf(send_buf, sizeof(send_buf), "%s %d\n",order_shop.items[0].name,food_order_arr[0]);
    }
    else    //both food have been ordered
    {
        
        for(int i=0; i<food_num; i++)
        {   
            char temp[BUF_SIZE];
            if(i == food_num-1)
            {
                snprintf(temp, sizeof(temp), "%s %d\n",order_shop.items[i].name,food_order_arr[i]);
            }
            else
            {
                snprintf(temp, sizeof(temp), "%s %d|",order_shop.items[i].name,food_order_arr[i]);
            }
            //concatenate 
            strcat(send_buf, temp);
        }
    }
    send(client_fd, send_buf, sizeof(send_buf), 0);
}


void* handle_client(void* client_fd_ptr)
{
    int client_fd = *((int*)client_fd_ptr);
    free(client_fd_ptr); //free the memory allocated for client_fd_ptr
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
    bool have_ordered = false;
    int order_shop_index = -1;
    int food_order_arr[2] = {0};            //set a static array with food num
    shop order_shop;
    int food_num = 0;

    while(1)
    {   
        //receive the command from client
        if(recv(client_fd, recv_buf, sizeof(recv_buf), 0) == -1)
        {  
            perror("recv failed");
            close(client_fd);
            exit(EXIT_FAILURE);
        }
        //strip newline character \n from the end of the command
        strip_newline(recv_buf);
        //shop list
        if(strcmp(recv_buf, commands[0]) == 0)
        {
            printf("client's command: %s\n",commands[0]);
            send_shop_list(client_fd);
        }   
        //order
        else if(strncmp(recv_buf, commands[1], 5) == 0)
        {
            char order_food[20];
            int order_num =0 ;
           
            int food_index = -1;
            //get order's food name and how many of this food
            sscanf(recv_buf, "%*s %s %d", order_food, &order_num);
            printf("client's command: %s %s %d\n", commands[1], order_food, order_num);

            if(have_ordered)//after first order
            {
                food_index = find_order_food_index(order_shop, food_num, order_food);
                if(food_index == -1)
                {
                    printf("Can't find this food in the current shop\n");
                    send_order(client_fd,order_shop,food_order_arr,food_num);
                    continue;
                }
                else
                {
                    food_order_arr[food_index]+=order_num;
                    send_order(client_fd,order_shop,food_order_arr,food_num);
                }
            }
            else  //first order
            {
                have_ordered = true;
                order_shop_index = find_order_shop(order_food);
                if(order_shop_index == -1)
                {
                    printf("Can't find this food in any shop\n");
                }
                //set the shop which the client can only order from in the future
                order_shop = shops[order_shop_index];
                food_num = sizeof(order_shop.items)/sizeof(food);
                //find the index of the food client want to order 
                food_index = find_order_food_index(order_shop, food_num, order_food);
                //update order record
                food_order_arr[food_index]+=order_num;
                //send order record to client
                send_order(client_fd,order_shop,food_order_arr,food_num);
            }
        }
        //confirm
        else if(strcmp(recv_buf, commands[2]) == 0)
        {   
            printf("client's command: %s\n",commands[2]);
            if(have_ordered)   
            {
                int distance = shops[order_shop_index].distance;
                int Estimated_total_waiting_time;
                int final_total_waiting_time;
                int remaining_delivery_time;
                int rider_index;

                //lock mutex
                pthread_mutex_lock(&mutex);
                //choose the delivery rider whose waiting time is shorter 
                if(food_delivery_rider_time[0] < food_delivery_rider_time[1])    //rider1 has shorter remaining delivery time
                {   
                    remaining_delivery_time = food_delivery_rider_time[0];
                    rider_index = 0;
                }
                else                                                             //rider2 has shorter remaining delivery time
                {
                    remaining_delivery_time = food_delivery_rider_time[1]; 
                    rider_index = 1;
                }
                //unlock mutex
                pthread_mutex_unlock(&mutex);

                //total waiting time eqauls the time the rider haven't deliveried + the delivery time of my order
                Estimated_total_waiting_time = remaining_delivery_time + distance;

                if(Estimated_total_waiting_time > 30)
                {   
                    //send a message to client to double check
                    char send_buf_1[BUF_SIZE] = "Your delivery will take a long time, do you want to wait?\n";
                    send(client_fd, send_buf_1, sizeof(send_buf_1), 0);
                    
                    //receive the response from client
                    char recv_buf_waiting_check[BUF_SIZE] = "";
                    if(recv(client_fd, recv_buf_waiting_check, sizeof(recv_buf_waiting_check), 0) == -1)
                    {  
                        perror("recv failed");
                        close(client_fd);
                        exit(EXIT_FAILURE);
                    }
                    //strip newline character \n
                    strip_newline(recv_buf_waiting_check);

                    if(strcmp(recv_buf_waiting_check,"Yes") == 0){}    //can continue to allocate this order to the rider
                    else if(strcmp(recv_buf_waiting_check,"No") == 0)
                    {   
                        printf("client don't want to wait. Order canceled.\n");
                        break;
                    }
                    else
                    {
                        printf("Invalid command of order waiting time check when confirming: %s \n",recv_buf_waiting_check);
                        break;
                    }
                }

                //allocate this order to food delivery rider and wait for delivery to complete
                //lock mutex
                pthread_mutex_lock(&mutex);
                //the actual waiting time of this order
                final_total_waiting_time = food_delivery_rider_time[rider_index] + distance;
                //update the rider's remaining delivery time
                food_delivery_rider_time[rider_index] += distance;
                //unlock mutex
                pthread_mutex_unlock(&mutex);
                //send a message to client to wait
                char send_buf_2[BUF_SIZE] = "Please wait a few minutes...\n";
                send(client_fd, send_buf_2, sizeof(send_buf_2), 0);
                //sleep for the wainting time
                sleep(final_total_waiting_time);

                //count total price and sent to client
                int total_price = 0;
                for(int i=0; i<food_num; i++)
                {   
                    total_price += order_shop.items[i].price * food_order_arr[i];
                }
                printf("Total price for order: %d\n", total_price);
                char send_buf_3[BUF_SIZE];
                snprintf(send_buf_3, sizeof(send_buf_3), "Delivery has arrived and you need to pay %d$\n", total_price);
                send(client_fd, send_buf_3, sizeof(send_buf_3), 0);
                break;
            }
            else        //there is no order
            {   
                printf("No order has been made\n");
                char send_buf[BUF_SIZE] = "Please order some meals\n";
                send(client_fd, send_buf, sizeof(send_buf), 0);
            }
        }
        //cancel
        else if(strcmp(recv_buf, commands[3]) == 0)
        {   
            printf("client's command: %s\n",commands[3]);
            break;
        }
        else
        {
            printf("invalid command from client: %s\n",recv_buf);
        }
    }
    //close the client socket
    close(client_fd);
    pthread_exit(NULL);

}

int main(int argc, char* argv[])
{
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if(argc != 2) 
    {
        fprintf(stderr, "Usage: ./hw3 <port>");
        exit(EXIT_FAILURE);
    }

    //close socket when catching SIGINT signal
    signal(SIGINT, sigint_handler);
    //create food delivery time coundown thread
    pthread_t food_delivery_countdown_thread;
    if(pthread_create(&food_delivery_countdown_thread, NULL, food_delivery_countdown, NULL) != 0)
    {
        perror("pthread_create failed");
        exit(EXIT_FAILURE);
    }
    //detach the thread from the main thread 
    if (pthread_detach(food_delivery_countdown_thread) != 0) 
    {
        perror("pthread_detach failed");
        exit(EXIT_FAILURE);
    } 


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
    if (listen(ser_socket_fd, 10) == -1)    //max. connection in waiting queue is 10
    {
        perror("socket listen failed");
        exit(EXIT_FAILURE);
    }

    while(1)
    {   
        int* acc_socket_fd = (int*)malloc(sizeof(int));  //in order to let every thread use its own client fd (different memory location)
        //accept client connection(will block server until accept a client connection)
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

    close(ser_socket_fd);
    pthread_mutex_destroy(&mutex);
    return 0;

}