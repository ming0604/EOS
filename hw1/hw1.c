#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
    printf("Press Enter to return to the main menu...\n");
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


void order(food* food_items, int items_num)
{   
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
                items_num = sizeof(shops[shop_index].items)/sizeof(food);
                order(shops[shop_index].items, items_num);

                break;
            default:
                printf("invalid choice, please try again\n");
                break;
        }
    }
    return 0;
}