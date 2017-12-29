import numpy as np
import matplotlib.pyplot as plt

players = ["kan", "lin", "hao", "fang"]
broadgames = ["zoo", "catan", "powergrid"]
rewards = [30, 0, -10, -20]

games = [("zoo", ("lin", "kan", "fang", "hao")),
        ("zoo", ("hao", "kan", "fang", "lin")),
        ("catan", ("fang", "lin", "kan", "hao")),
        ("powergrid", ("hao", "lin", "kan", "fang")),
        ("powergrid", ("hao", "kan", "fang", "lin")),
        ("catan", ("fang", "hao", "lin", "kan")),
        ("zoo", ("lin", "hao", "fang", "kan"))]

nplayers = len(players)
ngames = len(games)
nbroadgames = len(broadgames)

final_score = {}
player_index = {}
broadgame_index = {}

for ip, pp in enumerate(players):
    final_score[pp] = 0
    player_index[pp] = ip

for ibg, bg in enumerate(broadgames):
    broadgame_index[bg] = ibg

score_table = np.zeros((nplayers, ngames + 1))
score_per_broadgame = np.zeros((nbroadgames, nplayers))

for ig, game in enumerate(games):
    for r in range(0, len(game[1])):
        pp = game[1][r]
        final_score[pp] += rewards[r]
        pp_index = player_index[pp]
        bg_index = broadgame_index[game[0]]
        score_table[pp_index, ig + 1] = final_score[pp]
        score_per_broadgame[bg_index, pp_index] += rewards[r]

print(final_score)
print(score_table)

print score_per_broadgame

for x in range(0, nplayers):
    plt.plot(score_table[x, :], label=players[x], lw=2)

plt.legend()
plt.show()
