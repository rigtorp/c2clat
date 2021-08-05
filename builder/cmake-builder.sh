#!/usr/bin/env bash

mkdir -p build

cmake -S . -B build "$*"

cmake --build build
