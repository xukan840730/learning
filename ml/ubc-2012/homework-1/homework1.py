import matplotlib.pyplot as plt


links = {}
fnames = ['angelinajolie.html','bradpitt.html',
          'jenniferaniston.html','jonvoight.html',
          'martinscorcese.html','robertdeniro.html']

for file in fnames:
    links[file] = []
    f = open(file)
    for line in f.readlines():
        while True:
            temp = line.partition('<a href="http://')
            p = temp[2]
            if p == '':
                break
            url, _, line = p.partition('\">')
            links[file].append(url)

    f.close()

import networkx as nx
DG = nx.DiGraph()
DG.add_nodes_from(fnames)
edges = []
for key, values in links.items():
    eweight = {}
    for v in values:
        if v in eweight:
            eweight[v] += 1
        else:
            eweight[v] = 1

    for succ, weight in eweight.items():
        edges.append([key, succ, {'weight':weight}])

DG.add_edges_from(edges)

plt.figure(figsize=(9,9))
pos=nx.spring_layout(DG,iterations=10)
nx.draw(DG,pos,alpha=0.4,edge_color='r',font_size=16,with_labels=True)
plt.savefig('link_graph.png')
plt.show()

#import cPickle as pickle
#pickle.dump(DG, open('DG.pkl', 'w'))

import numpy as np
NX = len(fnames)
T = np.matrix(np.zeros((NX, NX)))

f2i = dict((fn, i) for i, fn in enumerate(fnames))

for pred, succ in DG.adj.items():
    for s, edata in succ.items():
        T[f2i[pred], f2i[s]] = edata['weight']

E = np.ones(T.shape) / NX
epsilon = 0.01
L = T + epsilon * E
G = np.matrix(np.zeros(L.shape))
for i in range(0, NX):
    G[i,:] = L[i,:] / np.sum(L[i,:])

print(G)