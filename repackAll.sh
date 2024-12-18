#!/bin/bash

for file in $(find $1 -name "*.zip" -type f)
do
    echo $file
    ./sentinelRepack.sh $file
    echo -e "\n\n\n"
done
