/* This driver gathers Interface and Endpoint details from the USB Device */
/* It also shows the usage of an URB for Read/Write Operations to Device */
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>

/* Sandisk Cruzer Data Flash Identification Codes */
#define VENDOR_ID 0x0781
#define DEVICE_ID 0x5567

#if 0
/* Get actual device number range for usb devices from usb maintainer */
/* List of USB Device No Mappings - http://www.linux-usb.org/usb.devices.txt */
#define USB_MINOR_BASE 192
#endif

static struct usb_driver usb_drv;

/* Private Structure */
struct driver_private
{
  /* Private usb_device structure */
  struct usb_device *usb_dev;
  /* Private usb_interface structure */
  struct usb_interface *usb_intf; 
  /* kref count refers to active references of this structure */
  struct kref kref;

  /* URB used for receiving data from bulk_in endpoint */
  struct urb *bulk_in_urb;
  /* Structure used to maintain the state of completion of an event */
  struct completion bulk_in_completion;
  /* The address of the bulk_in endpoint */
  unsigned int bulk_in_endpointAddr; 
  /* Buffer to store received data */
  unsigned char *bulk_in_buffer;
  /* Max packet size of the bulk_in endpoint */
  size_t bulk_in_max_size;
  /* No of bytes actually read from bulk_in endpoint */
  size_t bulk_in_read_bytes; 
  /* Save the error state of URB used for reading from bulk_in endpoint */
  int bulk_in_errors;

  /* URB used for sending data from bulk_out endpoint */
  struct urb *bulk_out_urb;
  /* Structure used to maintain the state of completion of an event */
  struct completion bulk_out_completion;
  /* The address of the bulk_out endpoint */
  unsigned int bulk_out_endpointAddr; 
  /* Buffer to store data for sending */
  unsigned char *bulk_out_buffer;
  /* Max packet size of the bulk_out endpoint */
  size_t bulk_out_max_size;
  /* No of bytes actually written to bulk_out endpoint */
  size_t bulk_out_write_bytes; 
  /* Save the error state of URB used for writing to bulk_out endpoint */
  int bulk_out_errors;
};

#define get_driver_private(ptr) container_of(ptr, struct driver_private, kref)

static void usb_cleanup(struct kref *kref)
{
  struct driver_private *dev;
 
  /* Fetch the parent private structure from kref field */
  dev = get_driver_private(kref);
  /* Decrement the kref count */
  usb_put_dev(dev->usb_dev);
  /* Free the memory allocated for bulk_in receive buffer */
  kfree(dev->bulk_in_buffer);
  /* Free the memory allocated for bulk_out transmit buffer */
  kfree(dev->bulk_out_buffer);
  /* Free the memory allocated for driver private structure */
  kfree(dev);
}

/* Called when the submitted URB transfer is completed */
static void drv_read_bulk_callback(struct urb *urb)
{
  struct driver_private *dev;
  
  /* Restore driver private structure from URB */
  dev = urb->context;

  /* Check status of the URB transaction */
  if(urb->status) 
  {
    if(!(urb->status == -ENOENT || urb->status == -ECONNRESET || urb->status == -ESHUTDOWN))
      dev_err(&dev->usb_intf->dev,"%s - nonzero write bulk status received: %d\n",__func__, urb->status);

    dev->bulk_in_errors = urb->status;
  } 
  else 
  {
    /* Save the number of bytes actually transferred */
    dev->bulk_in_read_bytes = urb->actual_length;
  }
  /* Signal thread associated with read_completion to wake up */
  complete(&dev->bulk_in_completion);
}


static ssize_t drv_read(struct file *file, char *buffer, size_t count, loff_t *off)
{
  struct driver_private *dev;
  int retval = 0;

  pr_info("USB Flash Storage Driver : %s Invoked\r\n", __func__);

  /* Restore driver private structure from file's private structure */
  dev = file->private_data;

  /* If we cannot read at all, return EOF */
  if(!dev->bulk_in_urb || !count)
    return 0;

  /* Create an URB for the USB driver to use for data transfer */
  /* For bulk endpoints, the first argument has to be 0 */
  dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
  if (!dev->bulk_in_urb) 
  {
    dev_err(&dev->usb_dev->dev,"Could not allocate bulk_in_urb\n");
    return -ENOMEM;
  }

  /* Allocate buffer to receive data */
  dev->bulk_in_buffer = kmalloc(dev->bulk_in_max_size, GFP_KERNEL);
  if (!dev->bulk_in_buffer)
  {
    dev_err(&dev->usb_dev->dev,"Could not allocate bulk_in_buffer\n");
  }

  /* Initialize URB */
  usb_fill_bulk_urb(dev->bulk_in_urb, dev->usb_dev,
                    usb_rcvbulkpipe(dev->usb_dev, dev->bulk_in_endpointAddr),
                    dev->bulk_in_buffer, min(dev->bulk_in_max_size, count),
                    drv_read_bulk_callback, dev);

  /* Submit URB to receive data via bulk_in_urb */
  retval = usb_submit_urb(dev->bulk_in_urb, GFP_KERNEL);
  if(retval < 0) 
  {
    dev_err(&dev->usb_dev->dev,"%s - Failed submitting read urb, error %d\n",__func__, retval);
  }

  /* Wait for the task to complete */
  retval = wait_for_completion_interruptible(&dev->bulk_in_completion);
  if(retval < 0)
    goto exit;
	
  /* Copy data read to user buffer and return the number of bytes read */
  if(copy_to_user(buffer, dev->bulk_in_buffer, dev->bulk_in_read_bytes))
    retval = -EFAULT;
  else
    retval = dev->bulk_in_read_bytes;
exit:
  return retval;
}

/* Called when the submitted URB transfer is completed */
static void drv_write_bulk_callback(struct urb *urb)
{
  struct driver_private *dev;

  /* Restore driver private structure from URB */
  dev = urb->context;

  /* Check status of the URB transaction */
  if(urb->status) 
  {
    if(!(urb->status == -ENOENT ||urb->status == -ECONNRESET || urb->status == -ESHUTDOWN))
      dev_err(&dev->usb_intf->dev,"%s - Non zero write bulk status received: %d\n",__func__, urb->status);

    dev->bulk_in_errors = urb->status;
  }

  /* Free up the allocated buffer */
  usb_free_coherent(urb->dev, urb->transfer_buffer_length, urb->transfer_buffer, urb->transfer_dma);
}

static ssize_t drv_write(struct file *file, const char *user_buffer, size_t count, loff_t *off)
{
  struct driver_private *dev;
  int retval = 0;

  pr_info("USB Flash Storage Driver : %s Invoked\r\n", __func__);

  /* Restore driver private structure from file's private structure */
  dev = file->private_data;

  /* Fill up the number of bytes to write */
  dev->bulk_out_write_bytes = count;

  /* Verify that we actually have some data to write */
  if(count == 0)
    goto exit;

  /* Create an URB for the USB driver to use for data transfer */
  /* For bulk endpoints, the first argument has to be 0 */
  dev->bulk_out_urb = usb_alloc_urb(0, GFP_KERNEL);
  if(NULL == dev->bulk_out_urb) 
  {
    retval = -ENOMEM;
    goto error;
  }

  /* Allocate DMA coherent buffer to transfer payload */
  dev->bulk_out_buffer = usb_alloc_coherent(dev->usb_dev, dev->bulk_out_write_bytes, GFP_KERNEL, &dev->bulk_out_urb->transfer_dma);
  if(!dev->bulk_out_buffer) 
  {
    retval = -ENOMEM;
    goto error;
  }

  /* Write payload(usb device class protocol) into dma buffer */
  if(copy_from_user(dev->bulk_out_buffer, user_buffer, dev->bulk_out_write_bytes)) 
  {
    retval = -EFAULT;
    goto error;
  }

  /* Initialize URB */
  usb_fill_bulk_urb(dev->bulk_out_urb, dev->usb_dev,
                    usb_sndbulkpipe(dev->usb_dev, dev->bulk_out_endpointAddr),
                    dev->bulk_out_buffer, dev->bulk_out_write_bytes, 
                    drv_write_bulk_callback, dev);
        
  dev->bulk_out_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

  /* FIXME: Fails with Error Code -ENOENT */
  /* Send the data out the bulk port */
  retval = usb_submit_urb(dev->bulk_out_urb, GFP_KERNEL);
  if(retval) 
  {
    dev_err(&dev->usb_intf->dev,"%s - Failed submitting write urb, error %d\n",__func__, retval);
    goto error_unanchor;
  }

  /* Release our reference to this URB, the USB core will eventually free it entirely */
  usb_free_urb(dev->bulk_out_urb);

  /* Return the number of bytes written */
  return dev->bulk_out_write_bytes;

error_unanchor:
  usb_unanchor_urb(dev->bulk_out_urb);

error:
  if(dev->bulk_out_urb)
  {
    usb_free_coherent(dev->usb_dev, dev->bulk_out_write_bytes, dev->bulk_out_buffer, dev->bulk_out_urb->transfer_dma);
    usb_free_urb(dev->bulk_out_urb);
  }

exit:
  return retval;
}
 
static int drv_open(struct inode *inode, struct file *file)
{
  struct driver_private *dev;
  struct usb_interface *interface;
  int subminor;

  pr_info("USB Flash Storage Driver : %s Invoked\r\n", __func__);

  /* Find minor from the Inode */
  subminor  = iminor(inode);

  /* Get usb_interface reference of the usb_driver */
  interface = usb_find_interface(&usb_drv, subminor);
  if(!interface) 
  {
    pr_err("Could not find device with minor %d\r\n", subminor);
    return -ENODEV;
  }

  /* Save usb_driver's private structure to a local copy */
  dev = usb_get_intfdata(interface);
  if(!dev)
    return -ENODEV;

  /* FIXME : Causes errors which are reflected in dmesg log */
  /* Increment usage count for the device */
  kref_get(&dev->kref);

  /* Prevents the device from getting autosuspended until call is made to
     usb_autopm_set_interface() */
  if(!usb_autopm_get_interface(interface))
    return -ENODEV;

  /* Save driver private structure in the file's private structure */
  file->private_data = dev;
  return 0;
}

static int drv_release(struct inode *inode, struct file *file)
{
  struct driver_private *dev;

  pr_info("USB Flash Storage Driver : %s Invoked\r\n", __func__);

  /* Restore driver private structure from file's private structure */
  dev = file->private_data;
  if(NULL == dev)
    return -ENODEV;

  /* Allow the device to be autosuspended */
  if(dev->usb_intf)
    usb_autopm_put_interface(dev->usb_intf);

  /* Decrement usage count for the device */
  kref_put(&dev->kref, usb_cleanup);
  return 0;
}

static struct file_operations storage_ops = 
{
  .owner   = THIS_MODULE,
  .read    = drv_read,
  .write   = drv_write,
  .open    = drv_open,
  .release = drv_release,
};

/* USB Class Driver is initialized to get a minor number from the usb core
   and to have the device register with the usb core */
/* Sysfs entry is created using this driver structure */ 
static struct usb_class_driver storage_class = 
{
  .name = "storage%d",
  /* Char drivers file_operations object */
  .fops = &storage_ops,
#if 0 
  /* Can be used if we define an USB_MINOR_BASE */
  .minor_base = USB_MINOR_BASE,
#endif
};

static int drv_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
  struct driver_private *dev;
  struct usb_host_interface *iface_desc;
  struct usb_endpoint_descriptor *endpoint;
  int ii;

  pr_info("USB Flash Storage Driver : %s Invoked\r\n", __func__);
  
  /* Allocate Memory for Private Structure */
  dev = kzalloc(sizeof(struct driver_private), GFP_KERNEL);
  if(NULL == dev)
  {
    dev_err(&intf->dev, "Memory Allocation Failed\r\n");
    return -ENOMEM;
  }
  dev->usb_dev  = interface_to_usbdev(intf);
  dev->usb_intf = intf;
  
  /* The currently active alternate setting/interface */
  iface_desc = intf->cur_altsetting;

  /* This loop fetches the endpoints of the current interface and based on that
     it sets up the driver configuration data structures */
  for(ii = 0; ii < iface_desc->desc.bNumEndpoints; ii++)
  {
    /* Copy current interface endpoint descriptor to driver local structure */
    /* Use only first bulk_in and bulk_out endpoints */
    endpoint = &iface_desc->endpoint[ii].desc;
    
    /* Return true if endpoint has bulk transfer type and IN direction */
    if(!usb_endpoint_is_bulk_in(endpoint))
    {
      /* Get bulk_in endpoint address */
      dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
      /* Get max packet size */
      dev->bulk_in_max_size = __le16_to_cpu(endpoint->wMaxPacketSize);

      /* Allocate buffer to receive data from bulk_in endpoint */
      dev->bulk_in_buffer = kmalloc(dev->bulk_in_max_size, GFP_KERNEL);
      if(NULL == dev->bulk_in_buffer)
      {
        dev_err(&intf->dev, "Could Not Allocate bulk_in_buffer\r\n");
        kref_put(&dev->kref, usb_cleanup);
        return -ENOMEM;
      }
    }
    /* Return true if endpoint has bulk transfer type and OUT direction */
    if(!usb_endpoint_is_bulk_out(endpoint))
    {
      /* Get bulk_out endpoint address */
      dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
    }
  }
  /* Handle when bulk_in or bulk_out endpoints are not found */
  if(!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr))
  {
    dev_err(&intf->dev, "Could Not Find both bulk_in or bulk_out endpoints\r\n");
    kref_put(&dev->kref, usb_cleanup);
    return -ENOMEM;
  }
  /* Save our private data pointer in interface device */
  usb_set_intfdata(intf, dev);

  /* Register a USB Class Device and Ask for a Minor Number */
  /* Creates an entry for the USB Class Device in sysfs */
  /* Path -->  /sys/class/usbmisc/storage0 */
  if(0 != usb_register_dev(intf, &storage_class))
  {
    dev_err(&intf->dev, "Could Not Get Minor for this device\r\n");
    usb_set_intfdata(intf, NULL);
    kref_put(&dev->kref, usb_cleanup);
    return -EINVAL;
  }
  dev_info(&intf->dev, "USB Flash Storage Driver is attached to Minor No %d\r\n", intf->minor);
  return 0;
}

static void drv_disconnect(struct usb_interface *intf)
{
  struct driver_private *dev;

  pr_info("USB Flash Storage Driver : %s Invoked\r\n", __func__);
  
  /* Fetch private data pointer from interface device where it was saved */
  dev = usb_get_intfdata(intf);
  /* Clear the interface device field data */
  usb_set_intfdata(intf, NULL);
  /* Free Allocated Memory */
  kref_put(&dev->kref, usb_cleanup);
  /* Free the allocated minor for our device */
  usb_deregister_dev(intf, &storage_class);
}

/* Match device and vendor ID and load this driver */
static struct usb_device_id usb_drv_mtable[] =
{
  {USB_DEVICE(VENDOR_ID, DEVICE_ID)},
  {}
};

/* Initialize the driver structure */
static struct usb_driver usb_drv = 
{
  .name       = "usb_flash_storage_driver",
  .probe      = drv_probe,
  .disconnect = drv_disconnect,
  .id_table   = usb_drv_mtable,
};

static int __init usb_drv_init(void)
{
  int ret;

  pr_info("USB Flash Storage Driver : %s Invoked\r\n", __func__);
  /* Registers a USB Flash Storage Driver with the USB Core */
  ret = usb_register(&usb_drv);
  return ret;
}

static void __exit usb_drv_exit(void)
{
  pr_info("USB Flash Storage Driver : %s Invoked\r\n", __func__);
  /* Deregister USB Flash Storage Driver from the USB Core */
  usb_deregister(&usb_drv);
}

module_init(usb_drv_init);
module_exit(usb_drv_exit);

MODULE_AUTHOR("debmalyasarkar1@gmail.com");
MODULE_DESCRIPTION("USB Flash Storage Driver");
MODULE_LICENSE("GPL");
