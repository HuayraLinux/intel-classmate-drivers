/*
* Copyright (C) 2008  Intel Corporation
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
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
/****************************************************************************

Copyright (c) Intel Corporation (2008).

DISCLAIMER OF WARRANTY
NEITHER INTEL NOR ITS SUPPLIERS MAKE ANY REPRESENTATION OR WARRANTY OR
CONDITION OF ANY KIND WHETHER EXPRESS OR IMPLIED (EITHER IN FACT OR BY
OPERATION OF LAW) WITH RESPECT TO THE SOURCE CODE.  INTEL AND ITS SUPPLIERS
EXPRESSLY DISCLAIM ALL WARRANTIES OR CONDITIONS OF MERCHANTABILITY OR
FITNESS FOR A PARTICULAR PURPOSE.  INTEL AND ITS SUPPLIERS DO NOT WARRANT
THAT THE SOURCE CODE IS ERROR-FREE OR THAT OPERATION OF THE SOURCE CODE WILL
BE SECURE OR UNINTERRUPTED AND HEREBY DISCLAIM ANY AND ALL LIABILITY ON
ACCOUNT THEREOF.  THERE IS ALSO NO IMPLIED WARRANTY OF NON-INFRINGEMENT.
SOURCE CODE IS LICENSED TO LICENSEE ON AN "AS IS" BASIS AND NEITHER INTEL
NOR ITS SUPPLIERS WILL PROVIDE ANY SUPPORT, ASSISTANCE, INSTALLATION,
TRAINING OR OTHER SERVICES.  INTEL AND ITS SUPPLIERS WILL NOT PROVIDE ANY
UPDATES, ENHANCEMENTS OR EXTENSIONS.


File Name:       cmpc_pm.c

****************************************************************************/
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>

#include <asm/uaccess.h>

#include <linux/module.h>

#include <acpi/acpi.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpiosxf.h>
#include "cmpc_pm.h"

#define CMPC_PM_MAJOR 240
#define CMPC_PM_MINOR 0
#define CMPC_PM_ACPI_NAME "ipml_pm"

#define ACPI_PATHNAME_MAX 256

#define acpi_os_allocate(x) kmalloc(x, GFP_KERNEL)
#define acpi_os_free(x) kfree(x)

#define TO_POINTER 0
#define TO_OFFSET 1

#ifdef DEBUG_PM
#define dbg_print(format, arg...) \
        printk("%s " format, __func__, ##arg)
#else
#define dbg_print(format, arg...)
#endif

static const struct acpi_device_id pm_devicd_ids[] = {
	{"IPML200", 0},
	{"", 0},
};

MODULE_DEVICE_TABLE(acpi, pm_devicd_ids);

typedef struct {
	struct acpi_buffer *in;
	struct acpi_buffer *out;
} priv_data_t;

static struct cdev cmpc_pm_acpi_cdev;
static struct class *cmpc_pm_acpi_class;
static int cmpc_pm_major;

/*
 * Could probably find this somewhere, but it didn't immediately work,
 * so I wrote my own.
 */
static char *
strdup(char *orig)
{
	char *new;

	new = kmalloc(strlen(orig), GFP_KERNEL);
	if (!new)
		return NULL;
	strcpy(new, orig);
	return new;
}

/*
 * Clean up the private data pointer
 */
static void
cmpc_pm_acpi_free_priv_in(struct file *f)
{
	priv_data_t *priv_data = f->private_data;
	struct acpi_buffer *buffer;

	if (priv_data->in) {
		buffer = priv_data->in;
		if (buffer->pointer)
			acpi_os_free(buffer->pointer);
		kfree(priv_data->in);
		priv_data->in = NULL;
	}
}

static void
cmpc_pm_acpi_free_priv_out(struct file *f)
{
	priv_data_t *priv_data = f->private_data;
	struct acpi_buffer *buffer;

	if (priv_data->out) {
		buffer = priv_data->out;
		if (buffer->pointer)
			acpi_os_free(buffer->pointer);
		kfree(priv_data->out);
		priv_data->out = NULL;
	}
}

/*
 * Try to handle paths from the filesystem, guess root from "ACPI"
 * directory, convert '/' to '.'.  Should I just let userpsace
 * take care of this?
 */
static char *
cmpc_pm_acpi_parse_path(char *orig_path)
{
	char *new_path, *tmp;

	tmp = strstr(orig_path, "ACPI");
	if (tmp)
		tmp += 5;
	else
		tmp = orig_path;

	new_path = strdup(tmp);

	while ((tmp = strchr(new_path, '/')) != NULL)
		*tmp = '.';

	return new_path;
}

/*
 * Given path, try to get an ACPI handle
 */
static acpi_handle
cmpc_pm_acpi_get_handle(char *path)
{
	char *new_path;
	acpi_handle handle;
	acpi_status status;

	if (!strlen(path))
		return ACPI_ROOT_OBJECT;

	new_path = cmpc_pm_acpi_parse_path(path);

	if (strlen(new_path) > ACPI_MAX_STRING) {
		kfree(new_path);
		return NULL;
	}
	
	status = acpi_get_handle(ACPI_ROOT_OBJECT, new_path, &handle);
	if(ACPI_FAILURE(status)){
		kfree(new_path);
		return NULL;
	}
	kfree(new_path);
	return handle;
}

static struct acpi_buffer *
cmpc_pm_acpi_get_next(acpi_handle handle)
{
	acpi_handle chandle = NULL;
	struct acpi_buffer *return_buf = NULL;
	struct acpi_buffer path_buf;
	char pathname[ACPI_PATHNAME_MAX];
	acpi_object_type type;
	char *new_buf;
	size_t new_size;

	path_buf.pointer = pathname;

	return_buf = kmalloc(sizeof(struct acpi_buffer), GFP_KERNEL);
	if (!return_buf)
		return NULL;

	/*
	 * Setup the string terminator, then just push it along
	 */
	return_buf->length = 1;
	return_buf->pointer = acpi_os_allocate(1);
	if (!return_buf->pointer)
		return NULL;
	memset(return_buf->pointer, 0, 1);

	while (ACPI_SUCCESS(acpi_get_next_object(ACPI_TYPE_ANY, handle,
	                                         chandle, &chandle))) {

		path_buf.length = sizeof(pathname);
		memset(pathname, 0, sizeof(pathname));

		if (ACPI_FAILURE(acpi_get_name(chandle, ACPI_SINGLE_NAME,
		                               &path_buf)))
			continue;

		if (ACPI_FAILURE(acpi_get_type(chandle, &type)))
			continue;

		/*
		 * Try to only provide defined objects and methods.  Note
		 * there's nothing preventing you from guessing.
		 */
		if (type != ACPI_TYPE_DEVICE &&
		    type != ACPI_TYPE_PROCESSOR &&
		    type != ACPI_TYPE_THERMAL &&
		    type != ACPI_TYPE_POWER &&
		    pathname[0] != '_') {

			continue;
		}

		/*
		 * length includes terminator, which we'll replace
		 * with a "\n".
		 */
		new_size = return_buf->length + path_buf.length;
		new_buf = acpi_os_allocate(new_size);

		if (!new_buf) {
			if (return_buf->pointer)
				acpi_os_free(return_buf->pointer);
			kfree(return_buf);
			return NULL;
		}

		memset(new_buf, 0, new_size);

		strcat(new_buf, return_buf->pointer);
		strcat(new_buf, pathname);
		strcat(new_buf, "\n");

		acpi_os_free(return_buf->pointer);

		return_buf->pointer = new_buf;
		return_buf->length = new_size;
	}

	if (return_buf->length == 1) {
		kfree(return_buf->pointer);
		kfree(return_buf);
		return NULL;
	}

	return return_buf;
}

static acpi_status
cmpc_pm_acpi_get_devices_callback(acpi_handle handle, u32 depth, void *context,
                              void **ret)
{
	char pathname[ACPI_PATHNAME_MAX] = {0};
	struct acpi_buffer buffer = {ACPI_PATHNAME_MAX, pathname};
	struct acpi_buffer *ret_buf = *ret;
	char *new_buf;
	size_t new_size;

	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);

	/*
	 * length includes terminator, which we'll replace
	 * with a "\n".
	 */
	new_size = buffer.length + ret_buf->length;
	new_buf = acpi_os_allocate(new_size);

	if (!new_buf)
		return AE_NO_MEMORY;

	memset(new_buf, 0, new_size);

	strcat(new_buf, ret_buf->pointer);
	strcat(new_buf, pathname);
	strcat(new_buf, "\n");

	acpi_os_free(ret_buf->pointer);

	ret_buf->pointer = new_buf;
	ret_buf->length = new_size;

	return AE_OK;
}

static struct acpi_buffer *
cmpc_pm_acpi_get_devices(char *hid)
{
	struct acpi_buffer *return_buf = NULL;

	return_buf = kmalloc(sizeof(struct acpi_buffer), GFP_KERNEL);
	if (!return_buf)
		return NULL;

	/*
	 * Setup the string terminator, then just push it along
	 */
	return_buf->length = 1;
	return_buf->pointer = kmalloc(1, GFP_KERNEL);
	if (!return_buf->pointer)
		return NULL;
	memset(return_buf->pointer, 0, 1);

	if (ACPI_FAILURE(acpi_get_devices(hid, cmpc_pm_acpi_get_devices_callback,
	                                  NULL, (void **)&return_buf))) {
		kfree(return_buf->pointer);
		kfree(return_buf);
		return NULL;
	}

	if (return_buf->length == 1) {
		kfree(return_buf->pointer);
		kfree(return_buf);
		return NULL;
	}

	return return_buf;
}

static int
cmpc_pm_acpi_check_addr(void *addr, void *start, void *end, int len)
{
	end = (unsigned char *)((unsigned long)end - len);
	if ((unsigned long)addr > (unsigned long)end)
		return 0;
	if ((unsigned long)addr < (unsigned long)start)
		return 0;
	
	return 1;
}

static void
cmpc_pm_acpi_fixup_string(union acpi_object *element, unsigned char *start,
                      unsigned char *end, int dir)
{
	char *tmp;
	char **strptr;

	strptr = &element->string.pointer;

	if (dir == TO_POINTER) {
		tmp = (char *)((unsigned long)*strptr +
		               (unsigned long)element->string.pointer);

		if (!cmpc_pm_acpi_check_addr(tmp, start, end,
		                         element->string.length)) {
			/* FIXME something bad */
			printk("%s:%s() string offset exceeds range\n",
			       CMPC_PM_ACPI_NAME, __FUNCTION__);
		}
		*strptr = tmp;
	} else {
		tmp = *strptr;

		if (!cmpc_pm_acpi_check_addr(tmp, start, end,
		                         element->string.length)) {
			/* FIXME something bad */
			printk("%s:%s() string pointer exceeds range\n",
			       CMPC_PM_ACPI_NAME, __FUNCTION__);
		}
		*strptr = (char *)((unsigned long)tmp - (unsigned long)strptr);
	}
}

static void
cmpc_pm_acpi_fixup_buffer(union acpi_object *element, unsigned char *start,
                      unsigned char *end, int dir)
{
	unsigned char *tmp;
	unsigned char **bufptr;

	bufptr = &element->buffer.pointer;

	if (dir == TO_POINTER) {
		tmp = (char *)((unsigned long)*bufptr +
		               (unsigned long)element->buffer.pointer);

		if (!cmpc_pm_acpi_check_addr(tmp, start, end,
		                         element->buffer.length)) {
			/* FIXME something bad */
			printk("%s:%s() buffer offset exceeds range\n",
			       CMPC_PM_ACPI_NAME, __FUNCTION__);
		}
		*bufptr = tmp;
	} else {
		tmp = *bufptr;

		if (!cmpc_pm_acpi_check_addr(tmp, start, end,
		                         element->buffer.length)) {
			/* FIXME something bad */
			printk("%s:%s() buffer pointer exceeds range\n",
			       CMPC_PM_ACPI_NAME, __FUNCTION__);
		}
		*bufptr = (char *)((unsigned long)tmp - (unsigned long)bufptr);
	}
}

static void cmpc_pm_acpi_fixup_package(union acpi_object *, unsigned char *,
                                   unsigned char *, int);

static void
cmpc_pm_acpi_fixup_element(union acpi_object *element, void *start,
                       void *end, int dir)
{
	switch (element->type) {
		case ACPI_TYPE_STRING:
			return cmpc_pm_acpi_fixup_string(element, start, end, dir);
		case ACPI_TYPE_BUFFER:
			return cmpc_pm_acpi_fixup_buffer(element, start, end, dir);
		case ACPI_TYPE_PACKAGE:
			return cmpc_pm_acpi_fixup_package(element, start, end, dir);
		default:
			/* No fixup necessary */
			return;
	}
}

static void
cmpc_pm_acpi_fixup_package(union acpi_object *element, unsigned char *start,
                       unsigned char *end, int dir)
{
	int count;
	union acpi_object **pkgptr;
	union acpi_object *tmp;

	pkgptr = &element->package.elements;

	if (dir == TO_POINTER) {
		tmp = (union acpi_object *)((unsigned long)*pkgptr +
		                     (unsigned long)element->package.elements);

		if (!cmpc_pm_acpi_check_addr(tmp, start, end,
		                         sizeof(union acpi_object))) {
			/* FIXME something bad */
			printk("%s:%s() pacakge offset exceeds range\n",
			       CMPC_PM_ACPI_NAME, __FUNCTION__);
		}
		*pkgptr = tmp;
	} else {
		tmp = *pkgptr;

		if (!cmpc_pm_acpi_check_addr(tmp, start, end,
		                         sizeof(union acpi_object))) {
			/* FIXME something bad */
			printk("%s:%s() package pointer exceeds range\n",
			       CMPC_PM_ACPI_NAME, __FUNCTION__);
		}
		*pkgptr = (union acpi_object *)((unsigned long)tmp -
		                                (unsigned long)pkgptr);
	}

	count = element->package.count;

	for ( ; count > 0 ; count--) {
		cmpc_pm_acpi_fixup_element(tmp, start, end, dir);
		tmp++;
	}
}

static int
cmpc_pm_acpi_fixup_arglist(struct acpi_object_list *arg_list, size_t len)
{
	int i;
	union acpi_object **cur_arg, *tmp, *end;

	/*
	 * Sanity check, make sure buffer is at least the minimum size
	 * for the claimed number of arguements.
	 */
	if (len < (sizeof(struct acpi_object_list) +
	           (arg_list->count * sizeof(union acpi_object))))
		return -EFAULT;

	end = (union acpi_object *)((unsigned long)arg_list + len);

	cur_arg = &arg_list->pointer;

	for (i = 0; i < arg_list->count ; i++) {
		tmp = (union acpi_object *)((unsigned long)&cur_arg[i] +
		                            (unsigned long)cur_arg[i]);

		/*
		 * Offset withing bounds of buffer?
		 */
		if (!cmpc_pm_acpi_check_addr(tmp, arg_list, end,
		                         sizeof(union acpi_object)))
			return -EFAULT;

		cur_arg[i] = tmp;

		cmpc_pm_acpi_fixup_element(tmp, arg_list, end, TO_POINTER);
	}

	return 0;
}

static ssize_t
cmpc_pm_acpi_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	struct acpi_buffer *buffer;
	unsigned long offset = *off;
	unsigned char *start;
	size_t copy_len;
	priv_data_t *priv_data = f->private_data;

	if (!priv_data->out)
		return -ENODEV;

	buffer = priv_data->out;

	if (!buffer->length || !buffer->pointer)
		return -ENODEV;

	if (offset > buffer->length)
		return -EFAULT;

	start = buffer->pointer + offset;

	copy_len = offset+len > buffer->length ? buffer->length - offset : len;

	if (copy_to_user(buf, start, copy_len))
		return -EFAULT;

	return copy_len;
}

static ssize_t
cmpc_pm_acpi_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	struct acpi_buffer *buffer;
	unsigned long offset = *off;
	unsigned char *start;
	priv_data_t *priv_data = f->private_data;

	if (!priv_data->in) {
		priv_data->in = kmalloc(sizeof(struct acpi_buffer), GFP_KERNEL);
		if (!priv_data->in)
			return -ENOMEM;

		buffer = priv_data->in;
		buffer->length = 0;
		buffer->pointer = NULL;
	}

	buffer = priv_data->in;

	if (len + offset > buffer->length) {
		void *new_buf;

		new_buf = acpi_os_allocate(len + offset);

		if (!new_buf)
			return -ENOMEM;

		memset(new_buf, 0, len + offset);

		if (buffer->length && buffer->pointer)
			memcpy(new_buf, buffer->pointer, buffer->length);

		acpi_os_free(buffer->pointer);

		buffer->pointer = new_buf;
		buffer->length = len + offset;
	}

	start = buffer->pointer + offset;

	if (copy_from_user(start, buf, len))
		return -EFAULT;

	return len;
}

static int
cmpc_pm_acpi_open(struct inode *i, struct file *f)
{
	f->private_data = kmalloc(sizeof(priv_data_t), GFP_KERNEL);
	if (!f->private_data)
		return -ENOMEM;

	memset(f->private_data, 0, sizeof(priv_data_t));
	return 0;
}


static int
cmpc_pm_acpi_release(struct inode *i, struct file *f)
{
	cmpc_pm_acpi_free_priv_in(f);
	cmpc_pm_acpi_free_priv_out(f);
	kfree(f->private_data);
	return 0;
}

static int
cmpc_pm_acpi_read_integer(struct file *f,
               acpi_integer param_value, unsigned long arg)
{
	acpi_handle handle;
	cmpc_pm_acpi_t data;
	acpi_status status;

	union acpi_object param;
	struct acpi_object_list input;
	unsigned long long output;

	cmpc_pm_acpi_free_priv_in(f);
	cmpc_pm_acpi_free_priv_out(f);

	if (copy_from_user(&data, (cmpc_pm_acpi_t *)arg, sizeof(data)))
		return -EFAULT;		
	handle = cmpc_pm_acpi_get_handle(data.pathname);

	if (!handle)
		return -ENOENT;

	param.type = ACPI_TYPE_INTEGER;
	param.integer.value = param_value;
	input.count = 1;
	input.pointer = &param;

	if (ACPI_FAILURE(status = acpi_evaluate_integer(handle, "GRDI", &input, &output)))
	{
		return -EFAULT;
	}

	data.status = output;

	if (copy_to_user((cmpc_pm_acpi_t *)arg, &data, sizeof(data)))
		return -EFAULT;
	return 0;
}

static int
cmpc_pm_acpi_write_integer(struct file *f,
               acpi_integer param_value, unsigned long arg)
{
	acpi_handle handle;
	cmpc_pm_acpi_t data;
	acpi_status status;

	union acpi_object args[2]; 
	struct acpi_object_list params = { .pointer = args, .count = 2 };
	unsigned long long output;

	cmpc_pm_acpi_free_priv_in(f);
	cmpc_pm_acpi_free_priv_out(f);

	if (copy_from_user(&data, (cmpc_pm_acpi_t *)arg, sizeof(data)))
		return -EFAULT;		
	handle = cmpc_pm_acpi_get_handle(data.pathname);

	if (!handle)
		return -ENOENT;

	args[0].type = ACPI_TYPE_INTEGER;
	args[0].integer.value = param_value;
	args[1].type = ACPI_TYPE_INTEGER;
	args[1].integer.value = data.inputparam;
	

	if (ACPI_FAILURE(status = acpi_evaluate_integer(handle, "GWRI", &params, &output)))
	{
		return -EFAULT;
	}

	if (copy_to_user((cmpc_pm_acpi_t *)arg, &data, sizeof(data)))
		return -EFAULT;
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
static long cmpc_pm_acpi_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
#else
static int cmpc_pm_acpi_ioctl(struct inode *inode, struct file *f, unsigned int cmd, unsigned long arg)
#endif
{
	/* Do stuff... */
	if (cmd == CMPC_PM_ACPI_EXISTS) {
		acpi_handle handle;
		cmpc_pm_acpi_t data;

		cmpc_pm_acpi_free_priv_in(f);
		cmpc_pm_acpi_free_priv_out(f);

		if (copy_from_user(&data, (cmpc_pm_acpi_t *)arg, sizeof(data)))
			return -EFAULT;

		handle = cmpc_pm_acpi_get_handle(data.pathname);

		if (!handle)
			return -ENOENT;

	} else if (cmd == CMPC_PM_ACPI_GET_TYPE) {
		acpi_handle handle;
		cmpc_pm_acpi_t data;
		acpi_object_type type;

		cmpc_pm_acpi_free_priv_in(f);
		cmpc_pm_acpi_free_priv_out(f);

		if (copy_from_user(&data, (cmpc_pm_acpi_t *)arg, sizeof(data)))
			return -EFAULT;

		handle = cmpc_pm_acpi_get_handle(data.pathname);
		if (!handle)
			return -ENOENT;

		if (ACPI_FAILURE(acpi_get_type(handle, &type)))
			return -EFAULT;

		data.status = type;

		if (copy_to_user((cmpc_pm_acpi_t *)arg, &data, sizeof(data)))
			return -EFAULT;		

	} else if (cmd == CMPC_PM_ACPI_EVALUATE_OBJ) {

		struct acpi_buffer *buffer;
		acpi_handle handle;
		cmpc_pm_acpi_t data;
		priv_data_t *priv_data = f->private_data;
		struct acpi_object_list *arg_list = NULL;

		cmpc_pm_acpi_free_priv_out(f);

		if (copy_from_user(&data, (cmpc_pm_acpi_t *)arg, sizeof(data)))
			return -EFAULT;

		handle = cmpc_pm_acpi_get_handle(data.pathname);

		if (!handle)
			return -ENOENT;

		buffer = kmalloc(sizeof(struct acpi_buffer), GFP_KERNEL);
		
		if (!buffer)
			return -ENOMEM;

		buffer->length = ACPI_ALLOCATE_BUFFER;
		buffer->pointer = NULL;

		if (priv_data->in) {
			struct acpi_buffer *tmp_buf;

			tmp_buf = priv_data->in;

			if (tmp_buf->length && tmp_buf->pointer) {
				arg_list = kmalloc(tmp_buf->length, GFP_KERNEL);
				if (!arg_list) {
					kfree(buffer);
					return -ENOMEM;
				}
				memcpy(arg_list, tmp_buf->pointer,
				       tmp_buf->length);

				if (cmpc_pm_acpi_fixup_arglist(arg_list,
				                           tmp_buf->length)) {
					kfree(buffer);
					kfree(arg_list);
					return -EFAULT;
				}
			}
			cmpc_pm_acpi_free_priv_in(f);
		}

		if (ACPI_FAILURE(acpi_evaluate_object(handle, NULL,
		                                      arg_list, buffer))) {
			kfree(arg_list);
			kfree(buffer);
			return -ENOENT;
		}
		kfree(arg_list);
		cmpc_pm_acpi_fixup_element(buffer->pointer, buffer->pointer,
		                       buffer->pointer + buffer->length,
				       TO_OFFSET);

		data.status = buffer->length;
		if (copy_to_user((cmpc_pm_acpi_t *)arg, &data, sizeof(data))) {
			acpi_os_free(buffer->pointer);
			kfree(buffer);
			return -EFAULT;
		}

		priv_data->out = buffer;

	} else if (cmd == CMPC_PM_ACPI_GET_NEXT) {
		acpi_handle handle;
		cmpc_pm_acpi_t data;
		priv_data_t *priv_data = f->private_data;

		cmpc_pm_acpi_free_priv_in(f);
		cmpc_pm_acpi_free_priv_out(f);

		if (copy_from_user(&data, (cmpc_pm_acpi_t *)arg, sizeof(data)))
			return -EFAULT;

		handle = cmpc_pm_acpi_get_handle(data.pathname);

		if (!handle)
			return -ENOENT;

		priv_data->out = cmpc_pm_acpi_get_next(handle);

		if (!priv_data->out)
			return -EFAULT;

		data.status = priv_data->out->length;

		if (copy_to_user((cmpc_pm_acpi_t *)arg, &data, sizeof(data))) {
			struct acpi_buffer *buffer = priv_data->out;

			priv_data->out = NULL;
			acpi_os_free(buffer->pointer);
			kfree(buffer);
			return -EFAULT;
		}

	} else if (cmd == CMPC_PM_ACPI_CLEAR) {
		cmpc_pm_acpi_free_priv_in(f);
		cmpc_pm_acpi_free_priv_out(f);
	} else if (cmd == CMPC_PM_ACPI_GET_DEVICES) {
		cmpc_pm_acpi_t data;
		priv_data_t *priv_data = f->private_data;

		cmpc_pm_acpi_free_priv_in(f);
		cmpc_pm_acpi_free_priv_out(f);

		if (copy_from_user(&data, (cmpc_pm_acpi_t *)arg, sizeof(data)))
			return -EFAULT;

		if (strlen(data.pathname) != 7 /* FIXME #def for this? */)
			return -EINVAL;

		priv_data->out = cmpc_pm_acpi_get_devices(data.pathname);

		if (!priv_data->out)
			return -EFAULT;

		data.status = priv_data->out->length;

		if (copy_to_user((cmpc_pm_acpi_t *)arg, &data, sizeof(data))) {
			struct acpi_buffer *buffer = priv_data->out;

			priv_data->out = NULL;
			acpi_os_free(buffer->pointer);
			kfree(buffer);
			return -EFAULT;
		}

	} else if (cmd == CMPC_PM_ACPI_GET_OBJ_INFO) {
		struct acpi_buffer *buffer;
		acpi_handle handle;
		cmpc_pm_acpi_t data;
		priv_data_t *priv_data = f->private_data;

		cmpc_pm_acpi_free_priv_in(f);
		cmpc_pm_acpi_free_priv_out(f);

		if (copy_from_user(&data, (cmpc_pm_acpi_t *)arg, sizeof(data)))
			return -EFAULT;

		handle = cmpc_pm_acpi_get_handle(data.pathname);

		if (!handle)
			return -ENOENT;

		buffer = kmalloc(sizeof(struct acpi_buffer), GFP_KERNEL);
		
		if (!buffer)
			return -ENOMEM;

		buffer->length = ACPI_ALLOCATE_BUFFER;
		buffer->pointer = NULL;

		if (ACPI_FAILURE(acpi_get_object_info(handle, (struct acpi_device_info **)&buffer))) {
			kfree(buffer);
			return -ENOENT;
		}

		data.status = buffer->length;

		if (copy_to_user((cmpc_pm_acpi_t *)arg, &data, sizeof(data))) {
			acpi_os_free(buffer->pointer);
			kfree(buffer);
			return -EFAULT;
		}

		priv_data->out = buffer;

	} else if (cmd == CMPC_PM_ACPI_GET_PARENT) {
		char pathname[ACPI_PATHNAME_MAX] = {0};
		struct acpi_buffer tmpbuf = {ACPI_PATHNAME_MAX, pathname};
		struct acpi_buffer *buffer;
		acpi_handle handle, phandle;
		cmpc_pm_acpi_t data;
		priv_data_t *priv_data = f->private_data;

		cmpc_pm_acpi_free_priv_in(f);
		cmpc_pm_acpi_free_priv_out(f);

		if (copy_from_user(&data, (cmpc_pm_acpi_t *)arg, sizeof(data)))
			return -EFAULT;

		handle = cmpc_pm_acpi_get_handle(data.pathname);

		if (!handle)
			return -ENOENT;

		if (ACPI_FAILURE(acpi_get_parent(handle, &phandle)))
			return -EFAULT;

		acpi_get_name(phandle, ACPI_FULL_PATHNAME, &tmpbuf);

		buffer = kmalloc(sizeof(struct acpi_buffer), GFP_KERNEL);

		if (!buffer)
			return -ENOMEM;

		buffer->length = tmpbuf.length;
		buffer->pointer = acpi_os_allocate(buffer->length);

		if (!buffer->pointer) {
			kfree(buffer);
			return -ENOMEM;
		}

		memcpy(buffer->pointer, tmpbuf.pointer, buffer->length);
		data.status = buffer->length;

		if (copy_to_user((cmpc_pm_acpi_t *)arg, &data, sizeof(data))) {
			acpi_os_free(buffer->pointer);
			kfree(buffer);
			return -EFAULT;
		}

		priv_data->out = buffer;

	} else if( cmd == CMPC_PM_ACPI_GET_BRIGHTNESS ){
		int ret = cmpc_pm_acpi_read_integer(f, 0xC0, arg);
		return ret;
	} else if( cmd == CMPC_PM_ACPI_GET_WIRELESS ){
		return cmpc_pm_acpi_read_integer(f, 0xC1, arg);
	} else if( cmd == CMPC_PM_ACPI_GET_LAN ){
		return cmpc_pm_acpi_read_integer(f, 0xC2, arg);
	} else if( cmd == CMPC_PM_ACPI_GET_CARDREADER ){
		return cmpc_pm_acpi_read_integer(f, 0xC3, arg);
	}
	else if( cmd == CMPC_PM_ACPI_SET_BRIGHTNESS ){
		int ret = cmpc_pm_acpi_write_integer(f, 0xC0, arg);
		return ret;
	}	
	else if( cmd == CMPC_PM_ACPI_SET_WIRELESS ){
		return cmpc_pm_acpi_write_integer(f, 0xC1, arg);
	}	
	else if( cmd == CMPC_PM_ACPI_SET_LAN ){
		return cmpc_pm_acpi_write_integer(f, 0xC2, arg);
	}	
	else if( cmd == CMPC_PM_ACPI_SET_CARDREADER ){
		return cmpc_pm_acpi_write_integer(f, 0xC3, arg);
	}
	else	
		return -EINVAL;

	return 0;
}


static struct file_operations cmpc_pm_acpi_fops = {
	.read = cmpc_pm_acpi_read,
	.write = cmpc_pm_acpi_write,
	.open = cmpc_pm_acpi_open,
	.release = cmpc_pm_acpi_release,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	.unlocked_ioctl = cmpc_pm_acpi_ioctl,
#else
	.ioctl = cmpc_pm_acpi_ioctl,
#endif
	.owner = THIS_MODULE,
};


static int __init cmpc_pm_acpi_init(void)
{
	int rc;
	dev_t devno;
	struct device *dev;

	rc = alloc_chrdev_region(&devno, 0, 1, CMPC_PM_ACPI_NAME);
	cmpc_pm_major = MAJOR(devno);
	if (rc) {
		printk(KERN_ERR "cmpc_pm: failed to register chrdev region.\n");
		goto out;
	}

    /* register the 'cmpc_pc_acpi_dev' char device */
	cdev_init(&cmpc_pm_acpi_cdev, &cmpc_pm_acpi_fops);
    cmpc_pm_acpi_cdev.owner = THIS_MODULE;
	cmpc_pm_acpi_cdev.ops   = &cmpc_pm_acpi_fops;

    rc = cdev_add(&cmpc_pm_acpi_cdev, devno, 1);
    if (rc) {
        printk(KERN_ERR "cmpc_pm: cdev_add() failed.\n");
		goto err_alloc;
	}

    /* creating your own class */
    cmpc_pm_acpi_class = class_create(THIS_MODULE, "cmpc_pm_acpi_class");
    if(IS_ERR(cmpc_pm_acpi_class)) {
        printk(KERN_ERR "cmpc_pm: failed in creating class.\n");
        goto err_cdevadd;
    }

	dev = device_create(cmpc_pm_acpi_class, NULL, devno, NULL, CMPC_PM_ACPI_NAME);
   	if (!dev) {
		printk(KERN_ERR "cmpc_pm: device_create faild.\n");
		goto err_clscreate;
	}
 
	return 0;

err_clscreate:
	class_destroy(cmpc_pm_acpi_class);

err_cdevadd:
	cdev_del(&cmpc_pm_acpi_cdev);

err_alloc:
	unregister_chrdev_region(devno, 1);

out:
    return rc;
}


static void __exit cmpc_pm_acpi_exit(void)
{
	device_destroy(cmpc_pm_acpi_class, MKDEV(cmpc_pm_major, 0));
	
	if (cmpc_pm_acpi_class) {
	    class_destroy(cmpc_pm_acpi_class);
	}
    cdev_del(&cmpc_pm_acpi_cdev);

	if (cmpc_pm_major) {
		unregister_chrdev_region(MKDEV(cmpc_pm_major, 0), 1);
	}
}

module_init(cmpc_pm_acpi_init);
module_exit(cmpc_pm_acpi_exit);
MODULE_AUTHOR("rhett");
MODULE_DESCRIPTION("Device file access to ACPI namespace");
MODULE_VERSION("1.0.0.1");
MODULE_LICENSE("GPL");
