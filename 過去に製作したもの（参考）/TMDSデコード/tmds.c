//TMDS 8bit->10bit encode
#include <stdio.h>
#include <stdbool.h>

bool trmin[9];
bool output[10];

// bool配列aからtrueの個数を数える
int count_true(bool *a, int m)
{
    int i;
    int n = 0;
    for (i = 0; i < m; i++)
    {
        if (a[i] == true)
        {
            n++;
        }
    }
    return n;
}

// trmin[0] = input[0]
// trmin[1:8] = trmin[1:8] XNOR input[0:8]
void xnorp(bool *input)
{
    int i;
    trmin[0] = input[0];
    trmin[8] = false;
    for (i = 1; i < 8; i++)
    {
        trmin[i] = !(!trmin[i - 1] != !input[i]);
    }
}

// output[0:8] = !trmin[0:8]
void inv()
{
    int i;
    for (i = 0; i < 8; i++)
    {
        output[i] = !trmin[i];
    }
}

// output[0:8] = trmin[0:8]
void itbe()
{
    int i;
    for (i = 0; i < 8; i++)
    {
        output[i] = trmin[i];
    }
}

// trmin[0] = input[0]
// trmin[1:8] = trmin[1:8] XOR input[0:8]
void xorp(bool *input)
{
    int i;
    trmin[0] = input[0];
    trmin[8] = true;
    for (i = 1; i < 8; i++)
    {
        trmin[i] = !trmin[i - 1] != !input[i];
    }
}

int main()
{
    bool input[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    //                1   2   4   8  16  32  64 128

    int m;
    int t;
    int k;
    int current_count;

    for (t = 0; t < 256; t++)
    {
        k = t;
        for (m = 0; m < 8; m++)
        {
            if (k % 2 == 1)
            {
                input[m] = true;
            }
            else
            {
                input[m] = false;
            }
            k = k >> 1;
        }
        printf("%d %d %d %d %d %d %d %d : ", input[7], input[6], input[5], input[4], input[3], input[2], input[1], input[0]);
        fflush(stdout);
        double sum = 0;
        int previous_count = 0;

        for (m = 0; m < 1920; m++)
        {
            if (count_true(input, 8) > 4 || (count_true(input, 8) == 4 && input[0] == false))
            {
                xnorp(input);
            }
            else
            {
                xorp(input);
            }

            if (previous_count == 0 || count_true(trmin, 8) == 4)
            {
                output[9] = !trmin[8];
                output[8] = trmin[8];
                if (trmin[8])
                {
                    itbe();
                }
                else
                {
                    inv();
                }
                if (trmin[8] == false)
                {
                    current_count = previous_count - 2 * count_true(trmin, 8) + 8;
                }
                else
                {
                    current_count = previous_count + 2 * count_true(trmin, 8) - 8;
                }
            }
            else
            {
                if ((previous_count > 0 && count_true(trmin, 8) > 4) || (previous_count < 0 && count_true(trmin, 8) < 4))
                {
                    output[9] = true;
                    output[8] = trmin[8];
                    inv();
                    current_count = previous_count + 2 * trmin[8] - 2 * count_true(trmin, 8) + 8;
                }
                else
                {
                    output[9] = false;
                    output[8] = trmin[8];
                    itbe();
                    current_count = previous_count - 2 * !trmin[8] + 2 * count_true(trmin, 8) - 8;
                }
            }

            sum = sum + output[9] + output[8] + output[7] + output[6] + output[5] + output[4] + output[3] + output[2] + output[1] + output[0];

            previous_count = current_count;
            current_count = 0;
        }
        printf("%3d : ", t);
        printf("%d %d %d %d %d %d %d %d %d %d : ", output[9], output[8], output[7], output[6], output[5], output[4], output[3], output[2], output[1], output[0]);
        printf("%f\n", sum / 1920);
    }
}
