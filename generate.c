#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define N_TYPES 5

#define DEF_NAME "out.wav"
#define DEF_TYPE 1
#define DEF_FREQ 440.0
#define DEF_AMPL 0.5
#define DEF_HARM 4
#define DEF_RATE 44100

typedef unsigned char u8;

typedef struct {
	char riff_magic[4];
	int riff_size;
	char riff_fmt[4];
	char fmt_magic[4];
	int fmt_size;
	short audio_fmt;
	short n_channels;
	int sample_rate;
	int byte_rate;
	short block_align;
	short bits_per_sample;
	char data_magic[4];
	int data_size;
} wav_t;

void make_wavhdr(wav_t *hdr, int rate, int length) {
	if (!hdr) return;

	const int bps = sizeof(float);
	const int n_ch = 1;

	int sz = length * bps * n_ch;
	memset(hdr, 0, sizeof(wav_t));

	memcpy(hdr->riff_magic, "RIFF", 4);
	hdr->riff_size = sz + 36;
	memcpy(hdr->riff_fmt, "WAVE", 4);
	memcpy(hdr->fmt_magic, "fmt ", 4);
	hdr->fmt_size = 16;
	hdr->audio_fmt = 3;
	hdr->n_channels = n_ch;
	hdr->sample_rate = rate;
	hdr->byte_rate = rate * bps * n_ch;
	hdr->block_align = hdr->byte_rate / rate;
	hdr->bits_per_sample = bps * 8;
	memcpy(hdr->data_magic, "data", 4);
	hdr->data_size = sz;
}

u8 *open_wav(char *name, int rate, int start, int length) {
	if (!name || start < 0 || length < 1)
		return NULL;

	wav_t hdr;

	FILE *f = fopen(name, "rb");
	int sz = 0;
	if (f) {
		fseek(f, 0, SEEK_END);
		sz = ftell(f);
		rewind(f);
	}

	if (!f || sz < 1) {
		make_wavhdr(&hdr, rate > 0 ? rate : DEF_RATE, length);
		u8 *temp = calloc(sizeof(wav_t) + length * sizeof(float), 1);
		memcpy(temp, &hdr, sizeof(wav_t));
		return temp;
	}

	u8 *buf = malloc(sz);
	fread(buf, 1, sz, f);
	fclose(f);

	int data_sz = sz - sizeof(wav_t);
	if (data_sz < 1) {
		printf("Error: file is too small to be a WAV file\n");
		free(buf);
		return NULL;
	}

	memcpy(&hdr, buf, sizeof(wav_t));

	if (memcmp(hdr.riff_magic, "RIFF", 4) ||
		memcmp(hdr.riff_fmt, "WAVE", 4) ||
		memcmp(hdr.fmt_magic, "fmt ", 4)) {
			printf("Error: file is not a WAV file\n");
			free(buf);
			return NULL;
	}

	if (hdr.data_size != data_sz) {
		printf("Error: invalid data size in header\n"
		       "       expected %d, got %d\n", data_sz, hdr.data_size);
		free(buf);
		return NULL;
	}

	if (hdr.audio_fmt != 3 || hdr.n_channels != 1) {
		printf("Error: unsupported WAV format\n"
		       "       this program expects the input WAV file to be\n"
			   "        mono and use floating-point samples\n");
		free(buf);
		return NULL;
	}

	if (rate > 0 && hdr.sample_rate != rate) {
		printf("Error: sampling rate mismatch\n"
		       "       WAV sampling rate: %d, input sampling rate: %d\n",
			hdr.sample_rate, rate);
		free(buf);
		return NULL;
	}

	int data_len = data_sz / sizeof(float);
	if (data_len < start + length) {
		hdr.data_size = (start + length) * sizeof(float);
		hdr.riff_size = hdr.data_size + 36;

		buf = realloc(buf, sizeof(wav_t) + hdr.data_size);
		memset(buf + sizeof(wav_t) + data_sz, 0, hdr.data_size - data_sz);

		data_sz = hdr.data_size;
		data_len = data_sz / sizeof(float);
	}

	return buf;
}

void noise(float *buf, float freq, float ampl, int rate, int length) {
	FILE *f = fopen("/dev/urandom", "rb");
	if (!f)
		return;

	unsigned short *rand = calloc(length, 2);
	fread(rand, 2, length, f);
	fclose(f);

	int i;
	for (i = 0; i < length; i++)
		buf[i] = ampl * (-1.0 + (float)rand[i] / 32767.5);

	free(rand);
}

void sawtooth(float *buf, float freq, float ampl, int rate, int length) {
	float sign = ampl < 0.0 ? -1.0 : 1.0;
	ampl *= sign;

	float grad = (ampl * 2) / ((float)rate / freq);
	float samp = -ampl;

	int i;
	for (i = 0; i < length; i++) {
		if (samp > ampl) samp -= ampl * 2;
		buf[i] += samp * sign;
		samp += grad;
	}
}

void sine(float *buf, float freq, float ampl, int rate, int length) {
	float period = 2 * M_PI * freq / (float)rate;
	int i;
	for (i = 0; i < length; i++)
		buf[i] += ampl * sin((float)i * period);
}

void square(float *buf, float freq, float ampl, int rate, int length) {
	float period = freq / (float)rate;
	int i;
	for (i = 0; i < length; i++) {
		int pos = (float)i * period;
		buf[i] += pos % 2 ? -ampl : ampl;
	}
}

void triangle(float *buf, float freq, float ampl, int rate, int length) {
	float sign = ampl < 0.0 ? -1.0 : 1.0;
	ampl *= sign;

	float grad = (ampl * 2) / ((float)rate / freq);
	float samp = -ampl;
	float inv = sign;
	int i;
	for (i = 0; i < length; i++) {
		if (samp > ampl) {
			samp -= ampl * 2;
			inv *= -1.0;
		}

		buf[i] += samp * inv;
		samp += grad;
	}
}

const char *wave_types = "nwiqt";

void (*generate[])(float *buf, float freq, float ampl, int rate, int length) = {
	noise, sawtooth, sine, square, triangle
};

void print_usage(char *argv0) {
	printf("Invalid arguments\n"
	       "Usage: %s <options>\n"
	       "\t-o <output file>\n"
		   "\t-t <type of wave>\n"
		   "\t\tw = sawtooth, i = sine, q = square, t = triangle\n"
	       "\t-f <frequency>\n"
	       "\t-a <amplitude>\n"
		   "\t-s <starting point in seconds>\n"
	       "\t-l <length in seconds>\n"
	       "\t-r <sample rate>\n", argv0);
}

int main(int argc, char **argv) {
	char *name = DEF_NAME;
	int type = DEF_TYPE;
	float freq = DEF_FREQ;
	float ampl = DEF_AMPL;
	int harm = DEF_HARM;
	int rate = DEF_RATE;

	int rate_set = 0;
	int i, j;
	for (i = 1; i < argc-1; i++) {
		if (strlen(argv[i]) != 2 || argv[i][0] != '-') {
			print_usage(argv[0]);
			return 1;
		}

		switch (argv[i][1]) {
			case 't':
			{
				type = -1;
				i++;

				int j;
				for (j = 0; j < N_TYPES; j++) {
					if (argv[i][0] == wave_types[j]) {
						type = j;
						break;
					}
				}

				if (type < 0) {
					printf("Error: invalid wave type '%c'\n", argv[i][0]);
					return 2;
				}

				break;
			}
			case 'o':
				name = argv[++i];
				break;
			case 'f':
				freq = atof(argv[++i]);
				if (freq < 0.0) freq = DEF_FREQ;
				break;
			case 'a':
				ampl = atof(argv[++i]);
				if (ampl < -1.0 || ampl > 1.0) ampl = DEF_AMPL;
				break;
			case 'h':
				harm = atoi(argv[++i]);
				if (harm < 1) harm = DEF_HARM;
				break;
			case 'r':
				rate = atoi(argv[++i]);
				if (rate < 1) rate = DEF_RATE;
				else rate_set = 1;
				break;
			case 's':
			case 'l':
				break;
			default:
				print_usage(argv[0]);
				return 1;
		}
	}

	int start = 0;
	int length = rate;

	for (i = 1; i < argc-1; i++) {
		if (strlen(argv[i]) != 2 || argv[i][0] != '-')
			continue;

		if (argv[i][1] == 's') {
			start = (float)rate * atof(argv[++i]);
			break;
		}
		if (argv[i][1] == 'l') {
			length = (float)rate * atof(argv[++i]);
			break;
		}
	}

	u8 *buf = open_wav(name, rate_set ? rate : 0, start, length);
	if (!buf)
		return 3;

	float *data = (float*)(buf + sizeof(wav_t));
	if (type >= 0 && type < N_TYPES)
		generate[type](data + start, freq, ampl, rate, length);

	wav_t *header = (wav_t*)buf;
	int size = sizeof(wav_t) + header->data_size;

	FILE *f = fopen(name, "wb");
	fwrite(buf, 1, size, f);
	fclose(f);

	free(buf);
	return 0;
}