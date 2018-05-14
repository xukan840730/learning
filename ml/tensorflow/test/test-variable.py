import tensorflow as tf
import numpy as np

counter = tf.Variable(0)

one = tf.constant(2)
new_value = tf.add(counter, one)
update = tf.assign(counter, new_value)

init_op = tf.global_variables_initializer()

with tf.Session() as sess:
    sess.run(init_op)
    print(sess.run(counter))

    for _ in range(3):
        sess.run(update)
        print(sess.run(counter))

x = np.ones((1,2,3))
xt = np.transpose(x, (1,0,2))
print(xt.shape)