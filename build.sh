#!/bin/bash

BUILD=build

[[ ! -d ${BUILD} ]] && mkdir ${BUILD}

clang src/client.c src/log.c src/draw.c -o ${BUILD}/rs-client -std=gnu2x -g -lraylib -lm -O0
clang src/server.c src/log.c            -o ${BUILD}/rs-server -std=gnu2x -g -lraylib -lm -O0
