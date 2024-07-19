/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>

static void start_scan(void);

static struct bt_conn *default_conn;
//----------------my design for connecting to multiple devices is explained after the end of main with these funcs
static void multi_connect();
static bool my_devices_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad);

char address_list[3][BT_ADDR_LE_STR_LEN]; //addresses of connected devices for multi-device connection. we'll allow 3 devices
size_t address_list_iter; 	///not the prettiest way, but much of the func defs could rewritten more cleanly for multi connect


//-----------------my callback for parsing the name
static bool my_parse_cb(struct bt_data *data, void *user_data)
{
	int err;
	char bt_dname[] = "DXC";
	bt_addr_le_t *addr = user_data;
	//to my understanding, bt_data_parse() will continue parsing through each member of "struct net_buf_simple ad" as this CB returns true
	//thus, we can detect when type BT_DATA_NAME_COMPLETE is currently being parsed, check for correct name via strncmp, and connect only to DXC
	if(data->type == BT_DATA_NAME_COMPLETE)
	{
		//if the device name is DXC, do stuff. I am assuming data->data is ascii values, in which case this should work.
		//since nRF52 is ARM based, char is unsigned. unsigned chars are equivalent to ascii uint8_t values
		if(strncmp(bt_dname, data->data, data->data_len)==0)
		{
			//then connect to it. first, stop scanning. stop parsing if scan stop fails - assuming this would help prevent a system crash
			if (bt_le_scan_stop()) {
				return false;
			}
			//if scan stopped correctly, attempt to connect
			err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
				BT_LE_CONN_PARAM_DEFAULT, &default_conn);
			if (err) {
				printk("Create conn to DXC failed (%d)\n", err);
				start_scan();
			}
		//if name is NOT DXC, continue parsing	
		}else return true;
	//if data type is NOT BT_DATA_NAME_COMPLETE, continue parsing
	}else return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];

	if (default_conn) {
		return;
	}

	/* We're only interested in connectable events */
	if (type != BT_GAP_ADV_TYPE_ADV_IND &&
	    type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	printk("Device found: %s (RSSI %d)\n", addr_str, rssi);
	/* connect only to devices in close proximity */
	if (rssi < -50) {
		return;
	}

	//---------------my edits
	bt_data_parse(ad,my_parse_cb, (void *) addr);


}

static void start_scan(void)
{
	int err;

	/* This demo doesn't require active scan */
	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);
	if (err) {
		printk("Scanning failed to start (err %d)\n", err);
		return;
	}

	printk("Scanning successfully started\n");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		printk("Failed to connect to %s (%u)\n", addr, err);

		bt_conn_unref(default_conn);
		default_conn = NULL;

		start_scan();
		return;
	}

	if (conn != default_conn) {
		return;
	}

	printk("Connected: %s\n", addr);

	bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (conn != default_conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	bt_conn_unref(default_conn);
	default_conn = NULL;

	start_scan();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

int main(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	start_scan();
	//---------basically it's going to keep calling bt_le_scan_start() until an empty (nonexistent) device is found
	multi_connect();
	return 0;
}

/*
I am unsure of the intricacies of bt_le_scan_start, but the idea is this: 
"my_devices_found" is just a bool version of "device_found"
if no new device is found, "devices_found" will return false, and the program will stop attempting to search/connect to new devices
*/
void multi_connect()
{
	int err;
	bool new_device;

	//--------note the assignment new_device=devices_found
	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, new_device=my_devices_found);
	if (err) {
		printk("Scanning failed to start (err %d)\n", err);
		return;
	}
	printk("Scanning successfully started\n");

	//--------my edits
	if(new_device==true)
	{
		//since this is single-thread functional programming, we can only get to this recursive call after the previous device has been connected
		multi_connect();
	}else
	{
		printk("all compatible devices have been added");
	}

}

//exact same as device_found, but returns a bool true so long as a new device is advertising
static bool my_devices_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];

	//my assumption is that the buffer will be empty if no new devices are found. this may not be the case, but basically
	//the idea is to break the recursion upon finding no new devices.
	if(ad->__buf==NULL)
	{
		return false;
	}

	//----------this currently stops a connection attempt if a device is already defined. so we'll delete it
	// if (default_conn) {
	// 	return;	
	// }

	//by setting default_conn to NULL, we can overwrite the parameters of previous device with info from new device and connect to it
	default_conn = NULL;

	/* We're only interested in connectable events */
	if (type != BT_GAP_ADV_TYPE_ADV_IND &&
	    type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {

		/*-------------
		i am making it return true so the program will continue to search for compatible devices if it first detects an incompatible device
		i am aware this will make the program indefinitely continue so long as there are incompatible devices advertising,
		but for the scope of the assignment, that edge case shouldn't negatively affect the desired results
		*/
		return true;
	}

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	printk("Device found: %s (RSSI %d)\n", addr_str, rssi);
	/* connect only to devices in close proximity */
	if (rssi < -50) {
		return true;	//same reasoning as connectable events
	}

	//let's add the new device's address to our array so we can target it later for comm/disconnect.
	// if we already have 3 connected devices, we're maxed out and will not add another
	//this clashes with the parsing, explained below
	if(address_list_iter>=3)
	{
		return false;
	}
	address_list[address_list_iter][0] = *addr_str;
	address_list_iter++;
	/*
	obviously parsing by name doesnt make sense as it's currently written. 
	depending on the desired outcomes, we could modify bt_data_parse to check against a list of names, or we could
	exclude it and simply run "bt_conn_le_create()". if we choose to parse by name, we'll need to move the address
	list code directly above into the parsing func
	*/
	bt_data_parse(ad,my_parse_cb, (void *) addr);
	
	/*
	the new device has been connected. now recursively search for another advertising device.
	since the program currently disconnects from a device as soon as it finishes the handshake, that device may start advertising
	again, making this loop indefinitely.
	we would simply remove function "bt_conn_disconnect" from function "connected" and place it elsewhere with a better-paramaterized call.
	we would also have to edit the various references to default_conn, instead reference the desired address in address_list
	*/
	return true;

}