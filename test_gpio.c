#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/interrupt.h>


#define NUM_GPIOS 54

/* Module parameters */
static int gpio[NUM_GPIOS];
static int gpio_argc = 0;
module_param_array(gpio, int, &gpio_argc, 0644);


/* GPIO Function Select Registers
 *
 * 5 GPFSEL 32-bit registers, starting from offset 0x00.
 * Every register controls 10 pins, 3 bits per pin, so the last two bits are unused (reserved):
 * 000 - GPIO pin is input
 * 001 - GPIO pin is otput
 * xxx - for other combinations, GPIO pin takes some alternate function */
#define GPFSEL		0x0
#define GET_GPFSEL_REG_OFFSET(pin)		(GPFSEL + (((pin) / 10) * 4))
#define GET_GPFSEL_PIN_OFFSET(pin)		(((pin) % 10) * 3)


/* GPIO Pin Output Set Registers
 *
 * 2 GPSET 32-bit registers, starting from offset 0x1c.
 * Every register controls 32 pins, 1 bit per pin:
 * 0 - no effect
 * 1 - Set GPIO pin */
#define GPSET		0x1c
#define GET_GPSET_REG_OFFSET(pin)		(GPSET + (((pin) / 32) * 4))
#define GET_GPSET_PIN_OFFSET(pin)		(pin)


/* GPIO Pin Output Clear Registers
 *
 * 2 GPCLR 32-bit registers, starting from offset 0x28.
 * Every register controls 32 pins, 1 bit per pin:
 * 0 - no effect
 * 1 - Clear GPIO pin */
#define GPCLR		0x28
#define GET_GPCLR_REG_OFFSET(pin)		(GPCLR + (((pin) / 32) * 4))
#define GET_GPCLR_PIN_OFFSET(pin)		(pin)


/* GPIO Pin Level Registers
 *
 * 2 GPLEV 32-bit registers, starting from offset 0x34.
 * Every register controls 32 pins, 1 bit per pin:
 * 0 - GPIO pin is low
 * 1 - GPIO pin is high */
#define GPLEV		0x34
#define GET_GPLEV_REG_OFFSET(pin)		(GPLEV + (((pin) / 32) * 4))
#define GET_GPLEV_PIN_OFFSET(pin)		(pin)


/* GPIO Event Detect Status Registers
 *
 * 2 GPEDS 32-bit registers, starting from offset 0x40
 * Every register controls 32 pins, 1 bit per pin.
 * The relevant bit in the event detect status registers is set whenever:
 *   1) an edge is detected that matches the type of edge programmed in the rising/falling
 *      edge detect enable registers
 *   2) a level is detected that matches the type of level programmed in the high/low
 *      level detect enable registers.
 * The bit is cleared by writing a “1” to the relevant bit. */
#define GPEDS		0x40
#define GET_GPEDS_REG_OFFSET(pin)		(GPEDS + (((pin) / 32) * 4))
#define GET_GPEDS_PIN_OFFSET(pin)		(pin)


/* GPIO Rising Edge Detect Enable Registers
 *
 * 2 GPREN 32-bit registers, starting from offset 0x4C
 * Every register controls 32 pins, 1 bit per pin:
 * 0 - Rising edge detect disabled
 * 1 - Rising edge sets corresponding bit in GPEDS */
#define GPREN		0x4C
#define GET_GPREN_REG_OFFSET(pin)		(GPREN + (((pin) / 32) * 4))
#define GET_GPREN_PIN_OFFSET(pin)		(pin)


/* GPIO Falling Edge Detect Enable Registers
 *
 * 2 GPREN 32-bit registers, starting from offset 0x58
 * Every register controls 32 pins, 1 bit per pin:
 * 0 - Falling edge detect disabled
 * 1 - Falling edge sets corresponding bit in GPEDS */
#define GPFEN		0x58
#define GET_GPFEN_REG_OFFSET(pin)		(GPFEN + (((pin) / 32) * 4))
#define GET_GPFEN_PIN_OFFSET(pin)		(pin)



enum output_level {
	OUTPUT_LOW,
	OUTPUT_HIGH,
	OUTPUT_MAX
};

enum reg_fsel {
	REG_FSEL_GPIO_IN = 0,
	REG_FSEL_GPIO_OUT = 1,
	REG_FSEL_ALT0 = 4,
	REG_FSEL_ALT1 = 5,
	REG_FSEL_ALT2 = 6,
	REG_FSEL_ALT3 = 7,
	REG_FSEL_ALT4 = 3,
	REG_FSEL_ALT5 = 2
};

enum edge_detect {
	EDGE_RISING,
	EGDE_FALLING
};


struct test_gpio_dev {
	struct miscdevice miscdev;
	void __iomem *regs;
	struct device_attribute **dev_attr;
	char **sysfiles;
	int irq;
};

static ssize_t test_gpio_write(struct file *file, const char __user *buf, size_t count, loff_t * ppos);
static ssize_t test_gpio_read(struct file *file, char __user *buf, size_t count, loff_t * ppos);

static const struct file_operations test_gpio_fops = {
    .owner      = THIS_MODULE,
    .write      = test_gpio_write,
	.read       = test_gpio_read
};

static unsigned int reg_read(struct test_gpio_dev *gpioDev, int off)
{
	return readl(gpioDev->regs + off);
}

static void reg_write(struct test_gpio_dev *gpioDev, int val, int off)
{
	writel(val, gpioDev->regs + off);
	return;
}


static int set_output(struct test_gpio_dev *gpioDev, char pin, enum output_level out) {
	int val, mask;
	int reg_offset, pin_offset;

	/* RED LED is connected to GPIO17, e.g. to turn it on: */
	// GPFSEL1, bits 23-21 -> 001 = GPIO Pin 17 is an output
	// GPSET0, set pin 17
	/* GREEN LED is connected to GPIO26 */

	reg_offset = GET_GPFSEL_REG_OFFSET(pin);
	pin_offset = GET_GPFSEL_PIN_OFFSET(pin);
	/* set pin as output */
	// first, cleanup all 3 pin bits
	mask = (0x03 << pin_offset);
	val = reg_read(gpioDev, reg_offset) & ~mask;
	// then set pin as output
	mask = (0x01 << pin_offset);
	val |= mask;
	reg_write(gpioDev, val, reg_offset);

	/* set pin to 0 on 1 */
	switch (out) {
	case OUTPUT_LOW:
		reg_offset = GET_GPCLR_REG_OFFSET(pin);
		pin_offset = GET_GPCLR_PIN_OFFSET(pin);
		val = (0x1 << pin_offset);
		break;

	case OUTPUT_HIGH:
		reg_offset = GET_GPSET_REG_OFFSET(pin);
		pin_offset = GET_GPSET_PIN_OFFSET(pin);
		val = (0x1 << pin_offset);
		break;

	default:
		printk(KERN_ALERT "\n[%s][%d] ERROR: Invalid argument!\n", __FUNCTION__, __LINE__);
		//TODO: Handle this error
	}

	reg_write(gpioDev, val, reg_offset);

	return 0;
}

static int set_input(struct test_gpio_dev *gpioDev, char pin) {
	int val, mask;
	int reg_offset, pin_offset;

	/* e.g, Switch is connected to GPIO17, e.g. to set it as input: */
	// GPFSEL1, bits 23-21 -> 000 = GPIO Pin 17 is an output

	reg_offset = GET_GPFSEL_REG_OFFSET(pin);
	pin_offset = GET_GPFSEL_PIN_OFFSET(pin);
	mask = (0x03 << pin_offset);
	val = reg_read(gpioDev, reg_offset) & ~mask;
	reg_write(gpioDev, val, reg_offset);

	return 0;
}


static int disable_egdes(struct test_gpio_dev *gpioDev, char pin) {
	int val, mask;
	int reg_offset, pin_offset;

	// Disable Rissing edge
	reg_offset = GET_GPREN_REG_OFFSET(pin);
	pin_offset = GET_GPREN_PIN_OFFSET(pin);

	/* Read register value */
	val = reg_read(gpioDev, reg_offset);
	// clear pin in corresponding Rising register
	mask = (0x01 << pin_offset);
	val &= ~mask;
	reg_write(gpioDev, val, reg_offset);


	// Disable Falling edge
	reg_offset = GET_GPFEN_REG_OFFSET(pin);
	pin_offset = GET_GPFEN_PIN_OFFSET(pin);

	/* Read register value */
	val = reg_read(gpioDev, reg_offset);
	// clear pin in corresponding Falling register
	mask = (0x01 << pin_offset);
	val &= ~mask;
	reg_write(gpioDev, val, reg_offset);

	return 0;
}

static int enable_egde(struct test_gpio_dev *gpioDev, char pin, int edge) {
	int val, mask;
	int reg_offset, pin_offset;

	disable_egdes(gpioDev, pin);
	set_input(gpioDev, pin);

	switch (edge) {
	case EDGE_RISING:
		reg_offset = GET_GPREN_REG_OFFSET(pin);
		pin_offset = GET_GPREN_PIN_OFFSET(pin);
		break;

	case EGDE_FALLING:
		reg_offset = GET_GPFEN_REG_OFFSET(pin);
		pin_offset = GET_GPFEN_PIN_OFFSET(pin);
		break;

	default:
		printk(KERN_ALERT "\n[%s][%d] ERROR: Invalid argument!\n", __FUNCTION__, __LINE__);
		//TODO: Handle this error
	}

	/* Read register value */
	val = reg_read(gpioDev, reg_offset);
	// Set pin in corresponding Rising/Falling register
	mask = (0x01 << pin_offset);
	val |= mask;
	reg_write(gpioDev, val, reg_offset);

	return 0;
}

static int acknowledge_int(struct test_gpio_dev *gpioDev) {
	int val, orig_val, mask;
	int reg_offset = GPEDS;
	int pin = 0, i;

	/* Read register value */
	val = reg_read(gpioDev, reg_offset);
//	pr_info("\n     val 1: 0x%x\n", val);
	orig_val = val;
	if (val == 0) {
		reg_offset += 0x04;
		val = reg_read(gpioDev, reg_offset);
		//pr_info("\n     val 2: 0x%x\n", val);
		pin += 32;
	}

	for (i = 0; i < 32; i++) {
		if (i != 0)
		   val >>= 1;
		if (val == 1)
			break;
	}
	pin += i;
//	pr_info("\npin: %d\n", pin);

	if (pin < 64) {
		mask = (0x01 << i);
		orig_val |= mask;
		reg_write(gpioDev, orig_val, reg_offset);
	}

	return pin;
}



static ssize_t test_gpio_read(struct file *file, char __user *buf, size_t count, loff_t * ppos)
{
	struct test_gpio_dev *gpioDev = container_of(file->private_data, struct test_gpio_dev, miscdev);
	static int pin = -1;
	int i, val, level;
	char tmp_buf[200] = {0};
	int transfer_size;
	int reg_offset, pin_offset;

	if (pin == -1) {
		snprintf(tmp_buf, sizeof(tmp_buf), "\n  #   dir   value");
		pin++;
		goto transfer;
	}
	for (i = pin ; i < NUM_GPIOS; i++) {
		reg_offset = GET_GPFSEL_REG_OFFSET(pin);
		pin_offset = GET_GPFSEL_PIN_OFFSET(pin);

		val = reg_read(gpioDev, reg_offset);
		val = (val >> pin_offset) & 7;

		if (val == REG_FSEL_GPIO_IN || val == REG_FSEL_GPIO_OUT) {
			reg_offset = GET_GPLEV_REG_OFFSET(pin);
			pin_offset = GET_GPLEV_PIN_OFFSET(pin);
			level = reg_read(gpioDev, reg_offset);
			level = (level >> pin_offset) & 1;
			if (val == REG_FSEL_GPIO_IN)
				snprintf(tmp_buf, sizeof(tmp_buf), "\n  %d. input    %d", i, level);
			else
				snprintf(tmp_buf, sizeof(tmp_buf), "\n  %d. output   %d", i, level);
		}
			pin++;
			goto transfer;
	}

	pin = -1;
	return 0;

transfer:
	transfer_size = strlen(tmp_buf) + 1;

	//		printk(KERN_ALERT "\n----- [%s] [%d] tmp_buf: %s \n", __FUNCTION__, __LINE__, tmp_buf);
	if (copy_to_user(buf /* to */ , tmp_buf + *ppos/* from */ , transfer_size)) {
		printk(KERN_ERR "ERROR, copy_to_user return -EFAULT \n");
		return -EFAULT;
	}
	return (transfer_size);
}


static ssize_t test_gpio_write(struct file *file, const char __user *buf, size_t count, loff_t * ppos)
{
	/* The ﬁrst thing to do is to retrieve the test_gpio_dev structure from the miscdevice structure itself,
	 * accessible through the private_data ﬁeld of the open ﬁle structure (file).
	 * At the time we registered our misc device, we didn’t keep any pointer to the test_gpio_dev structure.
	 * However, as the miscdevice structure is accessible through file->private_data, and is a member of the test_gpio_dev structure,
	 * we can use a magic macro to compute the address of the parent structure:
	 *
	 * see: http://radek.io/2012/11/10/magical-container_of-macro/
	 */
	struct test_gpio_dev *gpioDev = container_of(file->private_data, struct test_gpio_dev, miscdev);
	int pass=0;
	char *input, *input_free;
	const char *tmp;
	char cmd[20], pin;
	int err;

	input = kzalloc(count, GFP_ATOMIC);
	input_free = input;
	if (input == NULL) {
		err = -ENOMEM;
		goto out_err;
	}

	/* Last character is 0xA (NL line feed). kzalloc initialize allicated memory to zeros.
	 * Copy count-1 bytes only (without 0xA), last allocated byte in input buffer is NULL terminator */
	if (copy_from_user(input, buf, count-1)) {
		err = -EFAULT;
		goto out_err;
	}

	while ((tmp = strsep((char **)&input, " ")) != NULL) {
		if (!*tmp) {
			break;
		}

		switch (pass++) {
		case 0:
			pin = (char) simple_strtol(tmp, NULL, 0);
			break;
		case 1:
			strncpy(cmd, tmp, sizeof(cmd));
			break;
		default:
			break;
		}
	}

	kfree(input_free);


	if (strcmp(cmd, "high") == 0) {
		set_output(gpioDev, pin, OUTPUT_HIGH);
	}
	else if (strcmp(cmd, "low") == 0) {
		set_output(gpioDev, pin, OUTPUT_LOW);
	}
	else if (strcmp(cmd, "in") == 0) {
		set_input(gpioDev, pin);
	}
	else if (strcmp(cmd, "rising") == 0) {
		enable_egde(gpioDev, pin, EDGE_RISING);
	}
	else if (strcmp(cmd, "falling") == 0) {
		enable_egde(gpioDev, pin, EGDE_FALLING);
	}
	else if (strcmp(cmd, "none") == 0) {
		disable_egdes(gpioDev, pin);
	}
	else {
		printk(KERN_ALERT "\nERROR: Invalid command!\n");
		err = count;
		goto out_err;
	}


	return count;

out_err:
	return err;
}

/******************************************************************************
 *
 * sysfs show() and store()
 *
 *****************************************************************************/

/* sysfs file (attr->attr.name) is in format: testgpioX, where X is the pin number
 * This functon returns the pin number extracted from the file name
 */
static char get_pin_nb(struct device_attribute *attr)
{
	char *tmp, pin;
	/* strsep() is used to get pin number from attr->attr.name. attr->attr.name is in the format testgpioX, where the pin number is X.
	 * strsep() moves pointer passed as the first argument. Therefore attr->attr.name cannot be used as it is declared as const.
	 * Also, we need to preserve value of the fname after allocation (fname_free) in order tobe able to free it properly */
	char *fname = kzalloc(strlen(attr->attr.name) + 1, GFP_KERNEL);
	char *fname_free = fname;

	strcpy(fname, attr->attr.name);
	tmp = strsep((char **)&fname, "o");
	pin = (char) simple_strtol(fname, NULL, 0);
	kfree(fname_free);

	return pin;
}


static ssize_t test_gpio_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char pin;
	int val, level;
	char tmp_buf[200] = {0};
	struct test_gpio_dev *gpioDev = dev_get_drvdata(dev);
	int reg_offset, pin_offset;

	pin = get_pin_nb(attr);

	reg_offset = GET_GPFSEL_REG_OFFSET(pin);
	pin_offset = GET_GPFSEL_PIN_OFFSET(pin);

	val = reg_read(gpioDev, reg_offset);
	val = (val >> pin_offset) & 7;

	if (val == REG_FSEL_GPIO_IN || val == REG_FSEL_GPIO_OUT) {
		reg_offset = GET_GPLEV_REG_OFFSET(pin);
		pin_offset = GET_GPLEV_PIN_OFFSET(pin);

		level = reg_read(gpioDev, reg_offset);
		level = (level >> pin_offset) & 1;
		if (val == REG_FSEL_GPIO_IN)
			snprintf(tmp_buf, sizeof(tmp_buf), "input: %d", level);
		else
			snprintf(tmp_buf, sizeof(tmp_buf), "output: %d", level);
	}
	else {
		snprintf(tmp_buf, sizeof(tmp_buf), "Not input/output pin!");
	}

	return snprintf(buf, PAGE_SIZE, "%s\n", tmp_buf);
}

static ssize_t test_gpio_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	char pin;
	struct test_gpio_dev *gpioDev = dev_get_drvdata(dev);

	pin = get_pin_nb(attr);

	if (strncmp(buf, "high", strlen("high")) == 0) {
		set_output(gpioDev, pin, OUTPUT_HIGH);
	}
	else if (strncmp(buf, "low", strlen("low")) == 0) {
		set_output(gpioDev, pin, OUTPUT_LOW);
	}
	else if (strncmp(buf, "in", strlen("in")) == 0) {
		set_input(gpioDev, pin);
	}
	else if (strncmp(buf, "rising", strlen("rising")) == 0) {
		enable_egde(gpioDev, pin, EDGE_RISING);
	}
	else if (strncmp(buf, "falling", strlen("falling")) == 0) {
		enable_egde(gpioDev, pin, EGDE_FALLING);
	}
	else if (strncmp(buf, "none", strlen("none")) == 0) {
		disable_egdes(gpioDev, pin);
	}
	else {
		printk(KERN_ALERT "\nERROR: Invalid command: %s\n", buf);
		//TODO: handle this error
	}
	return count;
}


#ifdef CONFIG_OF
static struct of_device_id test_gpio_dt_match[] = {
	{ .compatible = "test_gpio", },
	{ },
};
MODULE_DEVICE_TABLE(of, test_gpio_dt_match);
#endif

static irqreturn_t test_gpio_interrupt(int irq, void *dev)
{
	int ret = IRQ_HANDLED;
	int pin;
	struct test_gpio_dev *gpioDev = (struct test_gpio_dev *)dev;

	pin = acknowledge_int(gpioDev);
	//pin=64: both GPEDS0 and GPEDS1 are zeros of all bits!!
	if (pin < 64)
		pr_info("\nEnter test_gpio_interrupt: %s, pin: %d\n", gpioDev->miscdev.name, pin);

	return ret;
}

static int test_gpio_remove(struct platform_device *pdev)
{
	int i;
	struct test_gpio_dev *gpioDev = platform_get_drvdata(pdev);

	for (i = 0; i < gpio_argc; i++) {
//		pr_info("device_remove_file: %s\n", gpioDev->dev_attr[i]->attr.name);
		device_remove_file(&pdev->dev, gpioDev->dev_attr[i]);
	}

	misc_deregister(&gpioDev->miscdev);


	return 0;
}

static int test_gpio_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct resource *regs;
	struct test_gpio_dev *gpioDev;
	int err = 0, i;
	char name[20];
	int irq;

	/* The first operation is a sanity check, verifying that the probe was called on a device that is relevant.
	 * This is probably not really necessary, but this check appears in many drivers. */
	match = of_match_device(test_gpio_dt_match, &pdev->dev);
	if (!match)
		return -EINVAL;

	/* Get the device memory range from the device tree */
	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "could not get IO memory\n");
		return -ENXIO;
	}
//	pr_info("\nphysical address: 0x%x\n", regs->start); //physical address: 0x20200000
//	if (regs->name)
//		pr_info("\n~~~~~ regs->name: %s\n", regs->name); //~~~~~ regs->name: /soc/test_gpio@7e215000

	gpioDev = devm_kzalloc(&pdev->dev, sizeof(struct test_gpio_dev), GFP_KERNEL);



	/* map the device physical memory into the virtual address space  */

/* !!! Cannot do it with devm_ioremap_resource because it internaly calls request_mem_region.
 *     As devm_ioremap_resource is already called from pinctrl-bcm2835, another call of
 *     request_mem_region on same memory region will cause an error.
 **/
#if 0
	gpioDev->regs = devm_ioremap_resource(&pdev->dev, regs);
	if (!gpioDev->regs) {
		dev_err(&pdev->dev, "Cannot remap registers\n");
		return -ENOMEM;
	}
#endif
/* request_mem_region tells the kernel that driver is going to use this range of I/O addresses,
 * which will prevent other drivers to make any overlapping call to the same region through request_mem_region.
 * This mechanism does not do any kind of mapping, it's a pure reservation mechanism,
 * which relies on the fact that all kernel device drivers must be nice, and they must call request_mem_region,
 * check the return value, and behave properly in case of error.
 */
#if 0
	if(request_mem_region(regs->start, resource_size(regs), regs->name) == NULL )
	{
		dev_err(&pdev->dev, "unable to obtain I/O memory address 0x%x\n", regs->start);
		return -EBUSY;
	}
#endif


	/* Instead of the devm_ioremap_resource, go with low level ioremap, without requsting memory region... */

/* NOTICE:
 *   Using request_mem_region() and ioremap() in device drivers is now deprecated.
 *   You should use the below "managed" functions instead, which simplify driver coding and error handling:
 *   	▶ devm_ioremap()
 *   	▶ devm_iounmap()
 *   	▶ devm_ioremap_resource()
 *   		▶ Takes care of both the request and remapping operations!
 */
#if 0
	gpioDev->regs  = ioremap(regs->start, resource_size(regs));
	if (!gpioDev->regs) {
		dev_err(&pdev->dev, "could not remap memory\n");
		return -1;
	}
#endif
	gpioDev->regs = devm_ioremap(&pdev->dev, regs->start, resource_size(regs));
	if (gpioDev->regs == NULL) {
		dev_err(&pdev->dev, "failed to ioremap() registers\n");
		return -ENODEV;
	}
//	pr_info("\nvirtual address: 0x%x!!!\n", (int)gpioDev->regs); //virtual address: 0xf2200000

	/* Create sysfs entries for all pins passed as module arguments */
	//static DEVICE_ATTR(testgpio, S_IWUSR | S_IRUGO, test_gpio_show, test_gpio_store);
	if (gpio_argc > 0) {
		// dinamically allocated array of device_attribute structs
		gpioDev->dev_attr = devm_kzalloc(&pdev->dev, gpio_argc * sizeof(struct device_attribute), GFP_ATOMIC);
		// dinamically allocated array of sysfs entry names
		gpioDev->sysfiles = devm_kzalloc(&pdev->dev, gpio_argc * sizeof(gpioDev->sysfiles), GFP_ATOMIC);
		for (i = 0; i < gpio_argc; i++) {
			snprintf(name, sizeof(name), "testgpio%d", gpio[i]);
	//		gpioDev->dev_attr[i] = __ATTR(testgpio, S_IWUSR | S_IRUGO, test_gpio_show, test_gpio_store);
			gpioDev->sysfiles[i] = devm_kzalloc(&pdev->dev, strlen(name)+1, GFP_ATOMIC);
			strcpy(gpioDev->sysfiles[i], name);

			gpioDev->dev_attr[i] = devm_kzalloc(&pdev->dev, sizeof(struct device_attribute), GFP_ATOMIC);
			gpioDev->dev_attr[i]->attr.name = gpioDev->sysfiles[i];
			gpioDev->dev_attr[i]->attr.mode = VERIFY_OCTAL_PERMISSIONS(S_IWUSR | S_IRUGO);
			gpioDev->dev_attr[i]->show	= test_gpio_show;
			gpioDev->dev_attr[i]->store	= test_gpio_store;
			device_create_file(&pdev->dev, gpioDev->dev_attr[i]);
		}
	}

	/* IMPLEMENTATION OF CHARACTER DRIVER USING MISC FRAMEWORK
	 *
	 * MISC subsistem is a thin layer above the characted driver API. It is intended for devices that really do not fit in
	 * any of the existing frameworks (input, network, video, audio, etc).
	 * Another advantage is that devices are integrated in the Device Model (device ﬁles appearing in devtmpfs,
	 * which is not the case with raw character devices).
	 */
	/* At the end of the probe() routine, when the device is fully ready to work, the miscdevice structure is initialized for each found device:
	 * • To get an automatically assigned minor number.
	 * • To specify a name for the device ﬁle in devtmpfs. We propose to use devm_kasprintf(&pdev->dev, GFP_KERNEL, "test_gpio-%x", res->start).
	 *   devm_kasprintf() allocates a buﬀer and runs ksprintf() to ﬁll its contents.
	 * • To pass the ﬁle operations structure prevously defined.
	 */
	gpioDev->miscdev.fops = &test_gpio_fops;
	gpioDev->miscdev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "test_gpio-%x", regs->start);
	gpioDev->miscdev.minor = MISC_DYNAMIC_MINOR;
	err = misc_register(&gpioDev->miscdev);
	if (err < 0)
		return err;

	/* In order to deal with usual constraint of handling multiple devices, miscdev struct is added to our driver specifc private data structure.
	 * To be able to access our private data structure in other parts of the driver, dev struct is attached to the pdev structure using the
	 * platform_set_drvdata() function.
	 */
	platform_set_drvdata(pdev, gpioDev);



	/*  device tree:
	 *      interrupts = <2 19>;
	 *
	 *  platform_get_irq:
	 *      struct resource *r;
	 *      r = platform_get_resource(dev, IORESOURCE_IRQ, num);
	 *      return r->start;
	 *
	 * it returns IRQ number index (r->start)
	 */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "could not get IRQ\n");
		return irq;
	}
//	pr_info("\nIRQ number index: %d\n", irq);

	/* Register interrupt handler */
	err = request_irq(irq, test_gpio_interrupt, IRQF_SHARED, dev_name(&pdev->dev), gpioDev);
	if (err) {
		dev_err(&pdev->dev, "could not request IRQ: %d\n", err);
		goto out_irq_error;
	}

	/* Then, pass the interrupt number to devm_request_irq() along with the interrupt handler to
	register your interrupt in the kernel. */
	err = devm_request_irq(&pdev->dev, irq, test_gpio_interrupt, IRQF_SHARED, "test_gpio_int", gpioDev);
	if (err) {
		dev_err(&pdev->dev, "devm_request_irq error: %d\n", err);
		goto out_irq_error;
	}

	gpioDev->irq = irq;

	/* SUMMARY:
 * In device tree (bcm2708_common.dtsi), "test_gpio" node is defined as child node of the "soc".
 * "test_gpio" has reg property (refers to a range of units in a register space):
 * 	reg = <0x7e200000 0xb4>;
 * Notice that information about number of cells is held in declarations of ancestor node (soc):
 * 	#address-cells = <1>;
	#size-cells = <1>;
 * That means, 0x7e200000 is start address and 0xb4 is size (length) of the range.
 * 0x7e200000 is BUS ADDRESS, ref to Broadcom-BC2835-datasheet.pdf, pages 5 and 6.
 *
 * From driver, this range is retrieved using platform_get_resource function with IORESOURCE_MEM parameter, but
 * PHYSICAL 0x20200000 address is returned!!
 *
 * Physical address needs to be translated to VIRTUAL ADDRESS IN KERNEL MODE 0xf2200000 using devm_ioremap()
 */

	return err;

	out_irq_error:
		test_gpio_remove(pdev);
		return err;
}



static struct platform_driver test_gpio_driver = {
	.driver = {
		.name = "test_gpio",
		.owner = THIS_MODULE,
		.of_match_table = test_gpio_dt_match,
	},
	.probe = test_gpio_probe,
	.remove = test_gpio_remove,
};

module_platform_driver(test_gpio_driver);

MODULE_AUTHOR("Stevan Bogic <bogics@gmail.com>");
MODULE_DESCRIPTION("Raspberry Pi GPIO kernel module");
MODULE_LICENSE("GPL"); // Get rid of taint message by declaring code as GPL.
