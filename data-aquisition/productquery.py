import pprint
import shapely
import urllib3
import heapq


def queryProducts(
    year: int,
    month: int,
    tile: str,
    shape: shapely.Polygon,
    maxCloudCover=20,
    minIntersection: float = 0.8,
):
    assert month >= 1 and month <= 12

    DAYS_IN_MONTH = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]

    queryURL = f"https://catalogue.dataspace.copernicus.eu/odata/v1/Products?$filter=Collection/Name eq 'SENTINEL-2' and ContentDate/Start gt {year}-{month:02}-01T00:00:00.000Z and ContentDate/Start lt {year}-{month:02}-{DAYS_IN_MONTH[month - 1]}T00:00:00.000Z and Attributes/OData.CSC.StringAttribute/any(att:att/Name eq 'tileId' and att/OData.CSC.StringAttribute/Value eq '{tile}') and Attributes/OData.CSC.DoubleAttribute/any(att:att/Name eq 'cloudCover' and att/OData.CSC.DoubleAttribute/Value le {maxCloudCover})&$top=1000&$expand=Attributes"

    data = urllib3.request("GET", queryURL, timeout=30).json()

    candidate = None
    candidateScore = None
    candidateInfo = None
    if not "value" in data.keys():
        pprint.pp(data)
        return None

    for val in data["value"]:
        geom = shapely.from_wkt(val["Footprint"].split(";")[1])

        percentIntersect = shape.intersection(geom).area / shape.area

        if percentIntersect >= minIntersection:
            cloudCover = None
            for attribute in val["Attributes"]:
                if attribute["Name"] == "cloudCover":
                    cloudCover = float(attribute["Value"])
                    break

            if cloudCover != None and cloudCover <= maxCloudCover:
                score = (1.0 - (cloudCover / 100)) * percentIntersect

                if candidateScore == None or candidateScore < score:
                    candidate = val
                    candidateScore = score
                    candidateInfo = {
                        "score": score,
                        "cloudCover": cloudCover,
                        "percentIntersect": percentIntersect,
                    }

    return candidate, candidateInfo
