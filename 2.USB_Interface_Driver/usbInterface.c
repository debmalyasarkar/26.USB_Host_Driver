/* This driver gathers Interface and Endpoint details from the USB Device */
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>

/* Sandisk Cruzer Data Flash Identification Codes */
#define VENDOR_ID 0x0781
#define DEVICE_ID 0x5567

/* Private Structure*/
struct driver_private
{
  /* Private usb_device structure */
  struct usb_device *usb_dev;
  /* Private usb_interface structure */
  struct usb_interface *usb_intf;
  /* Buffer to Receive Data */
  unsigned char *bulk_in_buffer;
  /* Max Packet Size of the bulk endpoint */
  size_t bulk_in_size;
  /* The address of the bulk_in endpoint */
  __u8 bulk_in_endpointAddr;
  /* The address of the bulk_out endpoint*/
  __u8 bulk_out_endpointAddr;
  /* kref count refers to active references of this structure */
  struct kref kref;
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
  /* Free the memory allocated for driver private structure */
  kfree(dev);
}

static int usb_interface_drv_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
  struct driver_private *dev;
  struct usb_host_interface *iface_desc;
  struct usb_endpoint_descriptor *endpoint;
  int ii;

  pr_info("Interface Driver : %s Invoked\r\n", __func__);
  
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
      dev->bulk_in_size = __le16_to_cpu(endpoint->wMaxPacketSize);
      /* Allocate buffer to receive data from bulk_in endpoint */
      dev->bulk_in_buffer = kmalloc(dev->bulk_in_size, GFP_KERNEL);
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
    pr_err("Could Not Find both bulk_in or bulk_out endpoints\r\n");
    kref_put(&dev->kref, usb_cleanup);
    return -ENOMEM;
  }
  /* Save our private data pointer in interface device */
  usb_set_intfdata(intf, dev);
  return 0;
}

static void usb_interface_drv_disconnect(struct usb_interface *intf)
{
  struct driver_private *dev;

  pr_info("Interface Driver : %s Invoked\r\n", __func__);
  
  /* Fetch private data pointer from interface device where it was saved */
  dev = usb_get_intfdata(intf);
  /* Clear the interface device field data */
  usb_set_intfdata(intf, NULL);
  /* Free Allocated Memory */
  kref_put(&dev->kref, usb_cleanup);
}

static struct usb_device_id usb_drv_mtable[] =
{
  {USB_DEVICE(VENDOR_ID, DEVICE_ID)},
  {}
};

static struct usb_driver usb_drv = 
{
  .name       = "usb_interface_drv",
  .probe      = usb_interface_drv_probe,
  .disconnect = usb_interface_drv_disconnect,
  .id_table   = usb_drv_mtable,
};

static int __init usb_interface_drv_init(void)
{
  int ret;

  pr_info("Interface Driver : %s Invoked\r\n", __func__);
  /* Registers a USB Interface Driver with the USB Core */
  ret = usb_register(&usb_drv);
  return ret;
}

static void __exit usb_interface_drv_exit(void)
{
  pr_info("Interface Driver : %s Invoked\r\n", __func__);
  /* Deregister USB Interface Driver from the USB Core */
  usb_deregister(&usb_drv);
}

module_init(usb_interface_drv_init);
module_exit(usb_interface_drv_exit);

MODULE_AUTHOR("debmalyasarkar1@gmail.com");
MODULE_DESCRIPTION("USB Flash Storage Interface Driver");
MODULE_LICENSE("GPL");

