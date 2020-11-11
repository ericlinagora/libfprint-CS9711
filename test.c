#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb-1.0/libusb.h>

#define VENDOR_ID   0x2541  // Replace XXXX with your device's vendor ID
#define PRODUCT_ID  0x0236  // Replace XXXX with your device's product ID
#define ENDPOINT_IN 0x81    // Endpoint address for IN direction (device to host)
#define ENDPOINT_OUT 0x01   // Endpoint address for OUT direction (host to device)
#define DATA_SIZE   8      // Size of data buffer
void printHex(char *prefix, const unsigned char *data, const int length) {
    fprintf(stderr, "%s (%d): { ", prefix, length);
    for (int i = 0; i < length; i++) {
        if (i) fprintf(stderr, ", ");
        fprintf(stderr, "0x%02X", data[i]);
    }
    fprintf(stderr, " }\n");
}
int send(libusb_device_handle *handle, unsigned char *data, const int length) {
    int transferred;
    int ret = libusb_bulk_transfer(handle, ENDPOINT_OUT, data, length, &transferred, 1000);
    if (ret == 0) {
        printf("Sent %d bytes (expected %d) to device\n", transferred, length);
    } else
        fprintf(stderr, "Failed to send data to device: %s\n", libusb_error_name(ret));
    return ret;
}
unsigned char *mallocReceiveSync(libusb_device_handle *handle, const int length, int *actualLength, const unsigned int timeout) {
    *actualLength = length;
    unsigned char *data = malloc(length);
    int ret = libusb_bulk_transfer(handle, ENDPOINT_IN, data, length, actualLength, timeout);
    if (ret == 0) {
        printHex("Received IN", data, *actualLength);
        return data;
    } else {
        free(data);
        fprintf(stderr, "Failed to receive data from device: %s\n", libusb_error_name(ret));
        return NULL;
    }
}
int receiveExpected(libusb_device_handle *handle, const unsigned char *expected, const int length, const unsigned int timeout) {
    int actual = length;
    // fprintf(stderr, "Reading length %d\n", length);
    unsigned char *data = mallocReceiveSync(handle, length, &actual, timeout);
    if (!data) return 1;
    if (actual != length)
        goto receiveExpected_failed;
    int result = memcmp(data, expected, actual);
    if (result)
        goto receiveExpected_failed;
    free(data);
    return 0;
receiveExpected_failed:
    fprintf(stderr, "Mismatched data\n");
    printHex("  Expected", expected, length);
    printHex("  Received", data, actual);
    free(data);
    return 1;
}
int main() {
    libusb_context *context = NULL;
    libusb_device_handle *handle = NULL;
    int transferred;
    unsigned char data[DATA_SIZE];
    int ret;

    ret = libusb_init_context(&context, /*options=*/NULL, /*num_options=*/0);
    if (ret < 0) {
        fprintf(stderr, "Failed to initialize libusb\n");
        return 1;
    }
    handle = libusb_open_device_with_vid_pid(context, VENDOR_ID, PRODUCT_ID);
    if (!handle) {
        fprintf(stderr, "Failed to open device\n");
        goto cleanup;
    }
    ret = libusb_claim_interface(handle, 0);
    if (ret < 0) {
        fprintf(stderr, "Failed to claim interface\n");
        goto cleanup;
    }

    unsigned char cmd_init_out[] = "\xea\x01\x00\x00\x00\x00\x01\xea";
    if (send(handle, cmd_init_out, sizeof(cmd_init_out) - 1))
        goto cleanup;
    unsigned char cmd_init_out_expected_response[] = { 0xEA, 0x01, 0x62, 0xA0, 0x00, 0x00, 0xC3, 0xEA };
    if (receiveExpected(handle, cmd_init_out_expected_response, sizeof(cmd_init_out_expected_response), 1000))
        goto cleanup;

    unsigned char cmd_start_scan_init_out[] = "\xea\x04\x00\x00\x00\x00\x04\xea";
    if (send(handle, cmd_start_scan_init_out, sizeof(cmd_start_scan_init_out) - 1))
        goto cleanup;
    if (mallocReceiveSync(handle, 8000, &transferred, 10000))
        goto cleanup;
cleanup:
    if (handle) {
        libusb_release_interface(handle, 0);
        libusb_close(handle);
    }
    if (context)
        libusb_exit(context);

    return ret < 0 ? 1 : 0;
}
