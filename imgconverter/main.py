import cv2 as cv
import numpy as np
import sys

colors = np.zeros((2 ** 6, 3))


def genColorMap():
    for val in range(2 ** 6):
        b = '{:6b}'.format(val)  # msb to lsb
        for i in [0, 1, 2]:  # blue is msb, then green, then red
            if b[0] == '1':  # high bit
                colors[val, i] += 170
            if b[1] == '1':  # low bit
                colors[val, i] += 85
            b = b[2:]


# use the squared distance between the image color and the color list to find the closest match
def getColorMatch(color: np.ndarray((3,))):
    difs = colors - color
    difs = difs ** 2
    dists = np.sum(difs, 1)
    return np.argmin(dists)


def main():
    if len(sys.argv) < 2:
        print('Usage: {} filename'.format(sys.argv[0]))
        return -1

    genColorMap()

    nameIn: str = sys.argv[1]
    imgName = ''.join(nameIn.split('.')[0:-1])  # get the name of the file itself

    img = cv.imread(nameIn)
    img = cv.resize(img, (80, 60), interpolation=cv.INTER_AREA)
    with open(imgName + '.c', 'w') as outf:
        outf.write('const char {}[4800] = {{\n'.format(imgName))
        for x in range(60):
            for y in range(80):
                outf.write('{}'.format(getColorMatch(img[x, y])))
                if x != 59 or y != 79:
                    outf.write(', ')
            outf.write('\n')
        outf.write('};\n')


if __name__ == '__main__':
    main()
