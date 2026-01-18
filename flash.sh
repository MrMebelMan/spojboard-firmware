#!/usr/bin/env bash

set -e

pio run -e adafruit_matrix_portal_m4 && pio run -e adafruit_matrix_portal_m4 -t upload
