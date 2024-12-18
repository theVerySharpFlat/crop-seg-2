# determine the bands we want to keep

subds=$(gdalinfo $1 | grep -E "SUBDATASET_[0-9]+_NAME" | awk '{split($0, a, "="); print a[2]}')

TARGET_BANDS_HIRES="B02_10m B03_10m B04_10m"
TARGET_BANDS_LOWRES="B05_20m B06_20m B07_20m B08_10m B8A_20m B11_20m B12_20m"
TARGET_QA_BANDS="MSK_CLDPRB_20m MSK_SNWPRB_20m"

hiresFiles=""
lowresFiles=""
qafiles=""
for ds in $subds
do
    hiresFiles="$hiresFiles $(gdalinfo $ds | grep ".jp2" | grep -E "$(echo $TARGET_BANDS_HIRES | sed 's/ /|/g')")"
    lowresFiles="$lowresFiles $(gdalinfo $ds | grep ".jp2" | grep -E "$(echo $TARGET_BANDS_LOWRES | sed 's/ /|/g')")"
    qafiles="$qafiles $(gdalinfo $ds | grep ".jp2" | grep -E "$(echo $TARGET_QA_BANDS | sed 's/ /|/g')")"
done

rootDir=$(dirname $1)
mkdir -p "$rootDir/bands"

transformedHiresFiles=""
transformedLowresFiles=""
transformedQaFiles=""
jobs=()

echo "building warped jp2s"
for file in $hiresFiles
do
    outFile="$rootDir/bands/$(basename -- $file | sed 's/.jp2/-EPSG3857.jp2/g')"

    # this sucks
    # jobs+=("set -x; gdalwarp -t_srs EPSG:3857 -ot UInt16 -overwrite -tr 10 10 $file $outFile > /dev/null")
    jobs+=("set -x; gdalwarp -t_srs EPSG:3857 -ot UInt16 -overwrite $file $outFile > /dev/null")

    transformedHiresFiles="$transformedHiresFiles $outFile"
    # echo "filename: $(basename -- $file)"
    # echo "dump directory: $(dirname $1)"
done
printf "%s\n" "${jobs[@]}" | xargs -I CMD --max-procs=24 bash -c CMD
echo "done..."

jobs=()
echo "building warped jp2s"
for file in $lowresFiles
do
    outFile="$rootDir/bands/$(basename -- $file | sed 's/.jp2/-EPSG3857.jp2/g')"

    # this sucks
    # jobs+=("set -x; gdalwarp -t_srs EPSG:3857 -ot UInt16 -overwrite -tr 10 10 $file $outFile > /dev/null")
    jobs+=("set -x; gdalwarp -t_srs EPSG:3857 -ot UInt16 -overwrite $file $outFile > /dev/null")

    transformedLowresFiles="$transformedLowresFiles $outFile"
    # echo "filename: $(basename -- $file)"
    # echo "dump directory: $(dirname $1)"
done
printf "%s\n" "${jobs[@]}" | xargs -I CMD --max-procs=24 bash -c CMD
echo "done..."

jobs=()
echo "building warped qa jp2s"
for file in $qafiles
do
    outFile="$rootDir/bands/$(basename -- $file | sed 's/.jp2/-EPSG3857.jp2/g')"

    # this sucks
    jobs+=("set -x; gdalwarp -t_srs EPSG:3857 -overwrite $file $outFile > /dev/null")

    transformedQaFiles="$transformedQaFiles $outFile"
    # echo "filename: $(basename -- $file)"
    # echo "dump directory: $(dirname $1)"
done
printf "%s\n" "${jobs[@]}" | xargs -I CMD --max-procs=24 bash -c CMD
echo "done..."

hiresFname=$(echo $1 | sed "s/.zip/-HIRES-EPSG3857.vrt/g")
lowresFname=$(echo $1 | sed "s/.zip/-LOWRES-EPSG3857.vrt/g")
qafname=$(echo $1 | sed "s/.zip/-QA-EPSG3857.vrt/g")


(set -x; gdalbuildvrt -resolution user -tr 10 10 -overwrite -separate $hiresFname $transformedHiresFiles)
(set -x; ./setBandDescriptions/build/satsample_setbandinfo $hiresFname $TARGET_BANDS_HIRES)
(set -x; gdal_translate -tr 10 10 -co COMPRESS=ZSTD -co NUM_THREADS=32 -co TILED=YES $hiresFname $(echo $1 | sed "s/.zip/-HIRES.tif/g"))

(set -x; gdalbuildvrt -resolution user -tr 20 20 -overwrite -separate $lowresFname $transformedLowresFiles)
(set -x; ./setBandDescriptions/build/satsample_setbandinfo $lowresFname $TARGET_BANDS_LOWRES)
(set -x; gdal_translate -tr 20 20 -co COMPRESS=ZSTD -co NUM_THREADS=32 -co TILED=YES $lowresFname $(echo $1 | sed "s/.zip/-LOWRES.tif/g"))

(set -x; gdalbuildvrt -resolution user -tr 20 20 -overwrite -separate $qafname $transformedQaFiles)
(set -x; ./setBandDescriptions/build/satsample_setbandinfo $qafname $TARGET_QA_BANDS)
(set -x; gdal_translate -tr 20 20 -co COMPRESS=ZSTD -co NUM_THREADS=32 -co TILED=YES $qafname $(echo $1 | sed "s/.zip/-QA.tif/g"))

rm -rf $rootDir/bands $hiresFname $qafname

# upscale as necessary
# place updated in the 10m bucket
# somehow change the ds to reflect
