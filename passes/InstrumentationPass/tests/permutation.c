#include <stdio.h>
#include <stdlib.h>

void report_one(char *a, int max)
{
    int i;
    for (i = 0; i < max; i++)
        printf("%c ", a[i] + '0');
    printf("\n");
}

int in_prefix(int i, char *a, int n)
{
    int found = 0, j;
    for (j = 0; j < n; j++)
        if (a[j] == i) return 1;
    return 0;
}

int atoi_(char *s, int *val)
{
    int i;
    if (s != 0) {
        *val = 0;
        for (; *s; s++)
            *val = *val * 10 + *s - '0';
        return 1;
    } else {
        printf("Invalid Input!\n");
        return 0;
    }
}

void permute(char *a, int n, int max);

void permute_next_pos(char *a, int n, int max)
{
    int i ;
    for (i = 0; i < max; i++) {
        if (!in_prefix(i, a, n)) {
            a[n] = i;
            permute(a, n + 1, max);
        }
    }
}

void permute(char *a, int n, int max)
{
    if (n == max) report_one(a, max);
    else permute_next_pos(a, n, max);
}

int main(int argc, char **argv)
{
    int max;
    char *a;
    atoi_(argv[1], &max);
    a = (char *)malloc(max);
    permute(a, 0, max);
}
