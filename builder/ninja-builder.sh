#!/usr/bin/env bash

mkdir -p build

cmake -S . -B build -G Ninja "$*"

ninja -C build
