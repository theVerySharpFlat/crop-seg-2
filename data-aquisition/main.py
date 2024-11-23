import argparse
import os

import shapely
import urllib3
from dotenv import load_dotenv
import pprint

from productquery import queryProducts
from tiling import build_tiling_info

import pathlib

# {"type":"Polygon","coordinates":[[[-95.734863,40.597271],[-96.679687,43.53262],[-91.362305,43.500752],[-90.043945,42.065607],[-91.318359,40.563895],[-95.734863,40.597271]]]}

load_dotenv()

USERNAME = os.environ.get("CDSE_USERNAME")
PASSWORD = os.environ.get("CDSE_PASSWORD")

iowaPoly = shapely.Polygon(
    [
        [-95.734863, 40.597271],
        [-96.679687, 43.53262],
        [-91.362305, 43.500752],
        [-90.043945, 42.065607],
        [-91.318359, 40.563895],
        [-95.734863, 40.597271],
    ]
)


argparser = argparse.ArgumentParser(
    prog="python3 main.py",
    description="Downloads Sentinel data from the given region",
    formatter_class=argparse.ArgumentDefaultsHelpFormatter,
)

argparser.add_argument(
    "-l",
    "--location",
    type=str,
    default=iowaPoly.wkt,
    help="The location (in wkt) to query copernicus for",
)

argparser.add_argument(
    "-m",
    "--months",
    type=int,
    nargs="*",
    choices=[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12],
    default=[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12],
    help="The months to query copernicus for",
)

argparser.add_argument(
    "-sy",
    "--start-year",
    type=int,
    help="The year to start looking for data on (inclusive)",
    required=True,
)

argparser.add_argument(
    "-ey",
    "--end-year",
    type=int,
    help="The year to end looking for data on (inclusive)",
    required=True,
)

argparser.add_argument(
    "-cc",
    "--max-cloud-cover",
    type=int,
    help="The maximum acceptable cloud cover (in %)",
    default=20,
)

argparser.add_argument(
    "-mti",
    "--minimum-tile-intersection",
    type=int,
    help="The minimum acceptable tile intersection with the shape (in %)",
    default=30,
)

argparser.add_argument(
    "-o",
    "--output-directory",
    type=pathlib.Path,
    help="The directory to output downloads to",
    default=pathlib.Path("./data"),
)

args = argparser.parse_args()

assert args.start_year <= args.end_year

loc = shapely.from_wkt(args.location)

tilingInfo = build_tiling_info(loc, minIntersect=args.minimum_tile_intersection / 100.0)

# resp = urllib3.request(
#     "POST",
#     "https://identity.dataspace.copernicus.eu/auth/realms/CDSE/protocol/openid-connect/token",
#     body=f"client_id=cdse-public&username={USERNAME}&password={PASSWORD}&grant_type=password",
#     headers={"Content-Type": "application/x-www-form-urlencoded"},
# )
#
# ACCESS_TOKEN = ""
# try:
#     data = resp.json()
#
#     if not "access_token" in data.keys():
#         print("data does not not have an access token!", data)
#         exit(1)
#
#     ACCESS_TOKEN = data["access_token"]
# except Exception as e:
#     print("could not decode auth data as json!", e)
#     exit(1)
#
# print("access token is", ACCESS_TOKEN)

# queryURL = "https://catalogue.dataspace.copernicus.eu/odata/v1/Products?$filter=Collection/Name eq 'SENTINEL-2' and OData.CSC.Intersects(area=geography'SRID=4326;POLYGON((-95.734863 40.597271,-96.679687 43.53262,-91.362305 43.500752,-90.043945 42.065607,-91.318359 40.563895,-95.734863 40.597271))') and ContentDate/Start gt 2023-01-01T00:00:00.000Z and ContentDate/Start lt 2023-01-31T00:00:00.000Z&$top=1000"
#
# resp = urllib3.request("GET", (queryURL), timeout=30)
#
# print(resp.json())
# print(len(resp.json()["value"]))

# print("intersectionPercentage:", tilingInfo[6]["percentIntersection"])

products = []

for year in range(args.start_year, args.end_year + 1):
    for month in range(1, 13):
        if not month in args.months:
            continue

        for tile in tilingInfo:
            product, info = queryProducts(
                year,
                month,
                tile["name"],
                tile["intersection"],
                maxCloudCover=args.max_cloud_cover,
            )

            tileName = tile["name"]
            print(f"{month}/{year}, tile={tileName}: ", end="")
            if product == None:
                print("None")
            else:
                print(product["Name"], info)

                products.append(
                    (product["Id"], (year, month, tileName, product["Name"]))
                )


resp = input(
    f"found {len(products)} products between {args.start_year} and {args.end_year} for the months {args.months}. Download size is estimated to be {len(products)}GB. Continue? [Y/N]: "
)

if len(resp.lower()) <= 0 or resp.lower()[0] != "y":
    exit(0)

authResp = urllib3.request(
    "POST",
    "https://identity.dataspace.copernicus.eu/auth/realms/CDSE/protocol/openid-connect/token",
    body=f"client_id=cdse-public&username={USERNAME}&password={PASSWORD}&grant_type=password",
    headers={"Content-Type": "application/x-www-form-urlencoded"},
)

ACCESS_TOKEN = ""
try:
    data = authResp.json()

    if not "access_token" in data.keys():
        print("data does not not have an access token!", data)
        exit(1)

    ACCESS_TOKEN = data["access_token"]
except Exception as e:
    print("could not decode auth data as json!", e)
    exit(1)

for i, product in enumerate(products):
    id = product[0]
    info = product[1]

    d = args.output_directory / str(info[0]) / str(info[1]) / str(info[2])

    os.makedirs(d, exist_ok=True)

    command = f'curl -H "Authorization: Bearer {ACCESS_TOKEN}" "https://catalogue.dataspace.copernicus.eu/odata/v1/Products({id})/$zip" --location-trusted -O --output-dir {d} --skip-existing'

    command = f"curl -H \"Authorization: Bearer {ACCESS_TOKEN}\" 'https://catalogue.dataspace.copernicus.eu/odata/v1/Products({id})/$value' --location-trusted --skip-existing --output {d / info[3]}.zip"
    os.system(command)
    print(f"{i + 1}/{len(products)}")

    if i % 5 == 0:
        authResp = urllib3.request(
            "POST",
            "https://identity.dataspace.copernicus.eu/auth/realms/CDSE/protocol/openid-connect/token",
            body=f"client_id=cdse-public&username={USERNAME}&password={PASSWORD}&grant_type=password",
            headers={"Content-Type": "application/x-www-form-urlencoded"},
        )

        ACCESS_TOKEN = ""
        try:
            data = authResp.json()

            if not "access_token" in data.keys():
                print("data does not not have an access token!", data)
                exit(1)

            ACCESS_TOKEN = data["access_token"]
        except Exception as e:
            print("could not decode auth data as json!", e)
            exit(1)
