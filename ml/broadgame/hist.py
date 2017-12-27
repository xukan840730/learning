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

ip = 0
for pp in players:
    final_score[pp] = 0
    player_index[pp] = ip
    ip += 1

ibg = 0
for bg in broadgames:
    broadgame_index[bg] = ibg
    ibg += 1

score_table = np.zeros((nplayers, ngames + 1))
score_per_broadgame = np.zeros((nbroadgames, nplayers))

ig = 1
for game in games:
    for r in range(0, len(game[1])):
        pp = game[1][r]
        final_score[pp] += rewards[r]
        pp_index = player_index[pp]
        bg_index = broadgame_index[game[0]]
        score_table[pp_index, ig] = final_score[pp]
        score_per_broadgame[bg_index, pp_index] += rewards[r]
    ig += 1

print(final_score)
print(score_table)

print score_per_broadgame

for x in range(0, nplayers):
    plt.plot(score_table[x, :], label=players[x], lw=2)

plt.legend()
plt.show()
