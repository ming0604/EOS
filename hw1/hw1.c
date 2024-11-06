#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

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



void print_shops(shop* shops, int size)
{    
    for(int i=0; i<size; i++)
    {
        printf("%s: %dkm\n",shops[i].name,shops[i].distance);
    }
    //flush the input buffer
    while (getchar() != '\n'); 
    printf("Press any key to return to the main menu...\n");
    // Wait for the user to press Enter
    getchar();
}

int choose_shop(shop* shops, int size)
{   
    int shop_choice;
    while(1)
    {
        printf("Please choose from 1~%d\n",size);
        for(int i=0; i<size; i++)
        {
            printf("%d. %s\n", i+1, shops[i].name);
        }
        scanf("%d",&shop_choice);

        if(shop_choice>0 && shop_choice<=size) //valid choice
        {
            return shop_choice;
        }
        else
        {
            printf("invalid choice, please try again\n");
            //flush the input buffer
            while (getchar() != '\n');
        } 
    }
}

void count_total_price(int* total_arr, int item_index, int item_price)
{   
    int count=0;
    printf("How many?\n");
    scanf("%d", &count);
    total_arr[item_index] += (item_price*count);
}

void* display_total_price(void *total_price_ptr)
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
    if((fd = open("/dev/etx_device", O_RDWR)) < 0) {
        perror("/dev/etx_device");
        exit(EXIT_FAILURE);
    }
    //get the total price
    int total_price = *((int*)total_price_ptr);
    //transform int to string 
    char total_price_str[50];
    sprintf(total_price_str, "%d", total_price);
    //write into the driver
    int len = strlen(total_price_str);
    int num = 0;
    for(int i=0; i<len; i++)
    {   
        num = total_price_str[i] - '0';
        if(write(fd, seg_num[num], 8) == -1)
        {
            perror("write()");
            exit(EXIT_FAILURE);
        }
        usleep(500000);  //sleep for 0.5s(=500000us)
    }
    close(fd);

    pthread_exit(NULL);
}

void* display_distance(void *distance_ptr)
{   
    int fd;
    if((fd = open("/dev/etx_device", O_RDWR)) < 0) {
        perror("/dev/etx_device");
        exit(EXIT_FAILURE);
    }
    //get the distance 
    int distance = *((int*)distance_ptr);
    //initialize the string distance
    char distance_str[12] = {0};

    for(int i=distance; i>=0; i--)
    {   
        sprintf(distance_str, "%d", i);
        if(write(fd, distance_str, 1) == -1)
        {
            perror("write()");
            exit(EXIT_FAILURE);
        }
        sleep(1);
    }
    close(fd);

    pthread_exit(NULL);
}


void order(shop* shops, int shop_index)
{   
    int items_num = sizeof(shops[shop_index].items)/sizeof(food); 
    food* food_items = shops[shop_index].items;
    int total_price[items_num]; //record the total price of each item 
    //initialize the total price of each item equals 0
    for(int i=0; i<items_num; i++)
    {
        total_price[i]=0;
    }

    while(1)
    {   
        //printf and scanf choices 
        printf("Please choose from 1~%d\n",items_num+2);
        
        for(int i=0; i<items_num; i++)
        {
            printf("%d. %s: %d\n", i+1, food_items[i].name, food_items[i].price);
        }
        printf("%d. confirm\n",items_num+1);
        printf("%d. cancel\n",items_num+2);
        int order_choice;
        scanf("%d", &order_choice);

        
        if(order_choice>0 && order_choice<=items_num)   //order the food items and count the total price
        {
            int index = order_choice-1;
            count_total_price(total_price, index, food_items[index].price);
        }
        else if(order_choice == items_num+1)    //confirm order
        {   
            //count the total price of all food items
            int final_total_price = 0;
            for(int i=0; i<items_num; i++)
            {
                final_total_price += total_price[i];
            }

            if(final_total_price>0)
            {
                printf("Tatol price of your order is: %d\n",final_total_price);
                printf("Please wait for a few minutes...\n");

                int distance = shops[shop_index].distance;
                pthread_t price_thread, distance_thread;
                //create threads for displaying the price on the 7-segment and the distance on the LED
                pthread_create(&price_thread, NULL, display_total_price, (void*) &final_total_price);
                pthread_create(&distance_thread, NULL, display_distance, (void*) &distance);
                //wait for the threads to finish
                pthread_join(price_thread, NULL);   
                pthread_join(distance_thread, NULL);
                printf("please pick up your meal\n");
                //flush the input buffer
                while (getchar() != '\n'); 
                printf("Press any key to return to the main menu...\n");
                // Wait for the user to press Enter
                getchar();
            
                break;
            }
            else  //If no items are ordered, return to the main menu
            {
                break;
            }
        }
        else if(order_choice == items_num+2)    //cancel order,then return to the main menu
        {   
            break;
        }
        else    //invalid input   
        {
            printf("invalid choice, please try again\n");
            //flush the input buffer
            while (getchar() != '\n');
        }
    }
}


int main(int argc, char* argv[])
{   
    //setup the data of the shops   
    shop shops[3] = {
        {"Dessert shop", 3, {{"cookie", 60}, {"cake", 80}}},        
        {"Beverage shop", 5, {{"tea", 40}, {"boba", 70}}},          
        {"Diner", 8, {{"fried rice", 120}, {"egg-drop soup", 50}}}  
    };
    //how many shops
    int shops_num = sizeof(shops)/sizeof(shop);

    while(1)
    {   
        int choice = 0;
        //main menu
        printf("1. shop list\n");
        printf("2. order\n");
        scanf("%d", &choice);
        switch(choice)
        {   
            case 1:     //shop list
                printf("\n");
                print_shops(shops,shops_num);
                break;
            case 2:     //order
                int shop_choice, shop_index, items_num;
                //get the shop the user want to order
                shop_choice = choose_shop(shops,shops_num);
                shop_index = shop_choice - 1;
                //order
                order(shops,shop_index);

                break;
            default:
                printf("invalid choice, please try again\n");
                //flush the input buffer
                while (getchar() != '\n');
                break;
        }
    }
    return 0;
}