#include <stdio.h>
#include <stdlib.h>
#include <libusb-1.0/libusb.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>


int* handleBits(unsigned char* data, int size) {
	int* usb = (int*)calloc(size, sizeof(int));
	int i = size;

	for (unsigned char c = *data; c; c >>= 1)
		usb[--i] = c & 1;

	return usb;
}

void emit(int fd, int type, int code, int val) {
   	struct input_event ie;

   	ie.type = type;
   	ie.code = code;
   	ie.value = val;

   	// timestamp values below are ignored
   	ie.time.tv_sec = 0;
   	ie.time.tv_usec = 0;

   	write(fd, &ie, sizeof(ie));
}

int joystickAxisVal(int type, int firstBit, int secondBit) {
	if ((firstBit == 0) && (secondBit == 0))
		return -1;
	else if ((firstBit == 0) && (secondBit == 1)) {
		// x axis
		if (type == 0)
			return -32767;

		// y axis
		return 32767;
    }
	else if ((firstBit == 1) && (secondBit == 0))
		return 0;
	else if ((firstBit == 1) && (secondBit == 1)) {
		if (type == 0)
			return 32767;

		return -32767;
	}
}

int main() {
	libusb_device **devs;// pointer to pointer of device, used to retrieve a list of devices
	libusb_device_handle *dev_handle;// a device handle
	libusb_context *ctx = NULL;// a libusb session
	int r;// for return values
	ssize_t cnt;// holding number of devices in list

	// initialize a library session
	r = libusb_init(&ctx);

	if (r < 0) {
		printf("Init error!\n");
		return 1;
	}

	// set verbosity level to 3, as suggested in the documentation
	libusb_set_debug(ctx, 3);
	cnt = libusb_get_device_list(ctx, &devs);

	if (cnt < 0) {
		printf("Get device error!\n");
		return 1;
	}

	// these are vendorID and productID for chompapp
	dev_handle = libusb_open_device_with_vid_pid(ctx, 39546, 47639);

	if (dev_handle == NULL)
	    printf("Cannot open device\n");

    libusb_free_device_list(devs, 1);

	// find out if kernel driver is attached
	if (libusb_kernel_driver_active(dev_handle, 0) == 1) {
        	printf("Kernel Driver Active\n");

        if (libusb_detach_kernel_driver(dev_handle, 0) == 0)
         	printf("Kernel Driver Detached!\n");
    }

	// claim interface 0 (the first) of device
    r = libusb_claim_interface(dev_handle, 0);

	if(r < 0) {
		printf("Cannot Claim Interface\n");
		return 1;
	}

	unsigned char *data = (unsigned char*)malloc(1 * sizeof(unsigned char));// array that holds the data sent by chompapp
	int actual;// used to find out how many bytes were written
	int *usb_data;// array that stores the chompapp data after bit shifting

	struct uinput_setup usetup;
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

	if (fd < 0)
		printf("open failed\n");

	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_KEYBIT, BTN_JOYSTICK);
	ioctl(fd, UI_SET_EVBIT, EV_ABS);
	ioctl(fd, UI_SET_ABSBIT, ABS_X);
	ioctl(fd, UI_SET_ABSBIT, ABS_Y);

	memset(&usetup, 0, sizeof(usetup));
	usetup.id.bustype = BUS_USB;
	usetup.id.vendor = 0x1234;
	usetup.id.product = 0x5678;
	strcpy(usetup.name, "My device");

	ioctl(fd, UI_DEV_SETUP, &usetup);
	ioctl(fd, UI_DEV_CREATE);

	while(1) {
		//disconnect from chompapp
		if (libusb_bulk_transfer(dev_handle, (1 | LIBUSB_ENDPOINT_IN), data, 1, &actual, 0) < 0)
			break;

		usb_data = handleBits(data, 8);

		emit(fd, EV_KEY, BTN_JOYSTICK, usb_data[3]);
		emit(fd, EV_SYN, SYN_REPORT, 0);//synchronize
		emit(fd, EV_ABS, ABS_X, joystickAxisVal(0, usb_data[4], usb_data[5]));
		emit(fd, EV_SYN, SYN_REPORT, 0);
		emit(fd, EV_ABS, ABS_Y, joystickAxisVal(1, usb_data[6], usb_data[7]));
		emit(fd, EV_SYN, SYN_REPORT, 0);

		free(usb_data);
		usb_data = NULL;
	}

	free(data);
	data = NULL;
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);

	r = libusb_release_interface(dev_handle, 0);

	if (r != 0)
        printf("Program ended.\n");

	libusb_close(dev_handle);
	libusb_exit(ctx);

	return 0;
}