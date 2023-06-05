#!/bin/bash

clang -g -o player player.c `pkg-config --libs --cflags libavutil libavformat libavcodec libswresample sdl2`
