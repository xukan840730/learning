import numpy as np
import matplotlib.pyplot as plt

players = ["kan", "lin", "hao", "fang"]
colors = ['r', 'g', 'b', 'y']
broadgames = ["zoo", "catan", "powergrid", "pureto-rico"]
rewards = [30, 0, -10, -20]

games = [
        ("zoo", ("lin", "kan", "fang", "hao")),
        ("zoo", ("hao", "kan", "fang", "lin")),
        ("catan", ("fang", "lin", "kan", "hao")),
        ("powergrid", ("hao", "lin", "kan", "fang")),
        ("powergrid", ("hao", "kan", "fang", "lin")),
        ("catan", ("fang", "hao", "lin", "kan")),
        ("zoo", ("lin", "hao", "fang", "kan")),
        ("zoo", ("fang", "lin", "hao", "kan")),
        ("catan", ("kan", "lin", "hao", "fang"), (30, -5, -5, -20)),
        ("zoo", ("kan", "lin", "hao", "fang")),
        ("pureto-rico", ("fang", "kan", "lin", "hao")),
        ("pureto-rico", ("hao", "lin", "fang", "kan")),
        ("pureto-rico", ("lin", "fang", "hao", "kan")),
        ("pureto-rico", ("kan", "fang", "lin", "hao")),
        ("pureto-rico", ("kan", "lin", "hao", "fang")),
        ("pureto-rico", ("fang", "hao", "lin", "kan")),
        ("pureto-rico", ("kan", "lin", "hao", "fang")),
        ("pureto-rico", ("kan", "lin", "hao", "fang")),
        ("pureto-rico", ("kan", "fang", "hao", "lin")),
         ]

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
    for pp_rank in range(0, len(game[1])):
        pp = game[1][pp_rank]
        score_this_game = rewards[pp_rank]
        if len(game) > 2:
            score_this_game = game[2][pp_rank]
        final_score[pp] += score_this_game
        pp_index = player_index[pp]
        bg_index = broadgame_index[game[0]]
        score_table[pp_index, ig + 1] = final_score[pp]
        score_per_broadgame[bg_index, pp_index] += score_this_game

print(final_score)
print(score_table)

print(score_per_broadgame)

#plt.subplot(2, 2, 1)
for x in range(0, nplayers):
    plt.plot(score_table[x, :], label=players[x], lw=2, color=colors[x])

plt.legend()

fig, ax = plt.subplots()
bg_ind = np.arange(nbroadgames)
width = 0.2       # the width of the bars

#yerr = np.ones((nbroadgames))
rects = []
for ip, pp in enumerate(players):
    r = ax.bar(bg_ind + width * ip, score_per_broadgame[:, ip], width, color=colors[ip])
    rects.append(r)

ax.set_ylabel('Scores')
ax.set_title('Scores by game and player')
ax.set_xticks(bg_ind + width / 2)
ax.set_xticklabels(broadgames)

ax.legend(rects, players)

plt.show()
