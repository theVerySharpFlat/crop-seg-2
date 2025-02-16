from satsamplepy.satsamplepy import (
    Sampler,
    DateRange,
    SampleOptions,
    SampleCacheGenOptions,
    Sample,
)

from torchgeo.datasets import CDL

from torch.distributed import init_process_group, destroy_process_group
from torch.nn.parallel import DistributedDataParallel as DDP

import tqdm

import cdl as cdlutil
import torch
import os

import numpy as np
import unet as UNET
import dice

import torch.nn.functional as F

from torch.utils.data import Dataset, DataLoader
import torch.multiprocessing as mp


class CustomDataset(Dataset):
    def __init__(self, sampler: Sampler, n, batchesPerEpoch):
        self.sampler = sampler
        self.n = n
        self.batchesPerEpoch = batchesPerEpoch

    def __len__(self):
        return self.batchesPerEpoch

    def __getitem__(self, index):
        samples = self.sampler.randomSample2(self.n)

        return samples[0], samples[1]


# def build_batch(samples: list[Sample], sampleDim, cdl: CDL):
#     batch = []
#     masks = []
#     for sample in samples:
#         bands = []
#         for band in sample.bands:
#             b = np.array(band, dtype=np.float32)
#             b.shape = (sampleDim, sampleDim)
#
#             bands.append(b)
#         batch.append(bands)
#         masks.append(cdlutil.getCDLMask(cdl, sample, sampleDim))
#
#     return {"data": torch.tensor(np.array(batch)), "masks": torch.stack(masks, dim=0)}


def train_epoch(model: UNET.UNet, batch, opt, device):
    image = (batch["data"]).to(device)
    mask = (batch["masks"]).to(device)

    opt.zero_grad()

    pred = model(image)
    loss = dice.dice_loss(
        F.sigmoid(pred.squeeze(1)),
        mask.squeeze(1).float(),
        multiclass=False,
    )

    loss.backward()
    opt.step()

    return loss.item()


def ddp_setup(rank: int, world_size: int):
    """
    Args:
        rank: Unique identifier of each process
       world_size: Total number of processes
    """
    os.environ["MASTER_ADDR"] = "localhost"
    os.environ["MASTER_PORT"] = "12355"
    torch.cuda.set_device(rank)
    init_process_group(backend="nccl", rank=rank, world_size=world_size)


def main(rank: int, world_size: int):
    # ddp_setup(rank, world_size)
    DEVICE = rank
    print("DEVICE:", DEVICE)

    N_BANDS = 11
    BATCH_SIZE = 48
    N_EPOCHS = 100
    BATCHES_PER_EPOCH = 100
    N_TEST_BATCHES = 12

    SAMPLE_DIM = 256

    # dataset initialization
    TRAIN_DATERANGE = DateRange()
    TRAIN_DATERANGE.minYear = 2022
    TRAIN_DATERANGE.maxYear = 2022
    TRAIN_DATERANGE.minMonth = 1
    TRAIN_DATERANGE.maxMonth = 12
    TRAIN_DATERANGE.minDay = 0
    TRAIN_DATERANGE.maxDay = 31

    TRAIN_SAMPLEOPT = SampleOptions()
    TRAIN_SAMPLEOPT.dbPath = "train-cache.db"
    TRAIN_SAMPLEOPT.nCacheGenThreads = 16
    TRAIN_SAMPLEOPT.nCacheQueryThreads = 8

    TRAIN_CACHEOPT = SampleCacheGenOptions()
    TRAIN_CACHEOPT.cldMax = 50
    TRAIN_CACHEOPT.snwMax = 50
    TRAIN_CACHEOPT.minOKPercentage = 0.99999
    TRAIN_CACHEOPT.sampleDim = SAMPLE_DIM

    train_sampler = Sampler(
        "../data/", TRAIN_SAMPLEOPT, TRAIN_CACHEOPT, TRAIN_DATERANGE
    )

    TEST_DATERANGE = DateRange()
    TEST_DATERANGE.minYear = 2023
    TEST_DATERANGE.maxYear = 2023
    TEST_DATERANGE.minMonth = 1
    TEST_DATERANGE.maxMonth = 12
    TEST_DATERANGE.minDay = 0
    TEST_DATERANGE.maxDay = 31

    TEST_SAMPLEOPT = SampleOptions()
    TEST_SAMPLEOPT.dbPath = "test-cache.db"
    TEST_SAMPLEOPT.nCacheGenThreads = 32
    TEST_SAMPLEOPT.nCacheQueryThreads = 64

    TEST_CACHEOPT = SampleCacheGenOptions()
    TEST_CACHEOPT.cldMax = 50
    TEST_CACHEOPT.snwMax = 50
    TEST_CACHEOPT.minOKPercentage = 0.99999
    TEST_CACHEOPT.sampleDim = SAMPLE_DIM

    test_sampler = Sampler("../data/", TEST_SAMPLEOPT, TEST_CACHEOPT, TEST_DATERANGE)

    train_ds = CustomDataset(train_sampler, BATCH_SIZE, BATCHES_PER_EPOCH)
    train_loader = DataLoader(
        train_ds,
        shuffle=False,
        batch_size=1,
        num_workers=8,
    )

    test_ds = CustomDataset(test_sampler, BATCH_SIZE, N_TEST_BATCHES)
    test_loader = DataLoader(test_ds, shuffle=False, batch_size=1, num_workers=8)

    # test_ds = CustomDataset(test_sampler, BATCH_SIZE, 1)

    cdl = CDL("data", download=True, checksum=True, years=[2023, 2022], res=10)

    unet = UNET.UNet(N_BANDS, n_classes=1).to(DEVICE)
    # unet = DDP(unet, device_ids=[DEVICE])
    opt = torch.optim.AdamW(
        unet.parameters(), lr=0.00005, foreach=True, weight_decay=1e-2
    )

    for epoch in range(1, N_EPOCHS + 1):
        totalTrainLoss = 0
        # samples = train_sampler.randomSample(BATCH_SIZE * BATCHES_PER_EPOCH)
        l = tqdm.tqdm(train_loader)
        if rank != 0:
            l = train_loader
        for batch in l:
            # samples = train_sampler.randomSample2(BATCH_SIZE)
            batch = {
                "data": batch[0].squeeze(0),
                "masks": cdlutil.isCrop(batch[1].squeeze(0)),
            }
            # print("samples", samples[0])
            # print("masks", samples[1])
            # batch = build_batch(
            #     samples[batchNum * BATCH_SIZE : (batchNum + 1) * BATCH_SIZE],
            #     SAMPLE_DIM,
            #     cdl,
            # )

            loss = train_epoch(unet, batch, opt, DEVICE)
            totalTrainLoss += loss

        print("avg train loss:", totalTrainLoss / BATCHES_PER_EPOCH)

        testSamples = test_sampler.randomSample2(BATCH_SIZE)

        with torch.no_grad():
            # image = (batch["data"]).to(DEVICE)
            # mask = (batch["masks"]).to(DEVICE)
            total_loss = 0
            l = tqdm.tqdm(test_loader)
            if rank != 0:
                l = test_loader

            for batch in l:
                image = testSamples[0].to(DEVICE)
                mask = cdlutil.isCrop(testSamples[1]).to(DEVICE)

                pred = unet(image)
                loss = dice.dice_loss(
                    F.sigmoid(pred.squeeze(1)),
                    mask.squeeze(1).float(),
                    multiclass=False,
                )
                total_loss += loss.item()

            print("test loss:", total_loss / N_TEST_BATCHES)


if __name__ == "__main__":
    world_size = torch.cuda.device_count()
    world_size = 1
    main(0, 1)
    # mp.spawn(main, args=(world_size,), nprocs=world_size)
