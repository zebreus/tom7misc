#!/bin/sh

make -j train-chess.exe

mkdir chess-leaky
./train-chess.exe chess-leaky LEAKY_RELU 200000

mkdir chess-identity
./train-chess.exe chess-identity IDENTITY 200000

mkdir chess-grad1
./train-chess.exe chess-grad1 GRAD1 200000

mkdir chess-tanh
./train-chess.exe chess-tanh TANH 200000

