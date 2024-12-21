#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/uaccess.h> //copy_to/from_user()
#include <linux/gpio.h>     //GPIO
#include <linux/delay.h>  // msleep

// 定義 GPIO 引腳
#define LEFT_BUTTON_GPIO 18
#define RIGHT_BUTTON_GPIO 24

#define NUM_BUTTONS 2  // 定義按鈕的數量

static int gpio_pins[NUM_BUTTONS] = {LEFT_BUTTON_GPIO, RIGHT_BUTTON_GPIO};

dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

static int __init etx_driver_init(void);
static void __exit etx_driver_exit(void);

/*************** Driver functions **********************/
static int etx_open(struct inode *inode, struct file *file);
static int etx_release(struct inode *inode, struct file *file);
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
/******************************************************/

//File operation structure
static struct file_operations fops =
{
    .owner = THIS_MODULE,
    .read = etx_read,
    .open = etx_open,
    .release = etx_release,
};

/*
** 消抖函數
*/
// static int debounce_gpio(int gpio_pin) {
//     int state1, state2;
//     state1 = gpio_get_value(gpio_pin);
//     msleep(50);  // 等待50ms
//     state2 = gpio_get_value(gpio_pin);
//     return (state1 == state2) ? state1 : 0;  // 若狀態一致，返回穩定值
// }

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
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    uint8_t gpio_states[NUM_BUTTONS];  // 用來存放按鈕的狀態

    // 若不需要消抖，直接讀取 GPIO 狀態
    gpio_states[0] = gpio_get_value(LEFT_BUTTON_GPIO);   // 讀取左按鈕 GPIO 狀態(0或1)
    gpio_states[1] = gpio_get_value(RIGHT_BUTTON_GPIO);  // 讀取右按鈕 GPIO 狀態(0或1)

    // 將按鈕狀態寫入用戶空間
    if (copy_to_user(buf, gpio_states, sizeof(gpio_states)) > 0) {
        pr_err("ERROR: Not all the bytes have been copied to user\n");
        return -EFAULT;
    }

    pr_info("Read function: LEFT_BUTTON = %d, RIGHT_BUTTON = %d\n",
            gpio_states[0], gpio_states[1]);

    return sizeof(gpio_states);
}


/*
** Module Init function
*/
static int __init etx_driver_init(void)
{
    /* Allocating Major number */
    if ((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) < 0) {
        pr_err("Cannot allocate major number\n");
        goto r_unreg;
    }

    pr_info("Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));

    /* Creating cdev structure */
    cdev_init(&etx_cdev, &fops);

    /* Adding character device to the system */
    if ((cdev_add(&etx_cdev, dev, 1)) < 0) {
        pr_err("Cannot add the device to the system\n");
        goto r_del;
    }

    /* Creating struct class */
    if ((dev_class = class_create(THIS_MODULE, "etx_class")) == NULL) {
        pr_err("Cannot create the struct class\n");
        goto r_class;
    }

    /* Creating device */
    if ((device_create(dev_class, NULL, dev, NULL, "etx_device")) == NULL) {
        pr_err("Cannot create the Device\n");
        goto r_device;
    }

    /* 檢查 GPIO 是否有效並請求它們 */
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (!gpio_is_valid(gpio_pins[i])) {
            pr_err("GPIO %d is not valid\n", gpio_pins[i]);
            goto r_gpio;
        }

        if (gpio_request(gpio_pins[i], "GPIO_BUTTON") < 0) {
            pr_err("ERROR: GPIO %d request failed\n", gpio_pins[i]);
            goto r_gpio;
        }

        gpio_direction_input(gpio_pins[i]);
        gpio_export(gpio_pins[i], false);
    }

    pr_info("Device Driver Insert...Done!!!\n");
    return 0;

r_gpio:
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_free(gpio_pins[i]);
    }

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
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_unexport(gpio_pins[i]);
        gpio_free(gpio_pins[i]);
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
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("A simple device driver - GPIO Button Driver");
MODULE_VERSION("1.0");
