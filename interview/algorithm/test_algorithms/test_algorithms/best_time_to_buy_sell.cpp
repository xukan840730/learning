#include <stdio.h>
#include <assert.h>


static int solution1(const int* prices, int n)
{
    int i_buy = 0;
    int i_sell = 0;

    for (int i = 1; i < n; i++)
    {
        if (prices[i] > prices[i_sell])
        {
            i_sell = i;
        }
        else if (prices[i] < prices[i_buy])
        {
            i_buy = i;
            i_sell = i;
        }
    }

    return prices[i_sell] - prices[i_buy];
}

struct DpRes
{
    int i_buy;
    int i_sell;
    
    int GetProfile(const int* prices, int n) const
    {
        assert(i_buy >= 0 && i_buy < n);
        assert(i_sell >= 0 && i_sell < n);
        assert(i_buy <= i_sell);
        return prices[i_sell] - prices[i_buy];
    }
};

static DpRes DP(const int* prices, int n, int i, int j)
{
    assert(i >= 0 && i < n);
    assert(j >= 0 && j < n);

    if (i == j)
    {
        DpRes dpRes;
        dpRes.i_buy = i;
        dpRes.i_sell = j;
        return dpRes;
    }
    else if (i + 1 == j)
    {
        DpRes dpRes;
        if (prices[i] < prices[j])
        {
            dpRes.i_buy = i;
            dpRes.i_sell = j;
        }
        else
        {
            dpRes.i_buy = j;
            dpRes.i_sell = j;
        }
    }

    const DpRes dp_i_j_minus_1 = DP(prices, n, i, j - 1);
    DpRes candidate1 = dp_i_j_minus_1;
    if (prices[j] > prices[dp_i_j_minus_1.i_sell])
        candidate1.i_sell = j;

    const DpRes dp_i_plus_1_j = DP(prices, n, i + 1, j);
    DpRes candidate2 = dp_i_plus_1_j;
    if (prices[i] < prices[dp_i_plus_1_j.i_buy])
        candidate2.i_buy = i;

    const DpRes best = candidate1.GetProfile(prices, n) > candidate2.GetProfile(prices, n) ? candidate1 : candidate2;
    return best;
}

int solution3(const int* prices, int n) {
    int minprice = 0x7FFFFFFF;
    int maxprofit = 0;
    for (int i = 0; i < n; i++) {
        if (prices[i] < minprice)
            minprice = prices[i];
        else if (prices[i] - minprice > maxprofit)
            maxprofit = prices[i] - minprice;
    }
    return maxprofit;
}

static int maxProfit(const int* prices, int n) {
    //return solution1(prices, n);
    //return DP(prices, n, 0, n - 1).GetProfile(prices, n);
    return solution3(prices, n);
}

void test_max_profit()
{
    int seq[] = { 7,1,5,3,6,4,1 };
    //int seq[] = { 7,2,5,3,6,4,1  };
    //int seq[] = { 2,1,3 };
    //int seq[] = { 1, 6 };
    int nseq = sizeof(seq) / sizeof(seq[0]);

    int max_profit = maxProfit(seq, nseq);
    printf("max_profit: %d\n", max_profit);
}