#!/bin/bash

clang -g -o player2 simpleplayer2.c `pkg-config --libs --cflags libavutil libavformat libavcodec libswresample sdl2`
