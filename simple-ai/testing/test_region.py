import cv2 as cv2
import matplotlib.pyplot as plt


def test1():
    img = cv2.imread('data\single\photo-road.jpg', cv2.IMREAD_COLOR)
    cv2.imshow('image', img)
    cv2.waitKey(0)
    cv2.destoryAllWindows()