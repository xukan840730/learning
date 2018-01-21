import matplotlib.pyplot as plt
import numpy as np
from math import log
from nltk import word_tokenize
from nltk.stem.porter import PorterStemmer

fnames = ['angelinajolie.html','bradpitt.html',
          'jenniferaniston.html','jonvoight.html',
          'martinscorcese.html','robertdeniro.html']
num_fnames = len(fnames)

fname_total_words = {}
revind = {}
stemmer = PorterStemmer()

for fn in fnames:
    fname_total_words[fn] = 0

    f = open(fn)
    for line in f.readlines():
        words = word_tokenize(line)
        for token in words:
            wtoken = stemmer.stem(token)
            if wtoken in revind:
                if fn in revind[wtoken]:
                    revind[wtoken][fn] += 1
                else:
                    revind[wtoken][fn] = 1
            else:
                revind[wtoken] = {fn:1}

            fname_total_words[fn] += 1

print(fname_total_words)
print(revind)

# term frequency-inverse document frequency
# term frequency:
#   (total number of times w appear in d) / (the total number of words in d)
# inverse document frequency:
#   log(number of documents in D / (1 + number of documents word w appears in)
def tfidf(revind, fnames, fname_total_words, word):
    print("tfidf for word:%s" % word)
    wstem = stemmer.stem(word)
    if wstem != word:
        print("word '%s' stemmed to '%s'" % (word, wstem))

    t1 = 1.0
    if wstem in revind:
        t1 = 1.0 + len(revind[wstem])
    idf = log(len(fnames) / t1)

    if wstem in revind:
        for fn, count in revind[wstem].items():
            tf = float(count) / float(fname_total_words[fn])
            tf_idf = tf * idf
            print("TF-IDF for '%s', count:%d = %f" % (fn, count, tf_idf))

tfidf(revind, fnames, fname_total_words, 'acting')
tfidf(revind, fnames, fname_total_words, 'awards')
tfidf(revind, fnames, fname_total_words, 'and')

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