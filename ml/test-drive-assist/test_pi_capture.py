import time
import datetime
import os
import picamera


curtime = datetime.datetime.now()
folder_name = "./capture-{0:d}-{1:d}-{2:d}-{3:d}-{4:d}".format(curtime.year, curtime.month, curtime.day, curtime.hour, curtime.minute)

if not os.path.exists(folder_name):
    os.mkdir(folder_name)

os.chdir(folder_name)

with picamera.PiCamera() as camera:
    camera.start_preview()
    try:
        for i, filename in enumerate(camera.capture_continuous('image{counter:04d}.jpg')):
            print(filename)
            time.sleep(1)
            if i == 59:
                break

    finally:
        camera.stop_preview()
