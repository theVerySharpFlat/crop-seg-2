from satsamplepy.satsamplepy import Sample

from torchgeo.datasets import CDL, BoundingBox

from pyproj import Transformer, CRS

import torchvision.transforms.functional as Fv
from torchvision.transforms import InterpolationMode
import torch

import time
import datetime


def isCrop(val):
    return torch.where(
        torch.logical_or(
            torch.logical_or(
                torch.logical_and(val >= 1, val <= 60),
                torch.logical_and(val >= 66, val <= 80),
            ),
            torch.logical_and(val >= 195, val <= 255),
        ),
        1.0,
        0.0,
    )


def getCDLMask(cdl: CDL, sample: Sample, sampleDim: int):
    srcCRS = CRS.from_string(sample.crs)
    dstCRS = cdl.crs
    transformer = Transformer.from_crs(srcCRS, dstCRS)

    physicalBounds = transformer.transform_bounds(
        sample.coords_min[0],
        sample.coords_min[1],
        sample.coords_max[0],
        sample.coords_max[1],
    )

    temporalBounds = (
        time.mktime(datetime.datetime(sample.year, 1, 1).timetuple()),
        time.mktime(datetime.datetime(sample.year, 12, 31).timetuple()),
    )

    bbox = BoundingBox(
        physicalBounds[0],
        physicalBounds[2],
        physicalBounds[1],
        physicalBounds[3],
        temporalBounds[0],
        temporalBounds[1],
    )

    sample = cdl[bbox]
    sample["mask"] = Fv.resize(
        sample["mask"],
        (sampleDim, sampleDim),
        interpolation=InterpolationMode.NEAREST,
        antialias=False,
    )

    return isCrop(sample["mask"])
