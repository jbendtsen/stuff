// Compile with:
// gcc sc64_ft_prog.c -lusb-1.0 -lftdi1 -o sc64_ft_prog

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libftdi1/ftdi.h>
#include <libusb-1.0/libusb.h>

#define MAYBE_BAIL(func, args...) \
    if ((res = func(args)) != 0) { \
        fprintf(stderr, "Error: " #func " returned %d (%s)\n", res, ftdi->error_str); \
        goto fail_after_open; \
    }

uint16_t get_eeprom_sum(uint16_t *data, int eeprom_size) {
    int sum_location = (eeprom_size / 2) - 1;
    uint16_t a = 0xaaaa;
    uint16_t b = 0;
    for (int i = 0; i < sum_location; i++) {
        b = (uint16_t)(data[i] ^ a);
        a = (uint16_t)((b << 1) | (b >> 15));
    }
    return a;
}

uint16_t write_u16_string_into_array(uint16_t *dst, int eeprom_size, int *dst_off, const char *str, int len) {
    int off = *dst_off;
    int byte_size = 2 + (len * 2);
    if (off * 2 + byte_size > eeprom_size) {
        fprintf(
            stderr,
            "Error: string exceeds EEPROM bounds (offset: %d, length: %d, EEPROM size: %d)\n",
            off * 2,
            byte_size,
            eeprom_size
        );
        return 0;
    }

    dst[off] = 0x300 | byte_size;
    for (int i = 0; i < len; i++) {
        dst[off+i+1] = str[i] & 0xff;
    }
    *dst_off = off + len + 1;
    return (byte_size << 8) | (off * 2);
}

void make_sc64_eeprom(uint16_t *data, int eeprom_size, const char *manufacturer, const char *serial_number) {
    memset(data, 0, eeprom_size);
    data[0] = 8 | 16 | 512; // FT1248, VCP, Bit_Order
    data[1] = 0x0403; // Vendor ID
    data[2] = 0x6014; // Product ID
    data[3] = 0x900;
    data[4] = 0xfa00 | 0x80; // MaxPower 500, (bus powered?)
    data[5] = 8; // Serial number enabled
    data[6] = 4 | 64; // SlowSlew, 4mA on both AC and AD

    int str_off = 80;
    int man_off    = write_u16_string_into_array(data, eeprom_size, &str_off, manufacturer, strlen(manufacturer));
    if (man_off    == 0) return;
    int prod_off   = write_u16_string_into_array(data, eeprom_size, &str_off, "SC64", 4);
    if (prod_off   == 0) return;
    int serial_off = write_u16_string_into_array(data, eeprom_size, &str_off, serial_number, strlen(serial_number));
    if (serial_off == 0) return;

    data[7] = man_off;
    data[8] = prod_off;
    data[9] = serial_off;
    data[10] = 0;
    data[11] = 0;
    data[12] = 0; // C0, C1, C2, C3 = Tristate
    data[13] = 0; // C4, C5, C6, C7 = Tristate
    data[14] = 0; // C8, C9 = Tristate

    int type = 82;
    if (eeprom_size < 128) { // 64
        type = 70;
    } else if (eeprom_size < 256) { // 128
        type = 86;
    } else if (eeprom_size < 1024) { // 256
        type = 102;
    }
    data[15] = type;
    data[46] = 0x302;
    data[69] = 72;

    int sum_location = (eeprom_size / 2) - 1;
    data[sum_location] = get_eeprom_sum(data, eeprom_size);
}

void bail_if_insufficient_args(char **argv, int argc, int cursor) {
    if (cursor + 1 >= argc) {
        fprintf(
            stderr,
            "Insufficient arguments for parameter \"%s\"\n"
            "Run \"%s --help\" for more information\n",
            argv[cursor],
            argv[0]
        );
        exit(1);
    }
}

void print_help(const char *prog) {
    printf(
        "SummerCart64 FT232H EEPROM tool\n"
        "Usage: %s <action> [options...]\n"
        "Actions:\n"
        "  dump\n"
        "   Read the EEPROM and dump it to stdout\n"
        "  flash\n"
        "   Write a new EEPROM\n"
        "Options:\n"
        "--help\n"
        "   Print this help menu\n"
        "--bus-device\n"
        "   Specify the device location according to bus:dev (eg. 001:005)\n"
        "   Defaults to finding the first device with VID:PID of 0403:6014\n"
        "--eeprom-size <number>\n"
        "   Specify the size of the EEPROM\n"
        "   Default size is 256\n"
        "--serial <string>\n"
        "   When flashing, set the serial number in the EEPROM\n"
        "   Defaults to SC64xxxxxx\n",
        prog
    );
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_help(argv[0]);
        return 0;
    }

    const char *action = argv[1];
    bool should_read  = strcmp(action, "dump") == 0;
    bool should_write = strcmp(action, "flash") == 0;
    if (!should_read && !should_write) {
        print_help(argv[0]);
        fprintf(stderr, "\nUnrecognised action \"%s\"\n", action);
        return 1;
    }

    int eeprom_size = 0;
    const char *serial_number = "SC64xxxxxx";
    const char *bus_dev = NULL;

    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            print_help(argv[0]);
            return 0;
        }
        else if (!strcmp(argv[i], "--bus-device")) {
            bail_if_insufficient_args(argv, argc, i);
            i++;
            bus_dev = argv[i];
        }
        else if (!strcmp(argv[i], "--eeprom-size")) {
            bail_if_insufficient_args(argv, argc, i);
            i++;
            eeprom_size = atoi(argv[i]);
        }
        else if (!strcmp(argv[i], "--serial")) {
            bail_if_insufficient_args(argv, argc, i);
            i++;
            serial_number = argv[i];
        }
    }

    if (eeprom_size < 64)
        eeprom_size = 256;

    uint8_t *buffer = calloc(eeprom_size, 1);

    struct ftdi_context *ftdi = ftdi_new();
    int res = ftdi_usb_open_desc(ftdi, 0x0403, 0x6014, NULL, NULL);
    if (res != 0) {
        fprintf(stderr, "Error: ftdi_usb_open_desc returned %d (%s)\n", res, ftdi->error_str);
        goto fail_after_new;
    }

    if (should_read) {
        MAYBE_BAIL(ftdi_read_eeprom, ftdi);
        MAYBE_BAIL(ftdi_get_eeprom_buf, ftdi, buffer, eeprom_size);
        fwrite(buffer, 1, eeprom_size, stdout);
    }

    if (should_write) {
        uint16_t *data = (uint16_t*)buffer;
        make_sc64_eeprom(data, eeprom_size, "Polprzewodnikowy", serial_number);

        uint16_t status;
        MAYBE_BAIL(ftdi_usb_reset, ftdi);
        MAYBE_BAIL(ftdi_poll_modem_status, ftdi, &status)
        MAYBE_BAIL(ftdi_set_latency_timer, ftdi, 0x77)

        for (int i = 0; i < eeprom_size / 2; i++) {
            if ((res = libusb_control_transfer(
                ftdi->usb_dev,
                FTDI_DEVICE_OUT_REQTYPE,
                SIO_WRITE_EEPROM_REQUEST,
                data[i],
                i,
                NULL,
                0,
                ftdi->usb_write_timeout
            )) < 0) {
                fprintf(stderr, "Error: unable to write eeprom (%d)\n", res);
                break;
            }
        }
    }

fail_after_open:
    ftdi_usb_close(ftdi);
fail_after_new:
    ftdi_deinit(ftdi);
    free(buffer);
    return 0;
}
