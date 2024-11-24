#!/bin/bash

subds=$(gdalinfo $1 | grep -E "SUBDATASET_[0-9]+_NAME" | awk '{split($0, a, "="); print a[2]}') #| grep -E "MSIL2A.xml:[0-9]+m")

TARGET_BANDS="B02_10m B03_10m B04_10m B05_20m B06_20m B07_20m B08_10m B8A_20m B11_20m B12_20m MSK_CLDPRB_20m.jp2 MSK_SNWPRB_20m.jp2"

files=""
for ds in $subds
do
    files="$files $(gdalinfo $ds | grep ".jp2" | grep -E "$(echo $TARGET_BANDS | sed 's/ /|/g')")"
done


unsortedFiles=$files
files=""

for band in $TARGET_BANDS
do
    files="$files $(echo $unsortedFiles | sed 's/ /\n/g' | grep -E "$band" | head -1)"
done

fname=$(echo $1 | sed "s/.zip/.vrt/g")
gdalbuildvrt -resolution user -tr 10 10 -overwrite -separate $fname $files
gdalwarp -t_srs EPSG:4326 -overwrite $fname $(echo $fname | sed "s/.vrt/-EPSG4326.vrt/g")
