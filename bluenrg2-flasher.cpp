#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <memory>
#include <string>

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/signal.h>

#include <libusb-1.0/libusb.h>

#define VENDOR  0x0483
#define PRODUCT 0x5741

#define START_ADDRESS 0x10040000

#define STDIN_BUF_SIZE    256
#define RX_BUF_SIZE     16384

// in milliseconds
#define CONFIG_TIMEOUT  100
#define FLASH_TIMEOUT   200

#define GET_LINE_CODING(dummy)        libusb_control_transfer(dev, 0xa1, 0x21, 0, 0, (u8*)dummy, 7, 100)
#define SET_LINE_CODING(line_coding)  libusb_control_transfer(dev, 0x21, 0x20, 0, 0, (u8*)line_coding, 7, 100)
#define SET_CTRL_LINE_STATE(state)    libusb_control_transfer(dev, 0x21, 0x22, state, 0, nullptr, 0, 100)

typedef unsigned char u8;
typedef unsigned int u32;

static const u8 api_line_coding[] = {0x64, 0, 0, 0, 0, 0, 8};
static const u8 transfer_line_coding[] = {0, 8, 7, 0, 0, 0, 8};
static const u8 serial_line_coding[] = {0, 0xc2, 1, 0, 0, 0, 8};

std::string filename;

static struct termios old_term = {0};
static int old_fl = 0;
static struct termios new_term = {0};
static int new_fl = 0;

static int reattach_drivers = 0;

static libusb_device_handle *dev = nullptr;
static struct timeval short_timeout = {0, 20 * 1000};

static u8 tx_buf[1024];

static bool can_free_read_tx = false;
static bool should_print = false;
static bool waiting_for_output = false;

static void handle_read_tx(libusb_transfer *tx) {
	if (tx->status == LIBUSB_TRANSFER_CANCELLED)
		can_free_read_tx = true;
}

static void handle_serial_tx(libusb_transfer *tx) {
	if (tx->status == LIBUSB_TRANSFER_COMPLETED) {
		waiting_for_output = false;
		if (should_print && tx->endpoint == 0x83 && tx->actual_length > 0)
			write(STDOUT_FILENO, tx->buffer, tx->actual_length);
	}
}

u8 bytes_xored(u8 *buf, int size) {
	u8 x = 0;
	for (int i = 0; i < size; i++) x ^= buf[i];
	return x;
}

struct timeval from_ms(int total_ms) {
	int secs = total_ms / 1000;
	int ms   = total_ms % 1000;
	return (struct timeval) {secs, ms * 1000};
}

libusb_transfer *start_configuration(const u8 *line_coding, u8 *buffer, int size, libusb_transfer_cb_fn callback, int timeout) {
	u8 dummy[7];

	libusb_transfer *read_tx = libusb_alloc_transfer(0);

	libusb_fill_bulk_transfer(read_tx, dev, 0x83, buffer, size, callback, nullptr, timeout);

	// no timeout for the interrupt transfer. this is so that it can act as a kind of signalling mechanism
	//libusb_fill_interrupt_transfer(int_tx, dev, 0x82, &tx_buf[512], 512, handle_int_transfer, nullptr, 0);

	libusb_submit_transfer(read_tx);
	//libusb_submit_transfer(int_tx);

	GET_LINE_CODING(dummy);
	GET_LINE_CODING(dummy);
	GET_LINE_CODING(dummy);
	GET_LINE_CODING(dummy);

	SET_LINE_CODING(line_coding);
	GET_LINE_CODING(dummy);
	SET_CTRL_LINE_STATE(3);
	SET_LINE_CODING(line_coding);
	GET_LINE_CODING(dummy);

	usleep(100 * 1000);
	return read_tx;
}

void end_configuration(libusb_transfer *read_tx) {
	can_free_read_tx = false;
	libusb_cancel_transfer(read_tx);

	struct timeval timeout = from_ms(read_tx->timeout);
	libusb_handle_events_timeout(nullptr, &timeout);

	if (can_free_read_tx)
		libusb_free_transfer(read_tx);

	SET_CTRL_LINE_STATE(2);
}

bool submit_command(libusb_transfer *read_tx, u8 *buf, int size) {
	int tx = 0;
	libusb_bulk_transfer(dev, 3, buf, size, &tx, 100);

	struct timeval timeout = from_ms(read_tx->timeout);
	bool success = libusb_handle_events_timeout(nullptr, &timeout) == LIBUSB_SUCCESS;

	u8 temp;
	libusb_bulk_transfer(dev, 0x83, &temp, 0, &tx, 100);

	libusb_submit_transfer(read_tx);

	return success;
}

void submit_command(libusb_transfer *read_tx, std::string str) {
	submit_command(read_tx, (u8*)str.data(), str.size());
}

std::vector<u8> reload_file() {
	std::vector<u8> file;
	const char *fname = filename.c_str();

	FILE *f = fopen(fname, "rb");
	if (!f) {
		printf("Could not open \"%s\"", fname);
		return file;
	}

	fseek(f, 0, SEEK_END);
	int sz = ftell(f);
	rewind(f);

	if (sz < 1) {
		printf("File \"%s\" could not be read\n", fname);
		return file;
	}

	file.resize(sz);
	fread(file.data(), 1, sz, f);
	fclose(f);

	return file;
}

void flash_image() {
	puts("\nFlashing...");

	std::vector<u8> file = reload_file();
	if (!file.size())
		return;

	libusb_transfer *init_read_tx = start_configuration(api_line_coding, &tx_buf[0], 512, handle_read_tx, CONFIG_TIMEOUT);
	submit_command(init_read_tx, "\nswitchDtmInterface 0\n");
	submit_command(init_read_tx, "\nBlueBootloader\n");
	end_configuration(init_read_tx);

	libusb_transfer *flash_read_tx = start_configuration(transfer_line_coding, &tx_buf[0], 512, handle_read_tx, FLASH_TIMEOUT);

	u8 cmd1 = 0x7f;
	if (!submit_command(flash_read_tx, &cmd1, 1)) {
		end_configuration(flash_read_tx);
		puts("Failed to set up flash transfer (retrying may work)\n");
		return;
	}

	u8 cmd2[] = {0x02, 0xfd};
	submit_command(flash_read_tx, cmd2, 2);

	int left = file.size();
	int chunks = 0;
	int offset = 0;

	u8 chunk_buf[258];

	while (left > 0) {
		if (chunks % 8 == 0) {
			int section = chunks / 8;
			int sec_h = (section >> 8) & 0xff;
			int sec_l = section & 0xff;

			u8 cmd3[] = {0x43, 0xbc};
			submit_command(flash_read_tx, cmd3, 2);
			u8 cmd4[] = {(u8)sec_h, (u8)sec_l, (u8)(sec_h ^ sec_l)};
			submit_command(flash_read_tx, cmd4, 3);
		}

		u32 addr = START_ADDRESS + (chunks * 0x100);

		u8 cmd5[] = {0x31, 0xce};
		submit_command(flash_read_tx, cmd5, 2);
		u8 cmd6[] = {(u8)(addr >> 24), (u8)(addr >> 16), (u8)(addr >> 8), (u8)addr, bytes_xored((u8*)&addr, sizeof(u32))};
		submit_command(flash_read_tx, cmd6, 5);

		int tx = 256;
		if (tx > left) tx = left;

		chunk_buf[0] = (tx - 1) & 0xff;
		memcpy(&chunk_buf[1], file.data() + offset, tx);
		chunk_buf[tx+1] = bytes_xored(&chunk_buf[0], tx + 1); // we include the size byte in the range to xor

		submit_command(flash_read_tx, chunk_buf, tx + 2);

		chunks++;
		offset += tx;
		left -= tx;
	}

	end_configuration(flash_read_tx);

	libusb_transfer *finish_read_tx = start_configuration(api_line_coding, &tx_buf[0], 512, handle_read_tx, CONFIG_TIMEOUT);
	submit_command(finish_read_tx, "\nBlueReset 1\n");
	submit_command(finish_read_tx, "\nBlueReset 0\n");
	end_configuration(finish_read_tx);

	puts("Done! Entering console... (hit Ctrl+R to flash again)\n");
}

void close_libusb() {
	if (reattach_drivers & 1)
		libusb_attach_kernel_driver(dev, 0);
	if (reattach_drivers & 2)
		libusb_attach_kernel_driver(dev, 1);

	libusb_close(dev);
	libusb_exit(nullptr);
}

void restore_terminal() {
	tcsetattr(STDIN_FILENO, TCSADRAIN, &old_term);
	fcntl(STDIN_FILENO, F_SETFL, old_fl);
}

static void signal_handler(int signum) {
	printf("Exiting\n");
	close_libusb();
	restore_terminal();
	exit(0);
}

int main(int argc, char **argv) {
	signal(SIGINT, signal_handler);
	tcgetattr(STDIN_FILENO, &old_term);
	old_fl = fcntl(STDIN_FILENO, F_GETFL);

	puts("BlueNRG2 flasher");

	if (argc < 2) {
		puts("Requires file to flash");
		return 1;
	}

	filename = std::string(argv[1]);

	libusb_init(nullptr);
	dev = libusb_open_device_with_vid_pid(NULL, VENDOR, PRODUCT);
	if (!dev) {
		puts("Could not open BlueNRG2 dev kit");
		return 4;
	}

	if (libusb_kernel_driver_active(dev, 0)) {
		libusb_detach_kernel_driver(dev, 0);
		reattach_drivers |= 1;
	}
	if (libusb_kernel_driver_active(dev, 1)) {
		libusb_detach_kernel_driver(dev, 1);
		reattach_drivers |= 2;
	}

	new_term = old_term;
	new_term.c_lflag &= ~ICANON;
	new_term.c_lflag &= ~ECHO;
	new_term.c_cc[VMIN] = 1;
	new_term.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
	new_fl = old_fl | O_NONBLOCK;
	fcntl(STDIN_FILENO, F_SETFL, new_fl);

	char stdin_buffer[STDIN_BUF_SIZE];
	auto rx_buffer = std::make_unique<char[]>(RX_BUF_SIZE);

	should_print = false;
	while (true) {
		flash_image();
		libusb_transfer *serial_tx = start_configuration(serial_line_coding, (u8*)rx_buffer.get(), RX_BUF_SIZE, handle_serial_tx, 0);
		should_print = true;

		while (true) {
			int input_len = read(STDIN_FILENO, stdin_buffer, STDIN_BUF_SIZE);

			if (input_len > 0) {
				char cmd = stdin_buffer[0];

				if (cmd == 0x12) { // Ctrl+R
					if (waiting_for_output)
						libusb_cancel_transfer(serial_tx);
					break;
				}

				int temp = 0;
				libusb_bulk_transfer(dev, 3, (u8*)stdin_buffer, input_len, &temp, 100);
			}

			if (!waiting_for_output) {
				waiting_for_output = true;
				libusb_submit_transfer(serial_tx);
			}

			libusb_handle_events_timeout(nullptr, &short_timeout);
		}

		should_print = false;
		end_configuration(serial_tx);
	}

	close_libusb();
	restore_terminal();

	return 0;
}
