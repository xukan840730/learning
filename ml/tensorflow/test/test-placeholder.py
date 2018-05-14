import tensorflow as tf

a = tf.placeholder(tf.float32)
b = tf.multiply(a, 2)

with tf.Session() as sess:
    # result = sess.run(b, feed_dict={a:3.5})
    result = sess.run(b)
    print(result)