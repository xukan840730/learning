from mpl_toolkits.mplot3d import Axes3D
import numpy as np
import matplotlib.pyplot as plt
from sklearn.decomposition import PCA


def read_data():
    with open("mm-motion-vectors-1.txt") as file_in:

        num_dimensions = 63
        data_line = list()
        for line in file_in:
            values_str = line.split()
            value = np.zeros([len(values_str)])

            idx = 0
            for v_str in values_str:
                value[idx] = float(v_str)
                idx += 1

            data_line.append(value)

        result = np.array(data_line)
        return result


def plot_data(data):
    fig = plt.figure()
    ax = fig.add_subplot(111, projection='3d')

    ax.scatter(data[:, 0], data[:, 1], data[:, 2], marker='o')
    ax.set_xlabel('X')
    ax.set_ylabel('Y')
    ax.set_zlabel('Z')

    plt.show()

def plot_data_label(data, label, num_label):
    fig = plt.figure()
    ax = fig.add_subplot(111, projection='3d')

    for l in range(num_label):
        mask = (label == l)
        data_set = data[mask]
        ax.scatter(data_set[:, 0], data_set[:, 1], data_set[:, 2])

    ax.set_xlabel('X')
    ax.set_ylabel('Y')
    ax.set_zlabel('Z')

    plt.show()

def generate_data_small(data):
    num_data_small = data.shape[0] // 8
    data_small = np.zeros([num_data_small, data.shape[1]])
    step = data.shape[0] // num_data_small

    for i in range(num_data_small):
        idx = i * step
        data_small[i, :] = data[idx, :]

    return data_small

def cluster_data(data, num_clusters):
    from sklearn.cluster import KMeans
    kmeans = KMeans(n_clusters=num_clusters, random_state=0).fit(data)
    return kmeans

data_t = read_data()

pca = PCA(n_components=3)
pca.fit(data_t)

X_pca = pca.transform(data_t)
print(X_pca.shape)

# plot_data(data_t)

X_pca_small = generate_data_small(X_pca)
X_pca_small = X_pca
# print(data_small)

num_clusters = 24
kmeans = cluster_data(X_pca_small, num_clusters)
# print(X_pca_small.shape)
# print(kmeans.labels_.shape)

plot_data_label(X_pca_small, kmeans.labels_, num_clusters)