#!/bin/bash

#  没有用这种  一直在命令行     clang -g -o xxx xxx.c `pkg-config --cflags  --libs sdl2`   执行 

#  但是这个 设置   c_cpp_properties.json 设置 还是不行  于是用了下面的脚本

clang -g -o player simpleplayer.c `pkg-config --libs --cflags libavutil libavformat libavcodec sdl2`
