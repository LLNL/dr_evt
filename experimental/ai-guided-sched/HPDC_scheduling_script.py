######### Parameters ###########
N = 5  # Number of trials
num_jobs = 7  # Number of jobs per stream (70,000 is around a week)
# Set the below to true if you want their results
use_nonModel = True  # Stock scheduler, ground truth
use_model_10 = False  # Model X with 10% Lambda
use_model_20 = False  # Model X with 20% Lambda
use_oracle = False  # Oracle
user_random_seed = 19  # Set for differeing runs

import pandas as pd
import numpy as np
from job_queue_generator import generateQueueFromDF
from scheduler_functions import schedule

dfModelX_10 = pd.read_csv("Data/modelX_df.csv")
dfModelX_20 = pd.read_csv("Data/modelX-20%_df.csv")
dfOracle = pd.read_csv("Data/oracle_df.csv")

# Stores turnaround times
stockRes = []
modelX_10Res = []
modelX_20Res = []
oracleRes = []

# Stores Makespans
stockMSRes = []
modelX_10MSRes = []
modelX_20MSRes = []
oracleMSRes = []

# To use if resource scaling
# sampleRange = [1280,1024,768,512,256]
random_seeds = []
random_starts = []
for i in range(0, N):
    random_seed = (
        user_random_seed + i
    )  # i*3+1 -> random seed for sampling different jobs
    random_seeds.append(random_seed)
    random_start = (
        random_seed * 70000
    ) % 1000000  # Starting at a different date every run
    random_starts.append(random_start)

    if use_nonModel:
        jobQueue_stock = generateQueueFromDF(
            dfModelX_10, 1, num_jobs, random_seed, startI=random_start
        )
        jobQueue_stock.to_csv(f"Results/job_stream.{random_seed}.csv")
        stockRT, _, stockWT = schedule(jobQueue_stock, 256, use_config=False)
        stockRes.append(stockWT)
        stockMSRes.append(stockRT)

    if use_model_10:
        jobQueue_modelX_10 = generateQueueFromDF(
            dfModelX_10, 1, num_jobs, random_seed, startI=random_start
        )
        jobQueue_modelX_10.to_csv(f"Results/job_stream.{random_seed}.csv")
        modelX_10RT, _, modelX_10WT = schedule(jobQueue_modelX_10, 256, use_config=True)
        modelX_10Res.append(modelX_10WT)
        modelX_10MSRes.append(modelX_10RT)

    if use_model_20:
        jobQueue_modelX_20 = generateQueueFromDF(
            dfModelX_20, 1, num_jobs, random_seed, startI=random_start
        )
        jobQueue_modelX_20.to_csv(f"Results/job_stream.{random_seed}.csv")
        modelX_20RT, _, modelX_20WT = schedule(jobQueue_modelX_20, 256, use_config=True)
        modelX_20Res.append(modelX_20WT)
        modelX_20MSRes.append(modelX_20RT)

    if use_oracle:
        jobQueue_oracle = generateQueueFromDF(
            dfOracle, 1, num_jobs, random_seed, startI=random_start
        )
        jobQueue_oracle.to_csv(f"Results/job_stream.{random_seed}.csv")
        oracleRT, _, oracleWT = schedule(jobQueue_oracle, 256, use_config=True)
        oracleRes.append(oracleWT)
        oracleMSRes.append(oracleRT)

if use_nonModel:
    df = pd.DataFrame()
    df["Turnaround Time"] = stockRes
    df["Makespan"] = stockMSRes
    df["Random Seed"] = random_seeds
    df["Random Start Index"] = random_starts
    df.to_csv("Results/non_model_results.csv", index=False)

if use_model_10:
    df = pd.DataFrame()
    df["Turnaround Time"] = modelX_10Res
    df["Makespan"] = modelX_10MSRes
    df["Random Seed"] = random_seeds
    df["Random Start Index"] = random_starts
    df.to_csv("Results/model_10_results.csv", index=False)

if use_model_20:
    df = pd.DataFrame()
    df["Turnaround Time"] = modelX_20Res
    df["Makespan"] = modelX_20MSRes
    df["Random Seed"] = random_seeds
    df["Random Start Index"] = random_starts
    df.to_csv("Results/model_20_results.csv", index=False)

if use_oracle:
    df = pd.DataFrame()
    df["Turnaround Time"] = oracleRes
    df["Makespan"] = oracleMSRes
    df["Random Seed"] = random_seeds
    df["Random Start Index"] = random_starts
    df.to_csv("Results/oracle_results.csv", index=False)
