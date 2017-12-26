import matplotlib.pyplot as plt
import numpy as np


def html_to_links(fnames):
    links = {}
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
    return links

def links_to_edges(links):
    edges = []
    for key, values in links.items():
        eweight = {}
        for v in values:
            if v in eweight:
                eweight[v] += 1
            else:
                eweight[v] = 1

        for succ, weight in eweight.items():
            edges.append([key, succ, {'weight': weight}])
    return edges

fnames = ['angelinajolie.html','bradpitt.html',
          'jenniferaniston.html','jonvoight.html',
          'martinscorcese.html','robertdeniro.html']
num_fnames = len(fnames)

links = html_to_links(fnames)
edges = links_to_edges(links)

# f2i : fname to index
f2i = dict((fn, i) for i, fn in enumerate(fnames))

def pagerank(fnames, edges):
    NX = len(fnames)
    T = np.matrix(np.zeros((NX, NX)))

    for e in edges:
        T[f2i[e[0]], f2i[e[1]]] = e[2]['weight']

    epsilon = 0.01
    # small scalar matrix so even tiny web still gets some weight
    E = np.ones(T.shape) / NX
    L = T + epsilon * E  # not normalized matrix

    G = np.matrix(np.zeros(L.shape))
    for i in range(0, NX):
        G[i, :] = L[i, :] / np.sum(L[i, :])

    return G

def rand_norm_p(rank):
    p = np.random.random(rank)
    sum_p = np.sum(p)
    return p / sum_p

G = pagerank(fnames, edges)

num_iter = 20

def evol(p, G, num_iter):
    r1 = p.copy()
    # r2 = p.copy()
    G1 = G.copy()
    evol1 = [np.dot(r1, G1**i) for i in range(1, num_iter + 1)]
    # The same as the following:
    # evol2 = []
    # Gi = np.matrix(np.identity(G.shape[0]))
    # for _ in range(1, num_iter + 1):
    #     Gi = np.dot(Gi, G)
    #     r = np.dot(r2, Gi)
    #     evol2.append(r)

    # print evol1
    # print evol2
    return evol1

p = rand_norm_p(num_fnames)
r = p.copy()
r = np.dot(r, G**num_iter)

def plt_mat_g(num_fnames, G, num_iter, p):
    evolution = evol(p, G, num_iter)

    x = np.array(range(1, num_iter + 1))

    table = np.zeros((num_fnames, num_iter))

    for i in range(0, num_fnames):
        for j in range(0, num_iter):
            table[i,j] = evolution[j][0, i] # evolution is a list of 1d matrix

        plt.plot(x, table[i, :], label=fnames[i], lw=2)
        #print table[i,:]

    plt.title('rank vs iterations')
    plt.xlabel('iterations')
    plt.ylabel('rank')
    plt.legend()
    plt.show()

# plot evolution
#plt_mat_g(num_fnames, G, num_iter, p)

def create_revise_dict(fnames):
    revind = {}

    for fn in fnames:
        f = open(fn)
        for line in f.readlines():
            for token in line.split():
                if token in revind:
                    if fn in revind[token]:
                        revind[token][fn] += 1
                    else:
                        revind[token][fn] = 1
                else:
                    revind[token] = {fn:1}

    return revind

revind = create_revise_dict(fnames)

def get_page_rank(fname):
    return r[0, f2i[fname]] # r is a matrix

#print get_page_rank(fnames[0], f2i)

# test final result
check_key = 'film'
if check_key in revind:
    fkeys = revind[check_key].keys()
    sorted_fkeys = sorted(fkeys, key=get_page_rank, reverse=True)

    result = []
    for fn in sorted_fkeys:
        result.append((fn, revind[check_key][fn], get_page_rank(fn)))
    print result