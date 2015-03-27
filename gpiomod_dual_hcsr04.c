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
static uint sampl_frequence = 0;

/* Latest distance value */

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

/*
 * Module init function
 */
static int __init gpiomode_init(void)
{
    int ret = 0;

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

    /* Initialize timer for scheduling */

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

MODULE_LICENSE("BSD");
MODULE_AUTHOR("Linh Nguyen");
MODULE_DESCRIPTION("PI B+ kernel module for measuring distance using dual HC-SR04");

module_init(gpiomode_init);
module_exit(gpiomode_exit);
