/* This driver runs the probes and lists the USB device properties */
#include <linux/module.h>
#include <linux/usb.h>

/* Sandisk Cruzer Data Flash Identification Codes */
#define VENDOR_ID 0x0781
#define DEVICE_ID 0x5567

static int usb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
  /* Get the usb_device structure reference from the interface */
  /* This structure is the kernel's representation of a usb device */
  struct usb_device *dev = interface_to_usbdev(intf);

  pr_info("Interface Driver : %s Invoked\r\n", __func__);

  /* Access the fields of the usb device */
  pr_info("Device Number   = %d\r\n",dev->devnum);  
  pr_info("Device Speed    = %d\r\n",dev->speed);  
  pr_info("Vendor ID       = 0x%4hX\r\n",dev->descriptor.idVendor);  
  pr_info("Product ID      = 0x%4hX\r\n",dev->descriptor.idProduct);  
  pr_info("Device BCD      = 0x%hX\r\n",dev->descriptor.bcdDevice);  
  pr_info("Device Class    = 0x%hX\r\n",dev->descriptor.bDeviceClass);
  pr_info("Device SubClass = 0x%hX\r\n",dev->descriptor.bDeviceSubClass);  
  pr_info("Device Protocol = 0x%hX\r\n",dev->descriptor.bDeviceProtocol);  
  pr_info("PacketSize      = %hu\r\n",dev->descriptor.bMaxPacketSize0);  
  pr_info("Manufacturer    = %s\r\n",dev->manufacturer);
  pr_info("Product         = %s\r\n",dev->product);
  pr_info("Serial          = %s\r\n",dev->serial);
  /* There are also separate iManufacturer, iProduct and iSerialNumber 
     Fields inside the descriptor structure field of the usb_device structure */
  return 0;
}

static void usb_disconnect(struct usb_interface *intf)
{
  pr_info("Interface Driver : %s Invoked\r\n", __func__);
}

static struct usb_device_id usb_drv_mtable[] =
{
  {USB_DEVICE(VENDOR_ID, DEVICE_ID)},
  {}
};

static struct usb_driver usb_drv = 
{
  .name       = "usb_probe_drv",
  .probe      = usb_probe,
  .disconnect = usb_disconnect,
  .id_table   = usb_drv_mtable,
};

static int __init usbprobe_init(void)
{
  int ret;

  pr_info("Interface Driver : %s Invoked\r\n", __func__);
  /* Registers a USB Interface Driver with the USB Core */
  ret = usb_register(&usb_drv);
  return ret;
}

static void __exit usbprobe_exit(void)
{
  pr_info("Interface Driver : %s Invoked\r\n", __func__);
  /* Deregister USB Interface Driver from the USB Core */
  usb_deregister(&usb_drv);
}

module_init(usbprobe_init);
module_exit(usbprobe_exit);

MODULE_AUTHOR("debmalyasarkar1@gmail.com");
MODULE_DESCRIPTION("USB Flash Storage Probing Driver");
MODULE_LICENSE("GPL");

