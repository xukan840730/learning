#include <stdio.h>

int DP(const int* coins, int n, int remainingAmount, int num_prev)
{
    int min_num = 0x7FFFFFFF;
    for (int i = 0; i < n; i++)
    {
        if (coins[i] > remainingAmount)
            continue;

        int num_coins;
        if (coins[i] == remainingAmount)
        {
            num_coins = num_prev + 1;
        }
        else
        {
            int dp_prev = DP(coins, n, remainingAmount - coins[i], num_prev + 1);
            if (dp_prev == 0x7FFFFFFF)
                continue;

            num_coins = dp_prev;
        }

        if (num_coins < min_num)
        {
            min_num = num_coins;
        }
    }

    return min_num;
}

int coinChange(const int* coins, int n, int amount) 
{
    if (amount == 0)
        return 0;

    int v = DP(coins, n, amount, 0);
    return v == 0x7FFFFFFF ? -1 : v;
}

void test_coin_change()
{
    int coins[] = { 1, 2, 5 };
    int ncoins = sizeof(coins) / sizeof(coins[0]);

    int amount = 23;
    int v = coinChange(coins, ncoins, amount);

    printf("v: %d\n", v);
}