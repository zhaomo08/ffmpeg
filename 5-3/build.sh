# 使用这个不行   直接在VS 终端 运行任务即可
clang -g -o extra_audio extra_audio.c  `pkg-config --libs --cflags libavutil libavformat libavcoderc`
