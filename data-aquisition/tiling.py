import shapely


def build_tiling_info(shape: shapely.Polygon, minIntersect=0.1):
    info = []
    with open("S2_TilingSystem2-1.txt", "r") as f:
        for i, line in enumerate(f):
            if i == 0:
                continue

            vals = line.split()

            minLon = float(vals[5])
            maxLon = float(vals[6])
            minLat = float(vals[7])
            maxLat = float(vals[8])

            tileBox = shapely.box(minLon, minLat, maxLon, maxLat)

            intersection = shapely.intersection(shape, tileBox)
            intersectPercent = (intersection.area) / (tileBox.area)

            if intersectPercent < minIntersect:
                continue

            info.append(
                {
                    "name": vals[0],
                    "bbox": tileBox,
                    "intersection": intersection,
                    "percentIntersection": intersectPercent,
                }
            )
    return info
