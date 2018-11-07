cimport cython
cimport numpy as np

import os
import numpy as np
import ctypes
import struct
import sys


from ibex.graphs.biological.util import CreateDirectoryStructure, ExtractExample, FindSmallSegments, GenerateExamplesArray, ScaleFeature
from ibex.graphs.biological import edge_generation
from ibex.utilities import dataIO
from ibex.utilities.constants import *



cdef extern from 'cpp-node-generation.h':
    void CppFindMiddleBoundaries(long *segmentation, long grid_size[3])
    void CppGetMiddleBoundaryLocation(long label_one, long label_two, float &zpoint, float &ypoint, float &xpoint)



def FindMiddleBoundaries(segmentation):
    # everything needs to be long ints to work with c++
    assert (segmentation.dtype == np.int64)

    cdef np.ndarray[long, ndim=3, mode='c'] cpp_segmentation = np.ascontiguousarray(segmentation, dtype=ctypes.c_int64)
    cdef np.ndarray[long, ndim=1, mode='c'] cpp_grid_size = np.ascontiguousarray(segmentation.shape, dtype=ctypes.c_int64)

    CppFindMiddleBoundaries(&(cpp_segmentation[0,0,0]), &(cpp_grid_size[0]))

    # free memory
    del cpp_segmentation
    del cpp_grid_size




def GetMiddleBoundary(label_one, label_two):
    cpp_label_one = min(label_one, label_two)
    cpp_label_two = max(label_one, label_two)

    # the center point on the boundary sent to cython
    cdef np.ndarray[float, ndim=1, mode='c'] cpp_point = np.zeros(3, dtype=ctypes.c_float)

    CppGetMiddleBoundaryLocation(label_one, label_two, cpp_point[0], cpp_point[1], cpp_point[2])
    
    return (int(cpp_point[IB_Z]), int(cpp_point[IB_Y]), int(cpp_point[IB_X]))




def GenerateNodes(prefix, segmentation, seg2gold_mapping, subset, network_radius=400, threshold=20000):
    # possible widths for the neural network
    widths = [(18, 52, 52), (20, 60, 60), (22, 68, 68), (24, 76, 76)]
    
    # create the directory structure to save the features in
    # forward is needed for training and validation data that is cropped
    CreateDirectoryStructure(widths, network_radius, ['training', 'validation', 'testing', 'forward'], 'nodes')

    # get the complete adjacency graph shows all neighboring edges
    adjacency_graph = edge_generation.ExtractAdjacencyMatrix(segmentation)

    # get the list of nodes over and under the threshold
    small_segments, large_segments = FindSmallSegments(segmentation, threshold)

    # get the locations around a possible merge
    FindMiddleBoundaries(segmentation)

    # get the size of the data
    zres, yres, xres = segmentation.shape

    # make sure the subset is one of three categories
    assert (subset == 'training' or subset == 'validation' or subset == 'testing')

    # crop the subset if it overlaps with testing data
    ((cropped_zmin, cropped_zmax), (cropped_ymin, cropped_ymax), (cropped_xmin, cropped_xmax)) = dataIO.CroppingBox(prefix)

    # create list for all relevant examples
    positive_examples = []
    negative_examples = []
    unknown_examples = []
    forward_positive_examples = []
    forward_negative_examples = []
    forward_unknown_examples = []

    for iv, (label_one, label_two) in enumerate(adjacency_graph):
        if (label_one in small_segments) ^ (label_two in small_segments):
            zpoint, ypoint, xpoint = GetMiddleBoundary(label_one, label_two)

            # if the center of the point falls outside the cropped box do not include it in training or validation 
            forward = False
            # however, you allow it for forward inference
            if (zpoint < cropped_zmin or cropped_zmax <= zpoint): forward = True
            if (ypoint < cropped_ymin or cropped_ymax <= ypoint): forward = True
            if (xpoint < cropped_xmin or cropped_xmax <= xpoint): forward = True

            # see if these two segments belong to the same node
            gold_one = seg2gold_mapping[label_one]
            gold_two = seg2gold_mapping[label_two]
                
            # create lists of locations where these point occur
            if forward:
                if gold_one < 1 or gold_two < 1: 
                    forward_unknown_examples.append((zpoint, ypoint, xpoint, label_one, label_two))
                elif gold_one == gold_two:
                    forward_positive_examples.append((zpoint, ypoint, xpoint, label_one, label_two))
                else: 
                    forward_negative_examples.append((zpoint, ypoint, xpoint, label_one, label_two))
            else:
                if gold_one < 1 or gold_two < 1: 
                    unknown_examples.append((zpoint, ypoint, xpoint, label_one, label_two))
                elif gold_one == gold_two:
                    positive_examples.append((zpoint, ypoint, xpoint, label_one, label_two))
                else: 
                    negative_examples.append((zpoint, ypoint, xpoint, label_one, label_two))


    for width in widths:
        parent_directory = 'features/biological/nodes-{}nm-{}x{}x{}'.format(network_radius, width[IB_Z], width[IB_Y], width[IB_X])

        if len(positive_examples):
            # save the examples
            positive_filename = '{}/{}/positives/{}.examples'.format(parent_directory, subset, prefix)
            with open(positive_filename, 'wb') as fd:
                fd.write(struct.pack('q', len(positive_examples)))
                for example in positive_examples:
                    fd.write(struct.pack('qq', example[3], example[4]))

            positive_examples_array = GenerateExamplesArray(prefix, segmentation, positive_examples, width, network_radius)
            dataIO.WriteH5File(positive_examples_array, '{}/{}/positives/{}-examples.h5'.format(parent_directory, subset, prefix), 'main', compression=True)
            del positive_examples_array

        if len(negative_examples):
            # save the examples
            negative_filename = '{}/{}/negatives/{}.examples'.format(parent_directory, subset, prefix)
            with open(negative_filename, 'wb') as fd:
                fd.write(struct.pack('q', len(negative_examples)))
                for example in negative_examples:
                    fd.write(struct.pack('qq', example[3], example[4]))

            negative_examples_array = GenerateExamplesArray(prefix, segmentation, negative_examples, width, network_radius)
            dataIO.WriteH5File(negative_examples_array, '{}/{}/negatives/{}-examples.h5'.format(parent_directory, subset, prefix), 'main', compression=True)
            del negative_examples_array

        if len(unknown_examples):
            # save the examples
            unknown_filename = '{}/{}/unknowns/{}.examples'.format(parent_directory, subset, prefix)
            with open(unknown_filename, 'wb') as fd:
                fd.write(struct.pack('q', len(unknown_examples)))
                for example in unknown_examples:
                    fd.write(struct.pack('qq', example[3], example[4]))

            unknown_examples_array = GenerateExamplesArray(prefix, segmentation, unknown_examples, width, network_radius)
            dataIO.WriteH5File(unknown_examples_array, '{}/{}/unknowns/{}-examples.h5'.format(parent_directory, subset, prefix), 'main', compression=True)
            del unknown_examples_array

        if len(forward_positive_examples):
            # save the examples
            forward_positive_filename = '{}/forward/positives/{}.examples'.format(parent_directory, prefix)
            with open(forward_positive_filename, 'wb') as fd:
                fd.write(struct.pack('q', len(forward_positive_examples)))
                for example in forward_positive_examples:
                    fd.write(struct.pack('qq', example[3], example[4]))
            
            forward_positive_examples_array = GenerateExamplesArray(prefix, segmentation, forward_positive_examples, width, network_radius)
            dataIO.WriteH5File(forward_positive_examples_array, '{}/forward/positives/{}-examples.h5'.format(parent_directory, prefix), 'main', compression=True)
            del forward_positive_examples_array            

        if len(forward_negative_examples):
            # save the examples
            forward_negative_filename = '{}/forward/negatives/{}.examples'.format(parent_directory, prefix)
            with open(forward_negative_filename, 'wb') as fd:
                fd.write(struct.pack('q', len(forward_negative_examples)))
                for example in forward_negative_examples:
                    fd.write(struct.pack('qq', example[3], example[4]))

            forward_negative_examples_array = GenerateExamplesArray(prefix, segmentation, forward_negative_examples, width, network_radius)
            dataIO.WriteH5File(forward_negative_examples_array, '{}/forward/negatives/{}-examples.h5'.format(parent_directory, prefix), 'main', compression=True)
            del forward_negative_examples_array

        if len(forward_unknown_examples):
            # save the examples
            forward_unknown_filename = '{}/forward/unknowns/{}.examples'.format(parent_directory, prefix)
            with open(forward_unknown_filename, 'wb') as fd:
                fd.write(struct.pack('q', len(forward_unknown_examples)))
                for example in forward_unknown_examples:
                    fd.write(struct.pack('qq', example[3], example[4]))

            forward_unknown_examples_array = GenerateExamplesArray(prefix, segmentation, forward_unknown_examples, width, network_radius)
            dataIO.WriteH5File(forward_unknown_examples_array, '{}/forward/unknowns/{}-examples.h5'.format(parent_directory, prefix), 'main', compression=True)
            del forward_unknown_examples_array