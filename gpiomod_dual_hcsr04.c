/*
 * Dual Untrasonic HC-SR04 controller driver.
 *
 * Author:
 *  Linh Nguyen (nvl1109@gmail.com)
 *
 */

#include <linux/time.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

#define DEVICE_MAJOR    (119)
#define DEVICE_NAME     "dual_hcsr04"
#define MAXIMUM_RATE    (50)

/* Timer struct, used to create an priodic timer */
static struct timer_list schedule_timer;
static struct timer_list distanceTimeoutTimer;

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

static struct task_struct *runThreads[2];
static bool threadStops[2] = {false, false};

/* Frequence of sampling */
static int sampl_frequence = 0;

/* Echo signals time */
static struct timespec startEcho1, startEcho2, endEcho1, endEcho2;

static uint distance1, distance2;

/* Flags */
static bool echo1Finished, echo2Finished, startMeasureDistance;

/* Character device structure */
static int      raspi_gpio_open (   struct inode *inode,
                                    struct file *filp);
static ssize_t  raspi_gpio_read (   struct file *filp,
                                    char *buf,
                                    size_t count,
                                    loff_t *f_pos);
static ssize_t  raspi_gpio_write (  struct file *filp,
                                    const char *buf,
                                    size_t count,
                                    loff_t *f_pos);
static int      raspi_gpio_release (struct inode *inode,
                                    struct file *filp);
/* File operation structure */
static struct file_operations raspi_gpio_fops = {
                                                    .owner = THIS_MODULE,
                                                    .open = raspi_gpio_open,
                                                    .release = raspi_gpio_release,
                                                    .read = raspi_gpio_read,
                                                    .write = raspi_gpio_write,
                                                };
/*
 * Calculate diferent of two time struct, return microsecond
 */
static long time_diff (struct timespec *t1, struct timespec *t2) {
    int sec = t1->tv_sec - t2->tv_sec;
    long nsec = t1->tv_nsec - t2->tv_nsec;

    return sec * 1000000 + nsec/1000;
}
/*
 * Calculate the distance of sensor.
 */
static uint calculate_distance(uint echoNum, bool isTimedOut) {
    uint res = -1;
    long usec = 0;
    struct timespec *tend, *tstart;

    if (echoNum == 1) {
        tend = &endEcho1;
        tstart = &startEcho1;
    } else {
        tend = &endEcho2;
        tstart = &startEcho2;
    }

    if (!isTimedOut) {
        // calculate
        usec = time_diff(tend, tstart);
        res = (usec * 1750) / 1000000;
    }

    return res;
}

/*
 * The interrupt service routine called on echo signal start/end
 */
static irqreturn_t echo_isr(int irq, void *data)
{
    if(irq == echo_irqs[0] && gpio_get_value(echos[0].gpio)) {
        // Echo 1 started
        getnstimeofday(&startEcho1);
    } else if(irq == echo_irqs[0] && !gpio_get_value(echos[0].gpio)) {
        // Echo 1 ended
        getnstimeofday(&endEcho1);
        // Calculate the distance
        distance1 = calculate_distance(1, false);
        // set the flag.
        echo1Finished = true;
    }
    if(irq == echo_irqs[1] && gpio_get_value(echos[1].gpio)) {
        // Echo 2 started
        getnstimeofday(&startEcho2);
    } else if(irq == echo_irqs[1] && !gpio_get_value(echos[1].gpio)) {
        // Echo 2 ended
        getnstimeofday(&endEcho2);
        // Calculate the distance
        distance2 = calculate_distance(2, false);
        // set the flag.
        echo2Finished = true;
    }

    return IRQ_HANDLED;
}

/*
 * Echo signal is timed out
 */
static void echo_timeout(unsigned long data)
{
    printk(KERN_INFO "%s\n", __func__);

    if (!echo1Finished) {
        distance1 = calculate_distance(1, true);
        // set the flag.
        echo1Finished = true;
    }
    if (!echo2Finished) {
        distance2 = calculate_distance(2, true);
        // set the flag.
        echo2Finished = true;
    }
    del_timer_sync(&distanceTimeoutTimer);
}
/*
 * Get distance thread. This thread always get distance value from dual HC-SR04
 * sensors.
 */
static int get_distance_thread(void *data)
{
    uint idx = (uint) data;

    while(!threadStops[idx]) {
        if (startMeasureDistance) {
            startMeasureDistance = false;
            // Set trigger pin in 2us
            gpio_set_value(triggers[0].gpio, 1);
            udelay(2);
            gpio_set_value(triggers[0].gpio, 0);
            // Start timeout timer
            distanceTimeoutTimer.function = echo_timeout;
            distanceTimeoutTimer.data = 0L;
            distanceTimeoutTimer.expires = jiffies + (sampl_frequence*HZ);         // 30 ms @TODO calculate the sampl_freq
            add_timer(&distanceTimeoutTimer);
        }
        if (echo1Finished) {
            echo1Finished = false;
            // process distance 1 value
            printk(KERN_INFO "distance1: %d\n", distance1);
        }
        if (echo2Finished) {
            echo2Finished = false;
            // process distance 2 value
            printk(KERN_INFO "distance2: %d\n", distance2);
        }

        /* Delay 1 microsecond */
        udelay(1);
    }
    return 0;
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
    char tmp[100];
    long rate;
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
        printk(KERN_ERR "Sampling frequence must in range [0..50], current is %ld", rate);
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

    // Init all flags
    startMeasureDistance = false;
    echo1Finished = false;
    echo2Finished = false;

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

    ret = request_irq(echo_irqs[0], echo_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "dual_hcsr04#echo1", NULL);

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

    ret = request_irq(echo_irqs[1], echo_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "dual_hcsr04#echo2", NULL);

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
    init_timer(&distanceTimeoutTimer);

    /* Create and start the distance measuring thread */
    runThreads[0] = kthread_run(&get_distance_thread,(void *)NULL,"distance");

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
    del_timer_sync(&distanceTimeoutTimer);

    // stop threads
    threadStops[0] = true;
    threadStops[1] = true;
    kthread_stop(runThreads[0]);

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
