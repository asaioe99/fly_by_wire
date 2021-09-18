//TMDS 8bit->10bit encode
#include <stdio.h>

bool trmin[9];
bool output[10];

int num_1(bool* a,int m) {
    int i;
    int n=0;
    for (i = 0; i < m; i++) {
        if (a[i] == true) {
            n++;
        }
    }
    return n;
}

void xnorp(bool * input) {
    int i;
    trmin[0] = input[0];
    trmin[8] = false;
    for (i = 1; i < 8; i++) {
        trmin[i] = !(trmin[i - 1] ^ input[i]);
    }
}

void inv() {
    int i;
    for (i = 0; i < 8; i++) {
        output[i] = ~trmin[i];
    }
}
void itbe() {
    int i;
    for (i = 0; i < 8; i++) {
        output[i] = trmin[i];
    }
}

void xorp(bool * input) {
    int i;
    trmin[0] = input[0];
    trmin[8] = true;
    for (i = 1; i < 8; i++) {
        trmin[i] = trmin[i - 1] ^ input[i];
    }
}

int main()
{
    bool input[8] = { 0,  0,  0,  0,  0,  0,  0,  0 };
    //                1   2   4   8  16  32  64 128

    int i;
    int m;
    int t;
    int k;
    int cnt_now;

    for (t = 0; t < 256;t++) {
        k = t;
        for (m = 0; m < 8; m++) {
            if (k%2 == 1) {
                input[m] = true;
            }
            else {
                input[m] = false;
            }
            k = k >> 1;
        }
        printf("%d %d %d %d %d %d %d %d : ",input[7], input[6], input[5], input[4], input[3], input[2], input[1], input[0]);

        double sam = 0;
        int cnt_bfr = 0;

        for (m = 0; m < 1920; m++) {
            int n = 0;
            if (num_1(input, 8) > 4 || (num_1(input, 8) == 4 && input[0] == false)) {
                xnorp(input);
            }
            else {
                xorp(input);
            }

            if (cnt_bfr == 0 || num_1(trmin, 8) == 4) {
                output[9] = ~trmin[8];
                output[8] = trmin[8];
                trmin[8] ? itbe() : inv();
                if (trmin[8] == false) {
                    cnt_now = cnt_bfr - 2 * num_1(trmin, 8) + 8;
                }
                else {
                    cnt_now = cnt_bfr + 2 * num_1(trmin, 8) - 8;
                }
            }
            else {
                if ((cnt_bfr > 0 && num_1(trmin, 8) > 4) || (cnt_bfr < 0 && num_1(trmin, 8) < 4)) {
                    output[9] = true;
                    output[8] = trmin[8];
                    inv();
                    cnt_now = cnt_bfr + 2 * trmin[8] - 2 * num_1(trmin, 8) + 8;
                }
                else {
                    output[9] = false;
                    output[8] = trmin[8];
                    itbe();
                    cnt_now = cnt_bfr - 2 * ~trmin[8] + 2 * num_1(trmin, 8) - 8;
                }
            }

            for (i = 0; i < 10; i++) {
                n = n + (output[i] << i);
            }
            sam = sam + output[9] + output[8] + output[7] + output[6] + output[5] + output[4] + output[3] + output[2] + output[1] + output[0];

            cnt_bfr = cnt_now;
            cnt_now = 0;
        }
        printf("%d : ",t);
        printf("%d %d %d %d %d %d %d %d %d %d : ", output[9], output[8], output[7], output[6], output[5], output[4], output[3], output[2], output[1], output[0]);
        printf("%f\n",sam / 1920);
    }
}
