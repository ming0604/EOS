#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/uaccess.h> //copy_to/from_user()
#include <linux/gpio.h>
// GPIO
// LED is connected to this GPIO
#define GPIO_10 (10)
#define GPIO_11 (11)
#define GPIO_12 (12)
#define GPIO_13 (13)
#define GPIO_14 (14)
#define GPIO_15 (15)
#define GPIO_16 (16)
#define GPIO_17 (17)
//7-seg is connected to this GPIO
#define GPIO_20 (20)
#define GPIO_21 (21)
#define GPIO_22 (22)
#define GPIO_23 (23)
#define GPIO_24 (24)
#define GPIO_25 (25)
#define GPIO_26 (26)
#define GPIO_27 (27)
int GPIO_LED[8] =
{
    GPIO_10,
    GPIO_11,
    GPIO_12,
    GPIO_13,
    GPIO_14,
    GPIO_15,
    GPIO_16,
    GPIO_17
};
int GPIO_7_seg[8] =
{
    GPIO_20,
    GPIO_21,
    GPIO_22,
    GPIO_23,
    GPIO_24,
    GPIO_25,
    GPIO_26,
    GPIO_27   
};
typedef enum my_device
{
    LED,
    seven_seg
}my_device;

dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;
static int __init etx_driver_init(void);
static void __exit etx_driver_exit(void);
/*************** Driver functions **********************/
static int
etx_open(struct inode *inode, struct file *file);
static int
etx_release(struct inode *inode, struct file *file);
static ssize_t etx_read(struct file *filp,
                        char __user *buf, size_t len, loff_t *off);
static ssize_t etx_write(struct file *filp,
                         const char *buf, size_t len, loff_t *off);
/******************************************************/
// File operation structure
static struct file_operations fops =
    {
        .owner = THIS_MODULE,
        .read = etx_read,
        .write = etx_write,
        .open = etx_open,
        .release = etx_release,
};
/*
** This function will be called when we open the Device file
*/
static int etx_open(struct inode *inode, struct file *file)
{
    pr_info("Device File Opened...!!!\n");
    return 0;
}
/*
** This function will be called when we close the Device file
*/
static int etx_release(struct inode *inode, struct file *file)
{
    pr_info("Device File Closed...!!!\n");
    return 0;
}
/*
** This function will be called when we read the Device file
*/
static ssize_t etx_read(struct file *filp,
                        char __user *buf, size_t len, loff_t *off)
{
    uint8_t gpio_state = 0;
    // reading GPIO value
    gpio_state = gpio_get_value(GPIO_21);
    // write to user
    len = 1;
    if (copy_to_user(buf, &gpio_state, len) > 0)
    {
        pr_err("ERROR: Not all the bytes have been copied to user\n");
    }
    pr_info("Read function : GPIO_21 = %d \n", gpio_state);
    return 0;
}
/*
** This function will be called when we write the Device file
*/
static ssize_t etx_write(struct file *filp,
                         const char __user *buf, size_t len, loff_t *off)
{   
    uint8_t rec_buf[10] = {0};
    my_device dev_type;
    if (copy_from_user(rec_buf, buf, len) > 0)
    {
        pr_err("ERROR: Not all the bytes have been copied from user\n");
    }
    //use input's length to determine which device we're using
    if(len == 1)
    {   
        dev_type = LED;
    }
    else if(len == 8)
    {
        dev_type = seven_seg;
    }
    else
    {
        pr_err("ERROR invalid input length to driver");
    }

    switch(dev_type)
    {
        case LED:
            char distance_str[8] = {'0','0','0','0','0','0','0','0'};
            int distance = rec_buf[0]-'0';
            //set the state of the LEDs based on the current distance
            for(int i=0; i<distance; i++)
            {
                distance_str[i] = '1';
            }

            int led_len = sizeof(GPIO_LED)/sizeof(GPIO_LED[0]);
            for (int i = 0; i < led_len; i++)
            {   
                pr_info("Write Function : GPIO_LED %d Set = %c\n", GPIO_LED[i], distance_str[i]);
                if (distance_str[i] == '1')
                {
                    // set the GPIO value to HIGH
                    gpio_set_value(GPIO_LED[i], 1);
                }
                else if (distance_str[i] == '0')
                {
                    // set the GPIO value to LOW
                    gpio_set_value(GPIO_LED[i], 0);
                }
                else
                {
                    pr_err("Unknown command : Please provide either 1 or 0 \n");
                }
            }
            break;
        case seven_seg:
            int seg_len = sizeof(GPIO_7_seg)/sizeof(GPIO_7_seg[0]);
            for (int i = 0; i < seg_len; i++)
            {   
                pr_info("Write Function : GPIO_7_seg %d Set = %c\n", GPIO_7_seg[i], rec_buf[i]);
                if (rec_buf[i] == '1')
                {
                    // set the GPIO value to HIGH
                    gpio_set_value(GPIO_7_seg[i], 1);
                }
                else if (rec_buf[i] == '0')
                {
                    // set the GPIO value to LOW
                    gpio_set_value(GPIO_7_seg[i], 0);
                }
                else
                {
                    pr_err("Unknown command : Please provide either 1 or 0 \n");
                }
            }
            break;
    }
    return len;
}
/*
** Module Init function
*/
static int __init etx_driver_init(void)
{
    /*Allocating Major number*/
    if ((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) < 0)
    {
        pr_err("Cannot allocate major number\n");
        goto r_unreg;
    }
    pr_info("Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));
    /*Creating cdev structure*/
    cdev_init(&etx_cdev, &fops);
    /*Adding character device to the system*/
    if ((cdev_add(&etx_cdev, dev, 1)) < 0)
    {
        pr_err("Cannot add the device to the system\n");
        goto r_del;
    }
    /*Creating struct class*/
    if ((dev_class = class_create(THIS_MODULE, "etx_class")) == NULL)
    {
        pr_err("Cannot create the struct class\n");
        goto r_class;
    }
    /*Creating device*/
    if ((device_create(dev_class, NULL, dev, NULL, "etx_device")) == NULL)
    {
        pr_err("Cannot create the Device \n");
        goto r_device;
    }
    // Checking the GPIO of LED is valid or not
    int gpio_led_len = sizeof(GPIO_LED) / sizeof(GPIO_LED[0]);
    int i;
    for (i = 0; i < gpio_led_len; i++)
    {
        if (gpio_is_valid(GPIO_LED[i]) == false)
        {
            pr_err("GPIO %d is not valid\n", GPIO_LED[i]);
            goto r_device;
        }
        // Requesting the GPIO
        if (gpio_request(GPIO_LED[i], "GPIO_21") < 0)
        {
            pr_err("ERROR: GPIO %d request\n", GPIO_LED[i]);
            goto r_gpio;
        }
        // configure the GPIO as output
        gpio_direction_output(GPIO_LED[i], 0);
        /* Using this call the GPIO 21 will be visible in /sys/class/gpio/
        ** Now you can change the gpio values by using below commands also.
        ** echo 1 > /sys/class/gpio/gpio21/value (turn ON the LED)
        ** echo 0 > /sys/class/gpio/gpio21/value (turn OFF the LED)
        ** cat /sys/class/gpio/gpio21/value (read the value LED)
        **
        ** the second argument prevents the direction from being changed.
        */
        gpio_export(GPIO_LED[i], false);
    }
    // Checking the GPIO of 7-seg is valid or not
    int gpio_seg_len = sizeof(GPIO_7_seg) / sizeof(GPIO_7_seg[0]);
    int j;
    for (j = 0; j < gpio_seg_len; j++)
    {
        if (gpio_is_valid(GPIO_7_seg[j]) == false)
        {
            pr_err("GPIO %d is not valid\n", GPIO_7_seg[j]);
            goto r_device;
        }
        // Requesting the GPIO
        if (gpio_request(GPIO_7_seg[j], "GPIO_21") < 0)
        {
            pr_err("ERROR: GPIO %d request\n", GPIO_7_seg[j]);
            goto r_gpio;
        }
        // configure the GPIO as output
        gpio_direction_output(GPIO_7_seg[j], 0);
        /* Using this call the GPIO 21 will be visible in /sys/class/gpio/
        ** Now you can change the gpio values by using below commands also.
        ** echo 1 > /sys/class/gpio/gpio21/value (turn ON the LED)
        ** echo 0 > /sys/class/gpio/gpio21/value (turn OFF the LED)
        ** cat /sys/class/gpio/gpio21/value (read the value LED)
        **
        ** the second argument prevents the direction from being changed.
        */
        gpio_export(GPIO_7_seg[j], false);
    }

    pr_info("Device Driver Insert...Done!!!\n");
    return 0;
r_gpio:
    while(i >= 0)
    {
        gpio_free(GPIO_LED[i]);
        i--;
    };
    while(j >= 0)
    {
        gpio_free(GPIO_7_seg[i]);
        j--;
    };
r_device:
    device_destroy(dev_class, dev);
r_class:
    class_destroy(dev_class);
r_del:
    cdev_del(&etx_cdev);
r_unreg:
    unregister_chrdev_region(dev, 1);
    return -1;
}
/*
** Module exit function
*/
static void __exit etx_driver_exit(void)
{   
    int gpio_led_len = sizeof(GPIO_LED) / sizeof(GPIO_LED[0]);
    for(int i = 0; i<gpio_led_len; i++)
    {
        gpio_unexport(GPIO_LED[i]);
        gpio_free(GPIO_LED[i]);
    }
    int gpio_seg_len = sizeof(GPIO_7_seg) / sizeof(GPIO_7_seg[0]);
    for(int i = 0; i<gpio_seg_len; i++)
    {
        gpio_unexport(GPIO_7_seg[i]);
        gpio_free(GPIO_7_seg[i]);
    }
    device_destroy(dev_class, dev);
    class_destroy(dev_class);
    cdev_del(&etx_cdev);
    unregister_chrdev_region(dev, 1);
    pr_info("Device Driver Remove...Done!!\n");
}
module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Willy <b0981340647@gmail.com>");
MODULE_DESCRIPTION("A simple device driver - GPIO Driver");
