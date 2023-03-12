#!/bin/bash

ROUNDS=250000

# make -j train-cifar10.exe && ./train-cifar10.exe cifar10-downshift2/ DOWNSHIFT2 250000
# make -j train-cifar10.exe && ./train-cifar10.exe cifar10-grad1/ GRAD1 250000
# make -j train-cifar10.exe && ./train-cifar10.exe cifar10-identity/ IDENTITY 250000
# make -j train-cifar10.exe && ./train-cifar10.exe cifar10-leaky/ LEAKY_RELU 250000
# make -j train-cifar10.exe && ./train-cifar10.exe cifar10-sigmoid/ SIGMOID 250000
# make -j train-cifar10.exe && ./train-cifar10.exe cifar10-tanh/ TANH 250000

make -j train-cifar10.exe && ./train-cifar10.exe cifar10-plus64/ PLUS64 250000
