#!/bin/bash

subds=$(gdalinfo $1 | grep -E "SUBDATASET_[0-9]+_NAME" | awk '{split($0, a, "="); print a[2]}')

TARGET_CRS="EPSG:3857"

TARGET_BANDS_HIRES="B02_10m B03_10m B04_10m B08_10m"
TARGET_BANDS_LOWRES="B05_20m B06_20m B07_20m B8A_20m B11_20m B12_20m"
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
    
    # echo "building warped jp2s"
    # local transformedFiles=""
    # local jobs=()
    # for file in $INPUT_FILES
    # do
    #     local outFile="$BUILD_DIR/bands/$(basename -- $file)"
    #
    #     local jobs+=("set -x; gdalwarp -t_srs $TARGET_CRS -ot $DATATYPE -overwrite $file $outFile > /dev/null")
    #
    #     local transformedFiles="$transformedFiles $outFile"
    # done
    # printf "%s\n" "${jobs[@]}" | xargs -I CMD --max-procs=24 bash -c CMD
    # echo "done..."

    local vrtFname=$BUILD_DIR/$(basename $(echo $PRODUCT_ZIP | sed "s/.zip/-$BUNDLE_NAME.vrt/g"))
    (set -x; gdalbuildvrt -resolution user -tr $TARGET_RES $TARGET_RES -overwrite -separate $vrtFname $INPUT_FILES)
    (set -x; ./setBandDescriptions/build/satsample_setbandinfo $vrtFname $TARGET_BAND_NAMES)
    (set -x; gdal_translate -tr $TARGET_RES $TARGET_RES -co COMPRESS=ZSTD -co ZSTD_LEVEL=15 -co PREDICTOR=2 -co NUM_THREADS=1 -co TILED=YES $vrtFname $BUILD_DIR/$(basename $(echo $PRODUCT_ZIP | sed "s/.zip/-$BUNDLE_NAME.tif/g")))

    # (set -x; mv $BUILD_DIR/*.tif $BUILD_DIR/..)
    # (set -x; rm -rf $BUILD_DIR)
}

buildSampleMask() {
    local PRODUCT_ZIP=$1
    local BUILD_DIR=$2

    local DETFOO_MASKS=$(unzip -Z1 $PRODUCT_ZIP | grep -E "DETFOO.*.jp2$" | xargs -I {} echo /vsizip/$PRODUCT_ZIP/{} | paste -s -d ' ')
    local CLD_MASK="/vsizip/$PRODUCT_ZIP/$(unzip -Z1 $PRODUCT_ZIP | grep -E "CLD.*20m.jp2$")"
    local SNW_MASK="/vsizip/$PRODUCT_ZIP/$(unzip -Z1 $PRODUCT_ZIP | grep -E "SNW.*20m.jp2$")"

    echo $DETFOO_MASKS
    echo $CLD_MASK
    echo $SNW_MASK

    local vrtFname=$BUILD_DIR/$(basename $(echo $PRODUCT_ZIP | sed "s/.zip/-MSK.vrt/g"))
    (set -x; gdalbuildvrt -resolution user -tr 20 20 -overwrite -separate $vrtFname $DETFOO_MASKS $CLD_MASK $SNW_MASK)
    (set -x; gdal_translate -tr 20 20 -co COMPRESS=ZSTD -co ZSTD_LEVEL=15 -co PREDICTOR=2 -co NUM_THREADS=1 -co TILED=YES $vrtFname $BUILD_DIR/$(basename $(echo $PRODUCT_ZIP | sed "s/.zip/-MSK.tif/g")))

    # (set -x; ./sample-map-gen/build/satsample_mapgen --dfm $DETFOO_MASKS --cld $CLD_MASK --snw $SNW_MASK --snwm 50 --cldm 50 -o $BUILD_DIR/MSK_OK.tiff)
}


buildSubDS $1 "$TARGET_BANDS_HIRES" 10 $(dirname $1)/build EPSG:3857 UInt16 HIRES
buildSubDS $1 "$TARGET_BANDS_LOWRES" 20 $(dirname $1)/build EPSG:3857 UInt16 LOWRES
buildSampleMask $1 $(dirname $1)/build
# buildSubDS $1 "$TARGET_BANDS_QA" 20 $(dirname $1)/build EPSG:3857 Byte QA &>> $(dirname $1)/log.txt
OUTZIP=$(dirname $(realpath $1))/REPACK_$(basename $1)

p=$(pwd)
cd $(dirname $1)/build
rm $OUTZIP
(set -x; zip -0 $OUTZIP *.tif)
cd $p

du -sh $OUTZIP
rm -rf $(dirname $1)/build

# unzip -l test-in.zip | grep -E 'MSK_DETFOO' | tr -s ' ' | cut -d ' ' -s -f 5 - | xargs -I{} printf '/vsizip/test-in.zip/{} '
