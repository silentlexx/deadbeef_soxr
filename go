#!/bin/bash

make && cat ddb_soxr_dsp.so > ~/.local/lib/deadbeef/ddb_soxr_dsp.so && echo "Copy ddb_soxr_dsp.so to ~/.local/lib/deadbeef/" && echo "Done!" && rm ddb_soxr_dsp.so