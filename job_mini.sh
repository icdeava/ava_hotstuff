#!/bin/bash

echo "Name of TestRun: $1";
#echo "./run.sh new ${1};"

cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED=ON -DHOTSTUFF_PROTO_LOG=ON; make -j4
