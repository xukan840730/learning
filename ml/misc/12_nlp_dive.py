from utils import *

from fastai.text.all import *
path = untar_data(URLs.HUMAN_NUMBERS)

#hide
Path.BASE_PATH = path
print(path.ls())

lines = L()
with open(path/'train.txt') as f: lines += L(*f.readlines())
with open(path/'valid.txt') as f: lines += L(*f.readlines())
print(lines)

text = ' . '.join([l.strip() for l in lines])
# print(text[:100])

tokens = text.split(' ')
print(tokens[:10])

L(*tokens)

vocab = L(*tokens).unique()
print(*vocab)

word2idx = {w:i for i, w in enumerate(vocab)}
nums = L(word2idx[i] for i in tokens)
print(nums)

# L((tokens[i:i+3], tokens[i+3]) for i in range(0,len(tokens)-4,3))
seqs = L((tensor(nums[i:i+3]), nums[i+3]) for i in range(0,len(nums)-4,3))
print(seqs)

cut = int(len(seqs) * 0.8)
dls = DataLoaders.from_dsets(seqs[:cut], seqs[cut:], bs=64, shuffle=False, num_workers=0)

# class LMModel1(Module):
#     def __init__(self, vocab_sz, n_hidden):
#         self.i_h = nn.Embedding(vocab_sz, n_hidden)
#         self.h_h = nn.Linear(n_hidden, n_hidden)
#         self.h_o = nn.Linear(n_hidden, vocab_sz)
#
#     def forward(self, x):
#         h = 0 + self.i_h(x[:, 0])
#         h = F.relu(self.h_h(h))
#         h = h + self.i_h(x[:, 1])
#         h = F.relu(self.h_h(h))
#         h = h + self.i_h(x[:, 2])
#         h = F.relu(self.h_h(h))
#         return self.h_o(h)
#
# model1 = LMModel1(len(vocab), 64)
# learn = Learner(dls, model1, loss_func=F.cross_entropy, metrics=accuracy)
# learn.fit_one_cycle(4, 1e-3)

#
# class LMModel2(Module):
#     def __init__(self, vocab_sz, n_hidden):
#         self.i_h = nn.Embedding(vocab_sz, n_hidden)
#         self.h_h = nn.Linear(n_hidden, n_hidden)
#         self.h_o = nn.Linear(n_hidden, vocab_sz)
#
#     def forward(self, x):
#         h = 0
#         for i in range(3):
#             h = h + self.i_h(x[:, i])
#             h = F.relu(self.h_h(h))
#         return self.h_o(h)
#
# model2 = LMModel2(len(vocab), 64)
# learn = Learner(dls, model2, loss_func=F.cross_entropy, metrics=accuracy)
# learn.fit_one_cycle(4, 1e-3)


# Maintaining the State of an RNN
class LMModel3(Module):
    def __init__(self, vocab_sz, n_hidden):
        self.i_h = nn.Embedding(vocab_sz, n_hidden)
        self.h_h = nn.Linear(n_hidden, n_hidden)
        self.h_o = nn.Linear(n_hidden, vocab_sz)
        self.h = 0

    def forward(self, x):
        for i in range(3):
            self.h = self.h + self.i_h(x[:, i])
            self.h = F.relu(self.h_h(self.h))
        out = self.h_o(self.h)
        self.h = self.h.detach()
        return out

    def reset(self): self.h = 0


def group_chunks(ds, bs):
    num_bs = len(ds) // bs
    new_ds = L()

    # group batch_size into one batch
    for i in range(num_bs):
        new_ds += L(ds[i + num_bs*j] for j in range(bs))
        # print(new_ds)
    return new_ds

bs = 64
cut = int(len(seqs) * 0.8)
dls = DataLoaders.from_dsets(
    group_chunks(seqs[:cut], bs),
    group_chunks(seqs[cut:], bs),
    bs=bs, drop_last=True, shuffle=False, num_workers=0)

learn = Learner(dls, LMModel3(len(vocab), 64), loss_func=F.cross_entropy,
                metrics=accuracy, cbs=ModelResetter)
learn.fit_one_cycle(10, 3e-3)

print('done')