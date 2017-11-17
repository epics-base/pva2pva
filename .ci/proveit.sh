#!/bin/sh
set -e

if ! prove -e cat -f "$1"
then
  cat "$1"
  exit 1
fi
