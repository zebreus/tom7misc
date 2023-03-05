#!/bin/bash

ROUNDS=200000
#
# make -j train-mnist.exe && ./train-mnist.exe mnist-leaky/ LEAKY_RELU ${ROUNDS}
# make -j train-mnist.exe && ./train-mnist.exe mnist-grad1/ GRAD1 ${ROUNDS}
# make -j train-mnist.exe && ./train-mnist.exe mnist-identity/ IDENTITY ${ROUNDS}
# make -j train-mnist.exe && ./train-mnist.exe mnist-tanh/ TANH ${ROUNDS}
# make -j train-mnist.exe && ./train-mnist.exe mnist-downshift2/ DOWNSHIFT2 ${ROUNDS}
# make -j train-mnist.exe && ./train-mnist.exe mnist-sigmoid/ SIGMOID ${ROUNDS}
#

make -j train-mnist.exe && ./train-mnist.exe mnist-plus64/ PLUS64 ${ROUNDS}
