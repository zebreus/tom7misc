#!/bin/sh

make -j train-chess.exe

mkdir chess-leaky
./train-chess.exe chess-leaky LEAKY_RELU 200000

