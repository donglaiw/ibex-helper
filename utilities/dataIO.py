import os
import h5py
import numpy as np
from ibex.data_structures import meta_data, swc
from ibex.utilities.constants import *
from PIL import Image
import scipy.misc


def GetWorldBBox(prefix):
    # return the bounding box for this segment
    return meta_data.MetaData(prefix).WorldBBox()



def ReadMetaData(prefix):
    # return the meta data for this prefix
    return meta_data.MetaData(prefix)



def Resolution(prefix):
    # return the resolution for this prefix
    return meta_data.MetaData(prefix).Resolution()



def ReadH5File(filename, dataset=None):
    # read the h5py file
    with h5py.File(filename, 'r') as hf:
        # read the first dataset if none given
        if dataset == None: data = np.array(hf[hf.keys()[0]])
        else: data = np.array(hf[dataset])

    # return the data
    return data



def IsIsotropic(prefix):
    resolution = Resolution(prefix)
    return (resolution[IB_Z] == resolution[IB_Y]) and (resolution[IB_Z] == resolution[IB_X])


def WriteH5File(data, filename, dataset):
    with h5py.File(filename, 'w') as hf:
        hf.create_dataset(dataset, data=data)



def ReadSegmentationData(prefix):
    filename, dataset = meta_data.MetaData(prefix).SegmentationFilename()

    return ReadH5File(filename, dataset).astype(np.int64)



def ReadGoldData(prefix):
    filename, dataset = meta_data.MetaData(prefix).GoldFilename()

    return ReadH5File(filename, dataset).astype(np.int64)



def ReadImageData(prefix):
    filename, dataset = meta_data.MetaData(prefix).ImageFilename()

    return ReadH5File(filename, dataset)



def ReadSkeletons(prefix, data):
    # read in all of the skeletons
    skeletons = []
    joints = []
    endpoints = []

    max_label = np.amax(data) + 1
    for label in range(max_label):
        # read the skeleton
        skeleton = swc.Skeleton(prefix, label)

        skeletons.append(skeleton)

        # add all joints for this skeleton
        for ij in range(skeleton.NJoints()):
            joints.append(skeleton.Joint(ij))
        # add all endpoints for this skeleton
        for ip in range(skeleton.NEndPoints()):
            endpoints.append(skeleton.EndPoint(ip))

    # return all of the skeletons
    return skeletons, joints, endpoints



def ReadImage(filename):
    image = np.array(Image.open(filename)) / 255.0

    return image


def WriteImage(filename, image):
    scipy.misc.imsave(filename, image)