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

typedef struct {
	unsigned int		version;
	unsigned long long	status;
	char				pathname[ACPI_MAX_STRING];
	unsigned int		inputparam;
} cmpc_pm_acpi_t;

#define CMPC_PM_ACPI_MAGIC 'A'

/* Clear all state associated w/ device
 *  input - none
 *  output - none
 */
#define CMPC_PM_ACPI_CLEAR			_IOW(CMPC_PM_ACPI_MAGIC, 0, cmpc_pm_acpi_t)

/* Return success if pathname exists
 *  input - pathname
 *  output - none
 */
#define CMPC_PM_ACPI_EXISTS			_IOW(CMPC_PM_ACPI_MAGIC, 1, cmpc_pm_acpi_t)

/* Return type of object
 *  input - pathname
 *  output - status = type
 */
#define CMPC_PM_ACPI_GET_TYPE		_IOWR(CMPC_PM_ACPI_MAGIC, 2, cmpc_pm_acpi_t)

/* Evaluate an object
 *  input - pathname, write buffer = arg list
 *  output - status = length of read buffer
 *           read buffer = eval data
 */
#define CMPC_PM_ACPI_EVALUATE_OBJ		_IOWR(CMPC_PM_ACPI_MAGIC, 3, cmpc_pm_acpi_t)

/* Get Next objects
 *  input - pathname
 *  output - status = length of read buffer
 *           read buffer = list of child objects
 */
#define CMPC_PM_ACPI_GET_NEXT		_IOWR(CMPC_PM_ACPI_MAGIC, 4, cmpc_pm_acpi_t)

/* Get Next objects
 *  input - pathname = PNP _HID/_CID value to look for
 *  output - status = length of read buffer
 *           read buffer = list of matching devices
 */
#define CMPC_PM_ACPI_GET_DEVICES		_IOWR(CMPC_PM_ACPI_MAGIC, 5, cmpc_pm_acpi_t)

/* Get object info structure
 *  input - pathname
 *  output - status = length of read buffer
 *           read buffer = object info structure
 */
#define CMPC_PM_ACPI_GET_OBJ_INFO		_IOWR(CMPC_PM_ACPI_MAGIC, 6, cmpc_pm_acpi_t)

/* Get parent object
 *  input - pathname
 *  output - status = length of read buffer
 *           read buffer = path of parent object
 */
#define CMPC_PM_ACPI_GET_PARENT		_IOWR(CMPC_PM_ACPI_MAGIC, 7, cmpc_pm_acpi_t)

/* Get brigtness of LCD
 *  input - pathname
 *  output - status = length of read buffer
 *           read buffer = path of parent object
 */
#define CMPC_PM_ACPI_GET_BRIGHTNESS		_IOWR(CMPC_PM_ACPI_MAGIC, 8, cmpc_pm_acpi_t)

/* Get status of Wireless
 *  input - pathname
 *  output - status = length of read buffer
 *           read buffer = path of parent object
 */
#define CMPC_PM_ACPI_GET_WIRELESS 	_IOWR(CMPC_PM_ACPI_MAGIC, 9, cmpc_pm_acpi_t)

/* Get status of LAN Device
 *  input - pathname
 *  output - status = length of read buffer
 *           read buffer = path of parent object
 */
#define CMPC_PM_ACPI_GET_LAN 	_IOWR(CMPC_PM_ACPI_MAGIC, 10, cmpc_pm_acpi_t)

/* Get status of Card reader
 *  input - pathname
 *  output - status = length of read buffer
 *           read buffer = path of parent object
 */
#define CMPC_PM_ACPI_GET_CARDREADER 	_IOWR(CMPC_PM_ACPI_MAGIC, 11, cmpc_pm_acpi_t)

/* Set brigtness of LCD
 *  input - pathname
            inputparam = value to set
 */
#define CMPC_PM_ACPI_SET_BRIGHTNESS		_IOWR(CMPC_PM_ACPI_MAGIC, 12, cmpc_pm_acpi_t)

/* Enable/disable the wireless device
 *  input - pathname
            inputparam = value to set
 */
#define CMPC_PM_ACPI_SET_WIRELESS		_IOWR(CMPC_PM_ACPI_MAGIC, 13, cmpc_pm_acpi_t)

/* Enable/disable the LAN Device
 *  input - pathname
            inputparam = value to set
 */
#define CMPC_PM_ACPI_SET_LAN		_IOWR(CMPC_PM_ACPI_MAGIC, 14, cmpc_pm_acpi_t)

/* Enable/disable the card reader
 *  input - pathname
            inputparam = value to set
 */
#define CMPC_PM_ACPI_SET_CARDREADER		_IOWR(CMPC_PM_ACPI_MAGIC, 15, cmpc_pm_acpi_t)
