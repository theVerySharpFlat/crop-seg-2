#!/bin/bash

for dir in $(find $1 -type d | grep -E "[0-9]{4}/[0-9]{1,2}$")
do
    echo $dir

    for product in $(find $dir -name "*.zip" -type f)
    do
        # echo "     $product"
        ./buildProductLevelVRT.sh $product
    done

    vrts=$(find $dir -name "*-EPSG3857.vrt" -type f | sed "s/\n/ /g")

    gdalbuildvrt -overwrite "$dir/product.vrt" $vrts
done
