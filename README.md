## Raspberry Pi GPIO kernel driver for Raspberry Pi.

Check the [Wiki](https://github.com/bogics/rpi_gpio_module/wiki) pages for details about  
- [Raspberry Pi Hardware](https://github.com/bogics/rpi_gpio_driver/wiki/Raspberry-Pi-Hardware)  
- [Device Tree](https://github.com/bogics/rpi_gpio_driver/wiki/Device-Tree)  
- [Software Implementation](https://github.com/bogics/rpi_gpio_driver/wiki/Software-Implementation)  

&nbsp;  
**Userspace** access to GPIO Device Driver can be obrained via **char device file** or **sysfs entries**.

When module is loaded, `/dev/test_gpio-20200000` character device is automatically created:
```
# insmod test_gpio.ko 
# ls -la /dev/test_gpio-20200000 
crw-------    1 root     root       10,  57 Jan  1 02:03 /dev/test_gpio-20200000
```
&nbsp;  
Module can optionally take an **argument**.  
The "gpio" argument is an array of integers which represents GPIO pins for which **sysfs entries** will be created.
```
e.g.:
# insmod test_gpio.ko gpio="17,26"

# ls -la /sys/devices/platform/soc/20200000.test_gpio/testgpio*
-rw-r--r--    1 root     root          4096 Jan  1 00:05 /sys/devices/platform/soc/20200000.test_gpio/testgpio17
-rw-r--r--    1 root     root          4096 Jan  1 00:05 /sys/devices/platform/soc/20200000.test_gpio/testgpio26
```

&nbsp;  

**Direction** of GPIO pin can be **input** or **output**.  
GPIO pin is set as **output** by writting **high** or **low**.  
GPIO pin is set as **input** by default, or by writting **in**.  

&nbsp;  
### Access via device file

To set pin as `output`, write `pin number` and `high` or `low` value to the `device file`:
```
# echo "17 high" > /dev/test_gpio-20200000
To set GPIO 17 to output low:
# echo "17 low" > /dev/test_gpio-20200000
```
To set pin as `input`, write `pin number` and `in` to the `device file`:  
`# echo "26 in" > /dev/test_gpio-20200000`  

To set interrupt for pin on `rising` egde, write `pin number` and `rising` to the `device file`:  
`# echo "26 rising" > /dev/test_gpio-20200000`  

To set interrupt for pin on `falling` egde, write `pin number` and `falling` to the `device file`:  
`# echo "26 falling" > /dev/test_gpio-20200000`

To disable interrupt for pin , write `pin number` and `none` to the `device file`:  
`# echo "26 none" > /dev/test_gpio-20200000`

`Read` from `device file` to get direction and value of all pins which direction is input or output:
```
# cat /dev/test_gpio-20200000 

  #   dir   value
  0. input    1
  1. input    1
  2. input    1

  ...
  ...
```

### Access through sysfs

To set pin as `output`, write `high` or `low` value to the corresponding `sysfs file`:  
`# echo high > /sys/devices/platform/soc/20200000.test_gpio/testgpio17`  
`# echo low > /sys/devices/platform/soc/20200000.test_gpio/testgpio17`  

To set pin as `input`, write `in` to the corresponding `sysfs file`:  
`# echo in > /sys/devices/platform/soc/20200000.test_gpio/testgpio26`  

To set interrupt for pin on `rising` egde, write `rising` to the corresponding `sysfs file`:  
`# echo rising > /sys/devices/platform/soc/20200000.test_gpio/testgpio26`  

To set interrupt for pin on `falling` egde, write `falling` to the corresponding `sysfs file`:  
`# echo falling > /sys/devices/platform/soc/20200000.test_gpio/testgpio26`  

To disable interrupt for pin , write `none` to the corresponding `sysfs file`:  
`# echo none > /sys/devices/platform/soc/20200000.test_gpio/testgpio26`  

To `read` value of some pin, read corresponding `sysfs file`:  
```
# cat /sys/devices/platform/soc/20200000.test_gpio/testgpio26
input: 1
```
