/*
 * Dual Untrasonic HC-SR04 controller driver.
 *
 * Author:
 *  Linh Nguyen (nvl1109@gmail.com)
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
// #include <stdlib.h>

#define DEVICE_MAJOR    (119)
#define DEVICE_NAME     "dual_hcsr04"
#define MAXIMUM_RATE    (50)

/* Timer struct, used to create an priodic timer */
static struct timer_list schedule_timer;

/* Define GPIOs for trigger pin */
static struct gpio triggers[] = {
        {  16, GPIOF_OUT_INIT_LOW, "Trigger" },
};

/* Define GPIOs for echo pins */
static struct gpio echos[] = {
        { 20, GPIOF_IN, "Echo 1" },
        { 21, GPIOF_IN, "Echo 2" },
};

/* Later on, the assigned IRQ numbers for the echos are stored here */
static int echo_irqs[] = { -1, -1 };

/* Frequence of sampling */
static int sampl_frequence = 0;

/* Latest distance value */
static float distance1 = -1, distance2 = -1;

/* Character device structure */
static int      raspi_gpio_open(struct inode *inode, struct file *filp);
static ssize_t  raspi_gpio_read (   struct file *filp,
                                    char *buf,
                                    size_t count,
                                    loff_t *f_pos);
static ssize_t  raspi_gpio_write (  struct file *filp,
                                    const char *buf,
                                    size_t count,
                                    loff_t *f_pos);
static int      raspi_gpio_release(struct inode *inode, struct file *filp);
/* File operation structure */
static struct file_operations raspi_gpio_fops = {
                                                    .owner = THIS_MODULE,
                                                    .open = raspi_gpio_open,
                                                    .release = raspi_gpio_release,
                                                    .read = raspi_gpio_read,
                                                    .write = raspi_gpio_write,
                                                };

/*
 * The interrupt service routine called on echo signal start/end
 */
static irqreturn_t echo_isr(int irq, void *data)
{
    if(irq == echo_irqs[0] && gpio_get_value(echos[0].gpio)) {
        // Echo 1 started
    } else if(irq == echo_irqs[0] && !gpio_get_value(echos[0].gpio)) {
        // Echo 1 ended
    }
    if(irq == echo_irqs[1] && gpio_get_value(echos[1].gpio)) {
        // Echo 2 started
    } else if(irq == echo_irqs[1] && !gpio_get_value(echos[1].gpio)) {
        // Echo 2 ended
    }

    return IRQ_HANDLED;
}


static int      raspi_gpio_open(struct inode *inode, struct file *filp) {
    try_module_get(THIS_MODULE);

    return 0;
}
static ssize_t  raspi_gpio_read (   struct file *filp,
                                    char *buf,
                                    size_t count,
                                    loff_t *f_pos){
    char tmp[100] = "DEMO READ\n";
    ssize_t i;

    for (i=0; i<strlen(tmp); ++i) {
        if (put_user(tmp[i], buf + i))
            break;
    }
    put_user(0, buf + i);

    return i;
}
static void measure_timer_function(unsigned long data)
{
    printk(KERN_INFO "%s\n", __func__);

    /* schedule next execution */
    schedule_timer.expires = jiffies + (sampl_frequence*HZ);         // 1 sec.
    add_timer(&schedule_timer);
}
static void raspi_update_timer(void) {
    if (sampl_frequence == 0) {
        /* Cancel the timer */
        del_timer_sync(&schedule_timer);
        printk(KERN_INFO "Stop schedule timer.");
        return;
    }
    schedule_timer.function = measure_timer_function;
    schedule_timer.data = 0L;
    schedule_timer.expires = jiffies + (sampl_frequence*HZ);         // 1 sec.
    add_timer(&schedule_timer);
}
static ssize_t  raspi_gpio_write (  struct file *filp,
                                    const char *buf,
                                    size_t count,
                                    loff_t *f_pos) {
    char tmp[100], *endptr;
    long rate, i;
    int maxbytes; /*maximum bytes that can be read from f_pos to BUFFER_SIZE*/
    int bytes_to_write; /* gives the number of bytes to write*/
    int bytes_writen;/*number of bytes actually writen*/
    int res;

    memset(tmp, 0, 100);
    maxbytes = 100 - *f_pos;
    if(maxbytes > count)
        bytes_to_write = count;
    else
        bytes_to_write = maxbytes;
    bytes_writen = bytes_to_write - copy_from_user(tmp + *f_pos, buf, bytes_to_write);
    *f_pos += bytes_writen;

    if (IS_ERR(tmp))
        return PTR_ERR(tmp);
    printk(KERN_INFO "Write requested: %d - [%s]", count, tmp);

    res = kstrtol(tmp, 10, &rate);
    if (res != 0) {
        printk(KERN_ERR "Sampling frequence must be a number.");
        return -1;
    }
    if ((rate < 0) || (rate > MAXIMUM_RATE))
    {
        printk(KERN_ERR "Sampling frequence must in range [0..50], current is %d", rate);
        return -1;
    }
    sampl_frequence = rate;
    raspi_update_timer();

    printk(KERN_INFO "Sample frequence: %d", sampl_frequence);
    return bytes_writen;
}

static int      raspi_gpio_release(struct inode *inode, struct file *filp) {
    module_put(THIS_MODULE);

    return 0;
}

/*
 * Module init function
 */
static int __init gpiomode_init(void)
{
    int ret = 0;
    int tmp;

    printk(KERN_INFO "%s\n", __func__);

    // register trigger pin
    ret = gpio_request_array(triggers, ARRAY_SIZE(triggers));

    if (ret) {
        printk(KERN_ERR "Unable to request GPIOs for triggers: %d\n", ret);
        return ret;
    }

    // register ECHO gpios
    ret = gpio_request_array(echos, ARRAY_SIZE(echos));

    if (ret) {
        printk(KERN_ERR "Unable to request GPIOs for echos: %d\n", ret);
        goto fail1;
    }

    printk(KERN_INFO "Current button1 value: %d\n", gpio_get_value(echos[0].gpio));

    ret = gpio_to_irq(echos[0].gpio);

    if(ret < 0) {
        printk(KERN_ERR "Unable to request IRQ: %d\n", ret);
        goto fail2;
    }

    echo_irqs[0] = ret;

    printk(KERN_INFO "Successfully requested BUTTON1 IRQ # %d\n", echo_irqs[0]);

    ret = request_irq(echo_irqs[0], echo_isr, IRQF_TRIGGER_RISING | IRQF_DISABLED, "gpiomod#button1", NULL);

    if(ret) {
        printk(KERN_ERR "Unable to request IRQ: %d\n", ret);
        goto fail2;
    }


    ret = gpio_to_irq(echos[1].gpio);

    if(ret < 0) {
        printk(KERN_ERR "Unable to request IRQ: %d\n", ret);
        goto fail2;
    }

    echo_irqs[1] = ret;

    printk(KERN_INFO "Successfully requested BUTTON2 IRQ # %d\n", echo_irqs[1]);

    ret = request_irq(echo_irqs[1], echo_isr, IRQF_TRIGGER_RISING | IRQF_DISABLED, "gpiomod#button2", NULL);

    if(ret) {
        printk(KERN_ERR "Unable to request IRQ: %d\n", ret);
        goto fail3;
    }

    /* Request character device */
    tmp = register_chrdev(DEVICE_MAJOR, DEVICE_NAME, &raspi_gpio_fops);
    if (tmp < 0) {
        printk(KERN_ERR "Unable to register char device with major %d and name %s.", DEVICE_MAJOR, DEVICE_NAME);
        goto fail3;
    }
    printk(KERN_INFO "Successfully registered char device %d - %s", DEVICE_MAJOR, DEVICE_NAME);

    /* Initialize timer for scheduling */
    init_timer(&schedule_timer);

    return 0;

// cleanup what has been setup so far
fail3:
    free_irq(echo_irqs[0], NULL);

fail2:
    gpio_free_array(echos, ARRAY_SIZE(triggers));

fail1:
    gpio_free_array(triggers, ARRAY_SIZE(triggers));

    return ret;
}

/**
 * Module exit function
 */
static void __exit gpiomode_exit(void)
{
    int i;

    printk(KERN_INFO "%s\n", __func__);

    // Un-register char device
    unregister_chrdev(DEVICE_MAJOR, DEVICE_NAME);

    del_timer_sync(&schedule_timer);

    // free irqs
    free_irq(echo_irqs[0], NULL);
    free_irq(echo_irqs[1], NULL);

    // turn all triggers off
    for(i = 0; i < ARRAY_SIZE(triggers); i++) {
        gpio_set_value(triggers[i].gpio, 0);
    }

    // unregister
    gpio_free_array(triggers, ARRAY_SIZE(triggers));
    gpio_free_array(echos, ARRAY_SIZE(echos));
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linh Nguyen");
MODULE_DESCRIPTION("PI B+ kernel module for measuring distance using dual HC-SR04");

module_init(gpiomode_init);
module_exit(gpiomode_exit);
