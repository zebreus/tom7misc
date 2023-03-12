#!/bin/sh

make -j train-chess.exe

ROUNDS=500000

# ./train-chess.exe chess-leaky LEAKY_RELU ${ROUNDS}
# ./train-chess.exe chess-identity IDENTITY ${ROUNDS}
# ./train-chess.exe chess-grad1 GRAD1 ${ROUNDS}
# ./train-chess.exe chess-tanh TANH ${ROUNDS}
# ./train-chess.exe chess-sigmoid SIGMOID ${ROUNDS}
# ./train-chess.exe chess-downshift2 DOWNSHIFT2 ${ROUNDS}

./train-chess.exe chess-plus64 PLUS64 ${ROUNDS}
