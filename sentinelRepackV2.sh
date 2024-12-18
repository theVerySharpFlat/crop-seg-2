#!/bin/bash

subds=$(gdalinfo $1 | grep -E "SUBDATASET_[0-9]+_NAME" | awk '{split($0, a, "="); print a[2]}')

TARGET_CRS="EPSG:3857"

TARGET_BANDS_HIRES="B02_10m B03_10m B04_10m"
TARGET_BANDS_LOWRES="B05_20m B06_20m B07_20m B08_10m B8A_20m B11_20m B12_20m"
TARGET_BANDS_QA="MSK_CLDPRB_20m MSK_SNWPRB_20m"

buildSubDS() {
    local PRODUCT_ZIP=$1
    local TARGET_BAND_NAMES=$2
    local TARGET_RES=$3
    local BUILD_DIR=$4
    local TARGET_CRS=$5
    local DATATYPE=$6
    local BUNDLE_NAME=$7 # (like HIRES, LORES, QA)

    local INPUT_FILES=""
    for ds in $subds
    do
        local INPUT_FILES="$INPUT_FILES $(gdalinfo $ds | grep ".jp2" | grep -E "$(echo $TARGET_BAND_NAMES | sed 's/ /|/g')")"
    done

    mkdir -p $BUILD_DIR/bands
    
    echo "building warped jp2s"
    local transformedFiles=""
    local jobs=()
    for file in $INPUT_FILES
    do
        local outFile="$BUILD_DIR/bands/$(basename -- $file)"

        local jobs+=("set -x; gdalwarp -t_srs $TARGET_CRS -ot $DATATYPE -overwrite $file $outFile > /dev/null")

        local transformedFiles="$transformedFiles $outFile"
    done
    printf "%s\n" "${jobs[@]}" | xargs -I CMD --max-procs=24 bash -c CMD
    echo "done..."

    local vrtFname=$BUILD_DIR/$(basename $(echo $PRODUCT_ZIP | sed "s/.zip/-$BUNDLE_NAME.vrt/g"))
    (set -x; gdalbuildvrt -resolution user -tr $TARGET_RES $TARGET_RES -overwrite -separate $vrtFname $transformedFiles)
    (set -x; ./setBandDescriptions/build/satsample_setbandinfo $vrtFname $TARGET_BAND_NAMES)
    (set -x; gdal_translate -tr 10 10 -co COMPRESS=ZSTD -co NUM_THREADS=32 -co TILED=YES $vrtFname $BUILD_DIR/$(basename $(echo $PRODUCT_ZIP | sed "s/.zip/-$BUNDLE_NAME.tif/g")))
}

buildSubDS $1 "$TARGET_BANDS_HIRES" 10 ./build EPSG:3857 UInt16 HIRES &>> ./build/log.txt
buildSubDS $1 "$TARGET_BANDS_LOWRES" 20 ./build EPSG:3857 UInt16 LOWRES &>> ./build/log.txt
buildSubDS $1 "$TARGET_BANDS_QA" 20 ./build EPSG:3857 Byte QA &>> ./build/log.txt

# unzip -l test-in.zip | grep -E 'MSK_DETFOO' | tr -s ' ' | cut -d ' ' -s -f 5 - | xargs -I{} printf '/vsizip/test-in.zip/{} '
