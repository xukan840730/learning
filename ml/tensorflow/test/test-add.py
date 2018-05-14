import tensorflow as tf

a = tf.constant([2])
b = tf.constant([3])

c = tf.add(a, b)

sess = tf.Session()

result = sess.run(c)
print(result)

sess.close()