#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/ktime.h>
#include <linux/delay.h>


#define DEVICE_NAME "timotwofx"
#define Max_buff_size                         1024 
#define TIMO_SPI_DEVICE_BUSY_IRQ_MASK (1 << 7)


struct my_spi_device {
    u8 dataRead[Max_buff_size];
    u8 nbdatafromRead;
    u8 dataSend[Max_buff_size];
    u8 nbdatatosend;
	struct spi_device *spi;
	struct gpio_desc *irq_gpio;
    struct gpio_desc *cs_gpio;
	int irq_number;
	int id;
	dev_t devt;
	struct cdev cdev;
    struct device *device;
    bool irqEdge;
};

static struct my_spi_device *device_list;
static int device_count;
static DEFINE_MUTEX(device_list_lock);
static struct class *timotwofxClass;
bool irq_is_pending(struct my_spi_device *device);

static irqreturn_t my_irq_handler(int irq, void *dev_id){
    struct my_spi_device *dev = (struct my_spi_device *)dev_id;
    device_list[dev->id].irqEdge = true;
    return IRQ_HANDLED;
}

bool irq_is_pending(struct my_spi_device *device){
    bool pending =  device->irqEdge;
    device->irqEdge = false;
    return pending;
}

static int timotwo_open(struct inode *inode,struct file *file){

    int minor = iminor(inode);
    file->private_data = & device_list[minor];
    return 0;

}


static ssize_t timotwofx_read(struct file *file , char __user *to ,size_t count ,loff_t *offset){

    struct my_spi_device * dev = (struct my_spi_device*)file->private_data;

    if(*offset >= dev->nbdatafromRead)
		return 0;
    
    if(count > dev->nbdatafromRead - *offset)
        count = dev->nbdatafromRead - *offset;

    if (count > dev->nbdatafromRead)
        count = dev->nbdatafromRead;

    copy_to_user(to,dev->dataRead,count);
    *offset += count; 
	return count; 

}


static ssize_t timotwofx_write(struct file *file , const char __user *from , size_t count , loff_t *offset){
    struct my_spi_device * dev = (struct my_spi_device*)file->private_data;
    int i=0;
    u8 CMD =0;
    u8 irq_flags=0;
    ktime_t start;

    if (count > Max_buff_size)
    {
       count = Max_buff_size;
    }
    copy_from_user(dev->dataSend,from,count);
    dev->nbdatatosend = count;
    CMD = dev->dataSend[0];
    for (i = 0; i < count - 1; i++) {
        dev->dataSend[i] = dev->dataSend[i + 1];
    }
    dev->dataSend[count-1] = 0;
    dev->nbdatatosend --;

    start = ktime_get();
    gpiod_set_value(dev->cs_gpio,0);
    irq_flags = spi_w8r8(dev->spi,CMD);
    
    if (irq_flags < 0) {
        dev->dataRead[0] = irq_flags;
        count = 0;
        return -1;
    }

    irq_is_pending(dev);
    gpiod_set_value(dev->cs_gpio,1);


    if (dev->nbdatatosend ==0){
        // NOP command
        start = ktime_get();
        while((!gpiod_get_value(dev->irq_gpio)) && (!irq_is_pending(dev))){
            if (ktime_to_ms(ktime_sub(ktime_get(),start)) > 10)
            {
                break;
            }
            
        }
        dev->dataRead[0] = irq_flags;
        dev->nbdatafromRead = count;
        return count;
    }


    while (!irq_is_pending(dev))    
    {
        if (ktime_to_ms(ktime_sub(ktime_get(),start)) > 10)
            {
                return -1;
            }
    }

    gpiod_set_value(dev->cs_gpio,0);

    irq_flags = spi_w8r8(dev->spi,0xFF);

    if (irq_flags & TIMO_SPI_DEVICE_BUSY_IRQ_MASK)
    {
        gpiod_set_value(dev->cs_gpio,1);
        dev->dataRead[0] = irq_flags;
        dev->nbdatafromRead = 1;
        count = 0;
        return -1;
    }
    spi_write_then_read(dev->spi,dev->dataSend,dev->nbdatatosend,&dev->dataRead[1],dev->nbdatatosend);
    gpiod_set_value(dev->cs_gpio,0);


    while(!gpiod_get_value(dev->irq_gpio)){
        if (ktime_to_ms(ktime_sub(ktime_get(),start)) > 10)
        {
            break;
        }
        
    } 
    
    dev->dataRead[0] = irq_flags;

    return count;
    
}


static const struct file_operations timo_fops = {
	.owner = THIS_MODULE,
	.open = timotwo_open,
    .read = timotwofx_read,
    .write = timotwofx_write
};


static int timotwofx_probe(struct spi_device *spi){
    struct my_spi_device *dev;
	int ret;
	char dv_name[32];

	mutex_lock(&device_list_lock);
    dev = kzalloc(sizeof(*dev),GFP_KERNEL);
    if (!dev) {
		mutex_unlock(&device_list_lock);
		return -ENOMEM;
	}

   if (device_count == 0){
    device_list = kcalloc(1,sizeof(*device_list),GFP_KERNEL);
    if (!device_list) {
        mutex_unlock(&device_list_lock);
        return -ENOMEM;
    }
   timotwofxClass = class_create(DEVICE_NAME);
   }else
   {
    device_list = krealloc_array(device_list,device_count+1,sizeof(*device_list),GFP_KERNEL);
    
    if (!device_list) {
        class_destroy(timotwofxClass);
        mutex_unlock(&device_list_lock);
        return -ENOMEM;
    }

   }

   dev->id = device_count;
   dev->devt = MKDEV(153,device_count);
   device_count ++;
   mutex_unlock(&device_list_lock);
   dev->spi = spi;
   dev->cs_gpio = devm_gpiod_get(&spi->dev, "cs", GPIOD_OUT_HIGH);
   if (IS_ERR(dev->cs_gpio)) {
    class_destroy(timotwofxClass);
    return PTR_ERR(dev->cs_gpio);
    }

   dev->irq_gpio = devm_gpiod_get(&spi->dev , "irq",GPIOD_IN);
   if (IS_ERR(dev->irq_gpio)) {
    class_destroy(timotwofxClass);
    return PTR_ERR(dev->irq_gpio);
    }
   dev->irq_number = gpiod_to_irq(dev->irq_gpio);
   if (dev->irq_number < 0){
    class_destroy(timotwofxClass);
    return dev->irq_number;
   }
	    
    
    ret=devm_request_irq(&spi->dev,dev->irq_number,my_irq_handler,IRQF_TRIGGER_FALLING,dev_name(&spi->dev), dev);
    if (ret){
        class_destroy(timotwofxClass);
        return ret;
    }
      
    /* Create a char device node */
    cdev_init(&dev->cdev, &timo_fops);
    dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->cdev, dev->devt, 1);
	if (ret){
        class_destroy(timotwofxClass);
        return ret;
    }
		
    snprintf(dv_name,sizeof(dv_name),"%s%d",DEVICE_NAME,dev->id);
   dev->device = device_create(timotwofxClass,&spi->dev,dev->devt,NULL,dv_name);
   if (IS_ERR(dev->device))
   {
    cdev_del(&dev->cdev);
    class_destroy(timotwofxClass);
    return PTR_ERR(dev->device);
   }
   


   device_list[device_count] = *dev;
   
   return 0;
   
}


/* Remove function */
static void timotwofx_remove(struct spi_device *spi){
    struct my_spi_device *dev = NULL;
	int i;

    mutex_lock(&device_list_lock);

    for ( i = 0; i < device_count; i++)
    {
        if (device_list[i].spi == spi)
        {
            dev = & device_list[i];
            break;
        }
        
    }
    device_count--;
    mutex_unlock(&device_list_lock);

   

    device_destroy(timotwofxClass,dev->devt);
    cdev_del(&dev->cdev);
	class_destroy(timotwofxClass);
    unregister_chrdev_region(dev->devt, 1);
  
}


static const struct of_device_id my_spi_of_match[] = {
	{ .compatible = "whm,timotwofx_driver" },
	{ }
};

static struct spi_driver my_spi_driver = {
	.driver = {
		.name = "timotwofx_driver",
		.owner = THIS_MODULE,
		.of_match_table = my_spi_of_match,
	},
	.probe = timotwofx_probe,
	.remove = timotwofx_remove,
};

module_spi_driver(my_spi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Wissem Haj Mohamed");
MODULE_DESCRIPTION("timotwofx caracter device driver ");
