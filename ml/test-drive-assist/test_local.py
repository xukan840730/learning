import cv2
import debug as dbg
import process

# def canny(image):
#     gray = cv2.cvtColor(image, cv2.COLOR_RGB2GRAY)
#     blur = cv2.GaussianBlur(gray, (5, 5), 0)
#     # canny = cv2.Canny(blur, 50, 150) ;; original value
#     canny = cv2.Canny(blur, 20, 40)
#     # sobel = cv2.Sobel(blur, cv2.CV_64F, 1, 0, ksize=5)
#     return canny

image_u8 = cv2.imread('test_image_tiny.jpg')
processed_u8 = process.process_image2(image_u8)

frame_width = image_u8.shape[0]
frame_height = image_u8.shape[1]

frame_u8 = cv2.addWeighted(image_u8, 1.0, processed_u8, 1.0, 0.0)
cv2.imshow('Frame', frame_u8)
cv2.waitKey(0)

# do a bit of cleanup
cv2.destroyAllWindows()

# for iregion in expand_regions:
#     dbg.debug_expansion(image_gray, iregion, 0, 0, 1.0)

# dbg.debug_expansion(image_gray, skipped_region, 0.0, 1.0, 0)