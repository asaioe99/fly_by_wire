/*
 * rtl-sdr, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 * Copyright (C) 2012 by Hoernchen <la@tfc-server.de>
 * Copyright (C) 2012 by Kyle Keen <keenerd@gmail.com>
 * Copyright (C) 2012 by Youssef Touil <youssef@sdrsharp.com>
 * Copyright (C) 2012 by Ian Gilmour <ian@sdrsharp.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include "getopt/getopt.h"
#endif

#ifdef NEED_PTHREADS_WORKARROUND
#define HAVE_STRUCT_TIMESPEC
#endif
#include <pthread.h>
#include <libusb.h>

#include <rtl-sdr.h>
#include <rtl_app_ver.h>
#include "convenience/convenience.h"
#include "convenience/rtl_convenience.h"

#ifdef _WIN32
#define sleep Sleep
#if defined(_MSC_VER) && (_MSC_VER < 1800)
#define round(x) (x > 0.0 ? floor(x + 0.5): ceil(x - 0.5))
#endif
#endif

#define ADSB_RATE			2000000
#define ADSB_FREQ			296930000
#define DEFAULT_ASYNC_BUF_NUMBER	12
#define DEFAULT_BUF_LENGTH		(16 * 16384)
#define AUTO_GAIN			-100

#define MESSAGEGO    253
#define OVERWRITE    254
#define BADSAMPLE    255

static pthread_t demod_thread;
static pthread_cond_t ready;
static pthread_mutex_t ready_m;
static volatile int do_exit = 0;
static rtlsdr_dev_t *dev = NULL;

uint16_t squares[256];

/* todo, bundle these up in a struct */
uint8_t *buffer;  /* also abused for uint16_t */
uint8_t binary[1125];
uint16_t p_high, p_low;
int verbose_output = 0;
int short_output = 0;
int quality = 10;
int allowed_errors = 5;
FILE *file;
int adsb_frame[717];
struct decode_place
{
	int            f; //in c90 can not use bool
	uint16_t  h_line;
};
struct decode_place dec_p[33333];
#define preamble_len	263
/* signals are not threadsafe by default */
#define safe_cond_signal(n, m) pthread_mutex_lock(m); pthread_cond_signal(n); pthread_mutex_unlock(m)
#define safe_cond_wait(n, m) pthread_mutex_lock(m); pthread_cond_wait(n, m); pthread_mutex_unlock(m)

void usage(void)
{
	fprintf(stderr,
		"rtl_adsb, a simple ADS-B decoder\n"
		"rtl_adsb version %d.%d %s (%s)\n"
		"rtl-sdr  library %d.%d %s\n\n",
		APP_VER_MAJOR, APP_VER_MINOR, APP_VER_ID, __DATE__,
		rtlsdr_get_version() >>16, rtlsdr_get_version() & 0xFFFF,
		rtlsdr_get_ver_id() );
	fprintf(stderr,
		"Usage:\trtl_adsb [-R] [-g gain] [-p ppm] [output file]\n"
		"\t[-d device_index or serial (default: 0)]\n"
		"\t[-V verbove output (default: off)]\n"
		"\t[-S show short frames (default: off)]\n"
		"\t[-Q quality (0: no sanity checks, 0.5: half bit, 1: one bit (default), 2: two bits)]\n"
		"\t[-e allowed_errors (default: 5)]\n"
		"\t[-g tuner_gain (default: automatic)]\n"
		"\t[-p ppm_error (default: 0)]\n"
		"\t[-T enable bias-T on GPIO PIN 0 (works for rtl-sdr.com v3 dongles)]\n"
		"\tfilename (a '-' dumps samples to stdout)\n"
		"\t (omitting the filename also uses stdout)\n\n"
		"Streaming with netcat:\n"
		"\trtl_adsb | netcat -lp 8080\n"
		"\twhile true; do rtl_adsb | nc -lp 8080; done\n"
		"Streaming with socat:\n"
		"\trtl_adsb | socat -u - TCP4:sdrsharp.com:47806\n"
		"\n");
	exit(1);
}

#ifdef _WIN32
BOOL WINAPI
sighandler(int signum)
{
	if (CTRL_C_EVENT == signum) {
		fprintf(stderr, "Signal caught, exiting!\n");
		do_exit = 1;
		rtlsdr_cancel_async(dev);
		return TRUE;
	}
	return FALSE;
}
#else
static void sighandler(int signum)
{
	fprintf(stderr, "Signal caught, exiting!\n");
	do_exit = 1;
	rtlsdr_cancel_async(dev);
}
#endif

int abs8(int x)
/* do not subtract 127 from the raw iq, this handles it */
{
	if (x >= 127) {
		return x - 127;}
	return 127 - x;
}

void squares_precompute(void)
/* equiv to abs(x-128) ^ 2 */
{
	int i, j;
	// todo, check if this LUT is actually any faster
	for (i=0; i<256; i++) {
		j = abs8(i);
		squares[i] = (uint16_t)(j*j);
	}
}
void set_dec_p()
{
	int i;
	int i2 = 0;
	for (i=0;i<33333;i++) {
		if ((i2 % 2200) < 1920 ) {
			dec_p[i].f = 1;
		} else {
			dec_p[i].f = 0;
		}
		dec_p[i].h_line = i2 / 2200;
		i2 = i2 +74;
		//i2 = i2 % 2200;
		if (i % 4 == 3) { i2++; }
	}
}


int magnitute(uint8_t *buf, int len)
/* takes i/q, changes buf in place (16 bit), returns new len (16 bit) */
{
	int i;
	uint16_t *m;
	for (i=0; i<len; i+=2) {
		m = (uint16_t*)(&buf[i]);
		*m = squares[buf[i]] + squares[buf[i+1]];
		//printf("%5d, ",*m);
	}
	return len/2;
}

static inline uint16_t bit_read(uint16_t a, uint16_t b, uint16_t c)
/* takes 4 consecutive real samples, return 0 or 1, BADSAMPLE on error */
{
	int bit_h, bit_l;
	bit_h = c > a;
	bit_l = b > c;

        if ((bit_h == 1) || (bit_l == 1)) {
               return BADSAMPLE;
	}
	if (bit_h == 1) {
		return 1;
	} else {
		return 0;
	}
}

static inline uint16_t min16(uint16_t a, uint16_t b)
{
	return a<b ? a : b;
}

static inline uint16_t max16(uint16_t a, uint16_t b)
{
	return a>b ? a : b;
}

static inline int preamble(uint16_t *buf, int i)
/* returns 0/1 for preamble at index i */
{
	int i2, i3;
	p_high = 0;
	p_low  = 0;
	int32_t cor_n[31]  = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	int32_t cor_h[5] =  {0,0,0,0,0};
	int32_t cor_l[4] =  {0,0,0,0};

	for (i3=-15;i3<16;i3++) {
		for (i2=26; i2<30; i2++) {
			cor_n[i3+15] += (int32_t)buf[i+i2+i3];
			cor_n[i3+15] += (int32_t)buf[i+i2+i3+30];
			cor_n[i3+15] += (int32_t)buf[i+i2+i3+60];
			cor_n[i3+15] += (int32_t)buf[i+i2+i3+89];
			cor_n[i3+15] += (int32_t)buf[i+i2+i3+119];
			cor_n[i3+15] += (int32_t)buf[i+i2+i3+149];
			cor_n[i3+15] += (int32_t)buf[i+i2+i3+178];
			cor_n[i3+15] += (int32_t)buf[i+i2+i3+208];
			cor_n[i3+15] += (int32_t)buf[i+i2+i3+237];
			cor_n[i3+15] += (int32_t)buf[i+i2+i3+267];
			cor_n[i3+15] += (int32_t)buf[i+i2+i3+297];
			cor_n[i3+15] += (int32_t)buf[i+i2+i3+326];
			cor_n[i3+15] += (int32_t)buf[i+i2+i3+356];
		}
		cor_n[i3+15] -= (int32_t)buf[i+i3+89];
		cor_n[i3+15] -= (int32_t)buf[i+i3+178];
		cor_n[i3+15] -= (int32_t)buf[i+i3+326];

		//if (i3==0 && (cor_n[0] > cor_n[15])) {return 0;}
		//if (i3==1 && (cor_n[0] > cor_n[15])) {return 0;}
		//if (i3==3 && (cor_n[5] < cor_n[8])) {return 0;}
		//if (i3==5 && (cor_n[5] < cor_n[10])) {return 0;}

	}
	for (i2=0;i2<15;i2++) {
		if (cor_n[15] < cor_n[i2]) {return 0;}
	}
	for (i2=16;i2<31;i2++) {
		if (cor_n[15] < cor_n[i2]) {return 0;}
	}
	for (i2=0; i2<26; i2++) {
		cor_h[0] += (int32_t)buf[i+i2];
		cor_l[0] += (int32_t)buf[i+i2+30];
		cor_h[1] += (int32_t)buf[i+i2+60];
		cor_l[1] += (int32_t)buf[i+i2+89];
		cor_h[2] += (int32_t)buf[i+i2+119];
		cor_l[2] += (int32_t)buf[i+i2+149];
		cor_h[3] += (int32_t)buf[i+i2+178];
		cor_l[3] += (int32_t)buf[i+i2+208];
		cor_h[4] += (int32_t)buf[i+i2+238];
	}
	cor_h[4] = cor_h[4] - (int32_t)buf[i+25+238];


	if (cor_h[0] < cor_l[0]) {printf("1");return 0;}
	if (cor_h[1] < cor_l[1]) {printf("2");return 0;}
	if (cor_h[2] < cor_l[2]) {printf("3");return 0;}
	if (cor_h[3] < cor_l[3]) {printf("4");return 0;}
	if (cor_h[1] < cor_l[0]) {printf("5");return 0;}
	if (cor_h[2] < cor_l[1]) {printf("6");return 0;}
	if (cor_h[3] < cor_l[2]) {printf("7");return 0;}
	if (cor_h[4] < cor_l[3]) {printf("8");return 0;}
	printf("\n");

	p_high = (uint16_t)((cor_h[0]+cor_h[1]+cor_h[2]+cor_h[3]+cor_h[4]) / 129);
	p_low = (uint16_t)((cor_l[0]+cor_l[1]+cor_l[2]+cor_l[3]) / 104);

	for (i2=0; i2<386; i2++) {
		printf("%d,",buf[i+i2]);
	}
	printf("\ndetect preamble at i=%d %d:%d\n",i,p_high,p_low);
	return 1;
}

int manchester(uint16_t *buf, int len)
/* overwrites magnitude buffer with valid bits (BADSAMPLE on errors) */
{
	/* a and b hold old values to verify local manchester */
	int i, i2, i3, i4, line;
	int end = 1;//temp
	int n_begin = 1;
	int ave;
	int maximum_i = len - 1;        // len-1 since we look at i and i+1
	// todo, allow wrap across buffers
	i = 2;
	while (i < maximum_i) {
		/* find preamble */
		for ( ; i < (len - preamble_len); i++) {
			if (!preamble(buf, i)) {
				continue;}
			i3 = 0;
			for ( ; i<(len - preamble_len); ) {
				if (dec_p[i].f == 1) {
					for (i2=23;i2<28;i2++) {
						if (dec_p[i+i2].f==0) {
							end = i2;
							for(i4=2;i4<6;i4++) {
								if (dec_p[i+i2+i4].f==1) {
									end = end + i4;
								}
							}
						}
					}
					ave = 0;
					for (i2=0;i2<end;i2++) {
						ave = ave + buf[i+i2];
					}
					ave = ave / end;
					i = i + end;
					line = (p_high >> 1) + (p_low >> 1);
					if (i3 < 1125) {
						binary[i3] = ave > line ? 1 : 0;
						printf("%4d %d,",i3,binary[i3]);
						i3++;
					}
				} else {
					i++;
				}
			}
		}
		/* mark bits until encoding breaks */
		//for ( ; i < maximum_i; i++) {
		//	buf[i] = bit_read(p_high, p_low, buf[i]);
		//}
	}

	return i3 - 1;
}

void messages(uint16_t *buf, int len)
{
	int i, data_i, index;
	// todo, allow wrap across buffers

	data_i = 0;
	index = 0;
	for (i=0; i<len; i++) {
		if (index == 0) {adsb_frame[data_i] = 0x00;}

		if (buf[i]==1) {adsb_frame[data_i] = adsb_frame[data_i] | 0x01 << index;}
		index = (index + 1) % 8;

		if (index == 0) {
			data_i++;
			data_i = data_i % 717;
			if (data_i < 34) {
				//printf("data_i:%d ",data_i,adsb_frame[data_i]);
				fprintf(file, "%c",adsb_frame[data_i]);
				fflush(file);
			}
		}
	}
}

static void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx)
{
	if (do_exit) {
		return;}
	memcpy(buffer, buf, len);
	safe_cond_signal(&ready, &ready_m);
}

static void *demod_thread_fn(void *arg)
{
	int len;
	int pre;
	while (!do_exit) {
		safe_cond_wait(&ready, &ready_m);
		len = magnitute(buffer, DEFAULT_BUF_LENGTH);
		pre = manchester((uint16_t*)buffer, len);
		if (pre == 1) { messages((uint16_t*)buffer, len);}
	}
	rtlsdr_cancel_async(dev);
	return 0;
}

int main(int argc, char **argv)
{
#ifndef _WIN32
	struct sigaction sigact;
#endif
	char *filename = NULL;
	int r, opt;
	int gain = AUTO_GAIN; /* tenths of a dB */
	int dev_index = 0;
	int dev_given = 0;
	int ppm_error = 0;
	int enable_biastee = 0;
	pthread_cond_init(&ready, NULL);
	pthread_mutex_init(&ready_m, NULL);
	squares_precompute();
	set_dec_p();

	while ((opt = getopt(argc, argv, "d:g:p:e:Q:VST")) != -1)
	{
		switch (opt) {
		case 'd':
			dev_index = verbose_device_search(optarg);
			dev_given = 1;
			break;
		case 'g':
			gain = (int)(atof(optarg) * 10);
			break;
		case 'p':
			ppm_error = atoi(optarg);
			break;
		case 'V':
			verbose_output = 1;
			break;
		case 'S':
			short_output = 1;
			break;
		case 'e':
			allowed_errors = atoi(optarg);
			break;
		case 'Q':
			quality = (int)(atof(optarg) * 10);
			break;
		case 'T':
			enable_biastee = 1;
			break;
		default:
			usage();
			return 0;
		}
	}

	if (argc <= optind) {
		filename = "-";
	} else {
		filename = argv[optind];
	}

	buffer = malloc(DEFAULT_BUF_LENGTH * sizeof(uint8_t));

	if (!dev_given) {
		dev_index = verbose_device_search("0");
	}

	if (dev_index < 0) {
		exit(1);
	}

	r = rtlsdr_open(&dev, (uint32_t)dev_index);
	if (r < 0) {
		fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
		exit(1);
	}
#ifndef _WIN32
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);
#else
	SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sighandler, TRUE );
#endif

	if (strcmp(filename, "-") == 0) { /* Write samples to stdout */
		file = stdout;
		setvbuf(stdout, NULL, _IONBF, 0);
#ifdef _WIN32
		_setmode(_fileno(file), _O_BINARY);
#endif
	} else {
		file = fopen(filename, "wb");
		if (!file) {
			fprintf(stderr, "Failed to open %s\n", filename);
			exit(1);
		}
	}

	/* Set the tuner gain */
	if (gain == AUTO_GAIN) {
		verbose_auto_gain(dev);
	} else {
		gain = nearest_gain(dev, gain);
		verbose_gain_set(dev, gain);
	}

	verbose_ppm_set(dev, ppm_error);
	r = rtlsdr_set_agc_mode(dev, 1);

	/* Set the tuner frequency */
	verbose_set_frequency(dev, ADSB_FREQ);

	/* Set the sample rate */
	verbose_set_sample_rate(dev, ADSB_RATE);

	rtlsdr_set_bias_tee(dev, enable_biastee);
	if (enable_biastee)
		fprintf(stderr, "activated bias-T on GPIO PIN 0\n");

	/* Reset endpoint before we start reading from it (mandatory) */
	verbose_reset_buffer(dev);

	pthread_create(&demod_thread, NULL, demod_thread_fn, (void *)(NULL));
	rtlsdr_read_async(dev, rtlsdr_callback, (void *)(NULL),
			      DEFAULT_ASYNC_BUF_NUMBER,
			      DEFAULT_BUF_LENGTH);

	if (do_exit) {
		fprintf(stderr, "\nUser cancel, exiting...\n");}
	else {
		fprintf(stderr, "\nLibrary error %d, exiting...\n", r);}
	rtlsdr_cancel_async(dev);
	pthread_cancel(demod_thread);
	pthread_join(demod_thread, NULL);
	pthread_cond_destroy(&ready);
	pthread_mutex_destroy(&ready_m);

	if (file != stdout) {
		fclose(file);}

	rtlsdr_close(dev);
	free(buffer);
	return r >= 0 ? r : -r;
}

