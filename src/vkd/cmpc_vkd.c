/*
 * Virtual Key Driver
 * Copyright (C) 2008  Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#include "cmpc_vkd.h"


static int __init cmpc_vkd_init(void)
{
	dbg_print("----->\n");
	
	/** get memory forstruct input_dev *input; device struct */
	vkd_dev = kzalloc(sizeof(struct cmpc_vkd_dev), GFP_KERNEL);
	if (!vkd_dev) {
		dbg_print("kmalloc for device struct failed\n");
		dbg_print("<-----\n");
		goto fail0;
	}

	/** make dir */
	vkd_class_dir = proc_mkdir(CMPC_CLASS_NAME, acpi_root_dir);
	if (!vkd_class_dir) {
		dbg_print("proc_mkdir failed\n");
		goto fail1;
	}
	
	/** register bus driver */
	if (acpi_bus_register_driver(&vkd_drv) < 0) {
		dbg_print("acpi_bus_register_driver failed\n");
		goto fail2;
	}

	init_waitqueue_head(&outq);
	
	que_head = 0;
	que_tail = -1;
	num = 0;

	dbg_print("<-----\n");
	return 0;

fail2:
	remove_proc_entry(CMPC_CLASS_NAME, acpi_root_dir);

fail1:
	kfree(vkd_dev);

fail0:
	dbg_print("<-----\n");
	return -1;
}

static void __exit cmpc_vkd_exit(void)
{
	dbg_print("----->\n");

	acpi_bus_unregister_driver(&vkd_drv);
	remove_proc_entry(CMPC_CLASS_NAME, acpi_root_dir);

	kfree(vkd_dev);
	
	dbg_print("<-----\n");
}

static int cmpc_vkd_open(struct inode *inode, struct file *file)
{
	dbg_print("----->\n");
	dbg_print("<-----\n");
	return 0;
}

static int cmpc_vkd_release(struct inode *inode, struct file *file)
{
	dbg_print("----->\n");
	if (flag) 
                //notify the data is available
                wake_up_interruptible(&outq);

	dbg_print("<-----\n");
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
static long cmpc_vkd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
#else
static int cmpc_vkd_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
#endif
{
        dbg_print("----->\n");
#ifdef MATCHPOINT
        if ( cmd == CMPC_ACPI_GET_LED_STATUS ){
                if(!ACPI_SUCCESS(cmpc_get_led_status(arg))) {
                return -EFAULT;
                }
        }

        else if ( cmd == CMPC_ACPI_SET_LED_ON ){
		dbg_print("light on\n");
                if(!ACPI_SUCCESS(cmpc_set_led_status(0x01))) {
                return -EFAULT;
                }
        }

        else if ( cmd == CMPC_ACPI_SET_LED_OFF ){
		dbg_print("light off\n");
                if(!ACPI_SUCCESS(cmpc_set_led_status(0x00))) {
                return -EFAULT;
                }
        }
        else
                return -EINVAL;
#endif
        dbg_print("<-----\n");
        return 0;
}

#ifdef MATCHPOINT
static acpi_status cmpc_set_led_status(unsigned long arg)
{
        union acpi_object param;
        struct acpi_object_list input;
        unsigned long long output;
        acpi_status status;

        param.type = ACPI_TYPE_INTEGER;
        param.integer.value = arg;
        input.count = 1;
        input.pointer = &param;

        if (ACPI_SUCCESS(status = acpi_evaluate_integer(vkd_dev->device->handle, "WLED", &input, &output)))
                dbg_print("Set LED success\n");
        return status;
}

static acpi_status cmpc_get_led_status(unsigned long arg)
{
        unsigned long long output;
        acpi_status status;
        int select;
        status = acpi_evaluate_integer(vkd_dev->device->handle, "RLED", NULL, &output);
        if (ACPI_SUCCESS(status))
                select = output;
        if (copy_to_user((void __user *)arg, &select, sizeof(select)))
                return -EFAULT;
        return status;
}
#endif

static ssize_t cmpc_vkd_read(struct file *file, char __user *buf,
	size_t size, loff_t *ppos)
{
	u32 to_user_event;

	if (num <= 0)
	{
		num = 0;
		if (wait_event_interruptible(outq, flag) != 0)
		{
			return - ERESTARTSYS;
		}
	}
	flag = 0;
	if(num <= 0)
		return 0;
	que_tail = (que_tail+1)%MAX_NUM;
	to_user_event = fifo_que[que_tail];
	if (copy_to_user(buf, &to_user_event, sizeof(u32)))
	{
		dbg_print("copy to user failed\n");
		return - EFAULT;
	}
	num --;
	return sizeof(u32);
}

static ssize_t cmpc_vkd_write(struct file *file, const char __user *buf,
	size_t size, loff_t *ppos)
{
	dbg_print("----->\n");
	dbg_print("<-----\n");
	return 0;
}

static loff_t cmpc_vkd_llseek(struct file *file, loff_t offset, int orig)
{
	dbg_print("----->\n");
	dbg_print("<-----\n");
	return 0;
}

static void cmpc_vkd_handler(acpi_handle handle, u32 event, void *data)
{
	
	if(num == MAX_NUM)
	{
		dbg_print("fifo que is full. \n");
	}
	else
	{
		fifo_que[que_head] = event;
		que_head = (que_head+1)%MAX_NUM;
		num ++;
		flag = 1;
		wake_up_interruptible(&outq);
	}

}

static int cmpc_vkd_add(struct acpi_device *device) {
	
	dbg_print("----->\n");

	vkd_entry = create_proc_entry(CMPC_VKD_NAME, S_IRUGO, acpi_root_dir);
	
	if (!vkd_entry) {
		dbg_print("create_proc_entry failed\n");
		goto fail0;
	}	
	vkd_entry->data = vkd_dev;
	vkd_entry->proc_fops = &vkd_fops;
	
	vkd_dev->device = device;
	(device)->driver_data = vkd_dev; 
	strcpy(acpi_device_name(device), CMPC_VKD_NAME);
	sprintf(acpi_device_class(device), "%s/%s", CMPC_CLASS_NAME,
		CMPC_VKD_NAME);

	/** install acpi notify handler */
	if (!ACPI_SUCCESS(acpi_install_notify_handler(device->handle,
		ACPI_DEVICE_NOTIFY, cmpc_vkd_handler, vkd_dev))) {
		dbg_print("acpi_install_notify_handler failed\n");
		goto fail1;
	}

	dbg_print("<-----\n");
	return 0;

fail1:
	remove_proc_entry(CMPC_VKD_NAME, acpi_root_dir);
	
fail0:
	dbg_print("<-----\n");
	return -ENODEV;
}

static int cmpc_vkd_resume(struct acpi_device *device) {
	dbg_print("----->\n");
	dbg_print("<-----\n");
	return 0;
}

static int cmpc_vkd_remove(struct acpi_device *device, int type) {
	dbg_print("----->\n");
	acpi_remove_notify_handler(vkd_dev->device->handle, ACPI_DEVICE_NOTIFY, 
		cmpc_vkd_handler);
	remove_proc_entry(CMPC_VKD_NAME, acpi_root_dir);
	dbg_print("<-----\n");
	return 0;
}

module_init(cmpc_vkd_init);
module_exit(cmpc_vkd_exit);
