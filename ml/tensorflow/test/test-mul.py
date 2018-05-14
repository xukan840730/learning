import tensorflow as tf

m1 = tf.constant([[2,3], [3,4]])
m2 = tf.constant([[2,3], [3,4]])

ops = tf.matmul(m1, m2)

with tf.Session() as sess:
    result = sess.run(ops)
    print(result)
