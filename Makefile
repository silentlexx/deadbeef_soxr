all:
	gcc -I/usr/include -I/opt/deadbeef/include -std=c99 -shared -O2 -o ddb_soxr_dsp.so  soxr.c -lsoxr -fPIC -Wall -march=native