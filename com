#!/bin/bash
#compile all 3 files
cc -o mroot mroot.c util.c mkdircreat.c