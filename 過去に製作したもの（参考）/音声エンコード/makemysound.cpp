
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#define PIXEL_NUM_X (1920)
#define PIXEL_NUM_Y (1080)
#define COLOR_BIT (24)
#define PIC_DATA_SIZE (PIXEL_NUM_X * 3 * PIXEL_NUM_Y)
#pragma warning(disable : 4996)
int fputc2LowHigh(unsigned short d, FILE* s)
{
	putc(d & 0xFF, s);
	return putc(d >> CHAR_BIT, s);
}
int fputc4LowHigh(unsigned long d, FILE* s)
{
	putc(d & 0xFF, s);
	putc((d >> CHAR_BIT) & 0xFF, s);
	putc((d >> CHAR_BIT * 2) & 0xFF, s);
	return putc((d >> CHAR_BIT * 3) & 0xFF, s);
}
void createPic(unsigned char* b, int x, int y, int freqr,int freqb, int freqg)
{
	unsigned char red;
	unsigned char green;
	unsigned char blue;
	int i;
	int j;

	/* データを生成 */
	for (i = 0; i < y; i++) {
		red = 8 + 38* (unsigned char)(sin(double(freqr) * double(i) / double(10934)) + 1);
		blue = 8 + 38*(unsigned char)(sin(double(freqb) * double(i) / double(10934)) + 1);
		green = 8 + 38*(unsigned char)(sin(double(freqg) * double(i) / double(10934)) + 1);
		/* 1行分のデータを出力 */
		for (j = 0; j < x; j++) {
			*b = blue;
			b++;
			*b = green;
			b++;
			*b = red;
			b++;
		}
	}
}
int putBmpHeader(FILE* s, int x, int y, int c)
{
	int i;
	int color; /* 色数 */
	unsigned long int bfOffBits; /* ヘッダサイズ(byte) */

	/* 画像サイズが異常の場合,エラーでリターン */
	if (x <= 0 || y <= 0) {
		return 0;
	}

	/* 出力ストリーム異常の場合,エラーでリターン */
	if (s == NULL || ferror(s)) {
		return 0;
	}

	/* 色数を計算 */
	if (c == 24) {
		color = 0;
	}
	else {
		color = 1;
		for (i = 1; i <= c; i++) {
			color *= 2;
		}
	}

	/* ヘッダサイズ(byte)を計算 */
	/* ヘッダサイズはビットマップファイルヘッダ(14) + ビットマップ情報ヘッダ(40) + 色数 */
	bfOffBits = 14 + 40 + 4 * color;

	/* ビットマップファイルヘッダ(計14byte)を書出 */
	/* 識別文字列 */
	fputs("BM", s);

	/* bfSize ファイルサイズ(byte) */
	fputc4LowHigh(bfOffBits + (unsigned long)x * y, s);

	/* bfReserved1 予約領域1(byte) */
	fputc2LowHigh(0, s);

	/* bfReserved2 予約領域2(byte) */
	fputc2LowHigh(0, s);

	/* bfOffBits ヘッダサイズ(byte) */
	fputc4LowHigh(bfOffBits, s);

	/* ビットマップ情報ヘッダ(計40byte) */
	/* biSize 情報サイズ(byte) */
	fputc4LowHigh(40, s);

	/* biWidth 画像Xサイズ(dot) */
	fputc4LowHigh(x, s);

	/* biHeight 画像Yサイズ(dot) */
	fputc4LowHigh(y, s);

	/* biPlanes 面数 */
	fputc2LowHigh(1, s);

	/* biBitCount 色ビット数(bit/dot) */
	fputc2LowHigh(c, s);

	/* biCompression 圧縮方式 */
	fputc4LowHigh(0, s);

	/* biSizeImage 圧縮サイズ(byte) */
	fputc4LowHigh(0, s);

	/* biXPelsPerMeter 水平解像度(dot/m) */
	fputc4LowHigh(0, s);

	/* biYPelsPerMeter 垂直解像度(dot/m) */
	fputc4LowHigh(0, s);

	/* biClrUsed 色数 */
	fputc4LowHigh(0, s);

	/* biClrImportant 重要色数 */
	fputc4LowHigh(0, s);

	/* 書出失敗ならエラーでリターン */
	if (ferror(s)) {
		return 0;
	}

	/* 成功でリターン */
	return 1;
}

int main(int argc, char** argv) {
	int i;
	FILE* f;
	int r;
	unsigned char* b;


	//for (i = 60; i < 6000; i=i+10) {
		b = (unsigned char*)malloc(PIC_DATA_SIZE);
		if (b == NULL) {
			return EXIT_FAILURE;
		}
		createPic(b, PIXEL_NUM_X, PIXEL_NUM_Y, atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
		//createPic(b, PIXEL_NUM_X, PIXEL_NUM_Y, i, 0, 0);
		fopen_s(&f, argv[4], "wb");
		//char wk[10];
		//sprintf(wk, "%d", i);
		//fopen_s(&f, wk, "wb");
		if (f == NULL) {
			return EXIT_FAILURE;
		}
		r = putBmpHeader(f, PIXEL_NUM_X, PIXEL_NUM_Y, COLOR_BIT);
		if (!r) {
			fclose(f);
			return EXIT_FAILURE;
		}
		r = fwrite(b, sizeof(unsigned char), PIC_DATA_SIZE, f);
		if (r != PIC_DATA_SIZE) {
			fclose(f);
			return EXIT_FAILURE;
		}
		fclose(f);
		free(b);
	//}
	return 0;
}