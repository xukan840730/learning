import matplotlib.pyplot as plt
import numpy as np
from nltk import word_tokenize

fnames = ['angelinajolie.html','bradpitt.html',
          'jenniferaniston.html','jonvoight.html',
          'martinscorcese.html','robertdeniro.html']
num_fnames = len(fnames)

x = "This (sentence) has punctuation."
word_tokenize(x)

# def create_revise_dict(fnames):
#     revind = {}
#
#     for fn in fnames:
#         f = open(fn)
#         for line in f.readlines():
#             for token in word_tokenize(line):
#                 if token in revind:
#                     if fn in revind[token]:
#                         revind[token][fn] += 1
#                     else:
#                         revind[token][fn] = 1
#                 else:
#                     revind[token] = {fn:1}
#
#     return revind
#
# revind = create_revise_dict(fnames)

# def get_page_rank(fname):
#     return r[0, f2i[fname]] # r is a matrix
#
# #print get_page_rank(fnames[0], f2i)
#
# # test final result
# check_key = 'film'
# if check_key in revind:
#     fkeys = revind[check_key].keys()
#     sorted_fkeys = sorted(fkeys, key=get_page_rank, reverse=True)
#
#     result = []
#     for fn in sorted_fkeys:
#         result.append((fn, revind[check_key][fn], get_page_rank(fn)))
#     for r in result:
#         print(r)