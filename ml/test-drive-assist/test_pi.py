# USAGE
# python test2.py
# python test2.py --display 1

# import the necessary packages
from __future__ import print_function
from pivideostream import PiVideoStream
from imutils.video import FPS
from multiprocessing import Process
from multiprocessing import Queue
from picamera.array import PiRGBArray
from picamera import PiCamera
import argparse
import time
import cv2
import process

# construct the argument parse and parse the arguments
ap = argparse.ArgumentParser()
ap.add_argument("-n", "--num-frames", type=int, default=100,
	help="# of frames to loop over for FPS test")
ap.add_argument("-d", "--display", type=int, default=-1,
	help="Whether or not frames should be displayed")
args = vars(ap.parse_args())

def capture1():
	# initialize the camera and stream
	camera = PiCamera()
	camera.resolution = (320, 240)
	camera.framerate = 32
	rawCapture = PiRGBArray(camera, size=(320, 240))
	stream = camera.capture_continuous(rawCapture, format="bgr",
									   use_video_port=True)

	# allow the camera to warmup and start the FPS counter
	print("[INFO] sampling frames from `picamera` module...")
	time.sleep(2.0)
	fps = FPS().start()

	# loop over some frames
	for (i, f) in enumerate(stream):
		# grab the frame from the stream and resize it to have a maximum
		# width of 400 pixels
		frame = f.array
		# frame = imutils.resize(frame, width=400)

		# check to see if the frame should be displayed to our screen
		if args["display"] > 0:
			cv2.imshow("Frame", frame)
			# key = cv2.waitKey(1) & 0xFF

		# clear the stream in preparation for the next frame and update
		# the FPS counter
		rawCapture.truncate(0)
		fps.update()

		# check to see if the desired number of frames have been reached
		if i == args["num_frames"]:
			break

	# stop the timer and display FPS information
	fps.stop()
	print("[INFO] elasped time: {:.2f}".format(fps.elapsed()))
	print("[INFO] approx. FPS: {:.2f}".format(fps.fps()))

	# do a bit of cleanup
	cv2.destroyAllWindows()
	stream.close()
	rawCapture.close()
	camera.close()

def classify_frame(inputQueue, outputQueue):
	# keep looping
	while True:
		# check to see if there is a frame in our input queue
		if not inputQueue.empty():
			# grab the frame from the input queue
			frame = inputQueue.get()
			processed_frame = process.process_image(frame)

			# write the detections to the output queue
			outputQueue.put(processed_frame)

# initialize the input queue (frames), output queue (detections),
# and the list of actual detections returned by the child process
inputQueue = Queue(maxsize=1)
outputQueue = Queue(maxsize=1)
processed = None

# construct a child process *indepedent* from our main process of
# execution
print("[INFO] starting process...")
p = Process(target=classify_frame, args=(inputQueue, outputQueue,))
p.daemon = True
p.start()

# created a *threaded *video stream, allow the camera sensor to warmup,
# and start the FPS counter
print("[INFO] sampling THREADED frames from `picamera` module...")
vs = PiVideoStream().start()
time.sleep(2.0)
fps = FPS().start()

# loop over some frames...this time using the threaded stream
while True:
	# grab the frame from the threaded video stream and resize it
	# to have a maximum width of 400 pixels
	frame_u8 = vs.read()
	frame_width = frame_u8.shape[0]
	frame_height = frame_u8.shape[1]

	# if the input queue *is* empty, give the current frame to classify
	if inputQueue.empty():
		inputQueue.put(frame_u8)

	# if the output queue *is not* empty, grab the detections
	if not outputQueue.empty():
		processed_u8 = outputQueue.get()

	# frame = imutils.resize(frame, width=400)

	# check to see if our detectios are not None (and if so, we'll
	# draw the detections on the frame)
	if processed_u8 is not None:
		frame_u8 = cv2.addWeighted(frame_u8, 1.0, processed_u8, 1.0, 0.0)

	# check to see if the frame should be displayed to our screen
	winname = "Frame"
	cv2.namedWindow(winname)
	cv2.moveWindow(winname, (480 - frame_width) // 2, (480 - frame_height) // 2)
	cv2.imshow(winname, frame_u8)
	key = cv2.waitKey(1)
	if key > 0:
		break

	# update the FPS counter
	fps.update()

# stop the timer and display FPS information
fps.stop()
print("[INFO] elasped time: {:.2f}".format(fps.elapsed()))
print("[INFO] approx. FPS: {:.2f}".format(fps.fps()))

# do a bit of cleanup
cv2.destroyAllWindows()
vs.stop()
