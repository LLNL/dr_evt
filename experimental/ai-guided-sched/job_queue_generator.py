import pandas as pd
import numpy as np
import math


def generateQueueFromDF(df: pd.DataFrame, numUsers, numJobs, randomSeed, startI=0):
    totalDF = df.copy(deep=True)
    # Generating queue
    jobQueue = None
    for i in range(0, int(numJobs / totalDF.shape[0])):
        jobQueue = pd.concat(
            [jobQueue, totalDF.sample(frac=1, random_state=randomSeed)]
        )
    jobQueue = pd.concat(
        [jobQueue, totalDF.sample(numJobs % totalDF.shape[0], random_state=randomSeed)]
    )
    jobQueue["minute"] = jobQueue["runtime"] / 60
    jobQueue["minute"] = jobQueue["minute"].apply(math.trunc)

    # Generating synthetic wall times based on real user data
    wallTimeDF = pd.read_csv("Data/wall_time.csv")
    submitTimesList = np.loadtxt("Data/submit_times.txt", dtype=int)
    wallTimes = []
    submitTimes = []
    randomGenerator = np.random.default_rng(seed=randomSeed + 13)
    for i in range(0, jobQueue.shape[0]):
        currMin = jobQueue.iloc[i]["minute"]
        tempDf = wallTimeDF[wallTimeDF["exec_time"] == currMin]
        submitTimes.append(submitTimesList[startI + i] - submitTimesList[startI])
        if tempDf.shape[0] > 0:
            randInt = randomGenerator.integers(low=0, high=tempDf.shape[0], size=1)
            wallTimes.append(tempDf.iloc[randInt[0]]["time_limit"])
        else:
            print("Alternative")
            currHr = currMin / 30
            currHr = math.ceil(currHr)
            wallTimes.append(curHr * 30)
    jobQueue["wall_time"] = wallTimes
    jobQueue["submit_time"] = submitTimes
    # Generating user list, Round Robin style
    users = []
    for i in range(0, numJobs):
        users.append(i % numUsers)
    jobQueue["user"] = users

    jobQueue = jobQueue.reset_index(drop=True)
    return jobQueue


"""
def generateQueue(xFilePath, yFilePath, predFilePath, numUsers, numJobs, randomSeed):

    # Reading in data
    x = pd.read_csv(xFilePath)
    y = pd.read_csv(yFilePath)
    pred = pd.read_csv(predFilePath)
    
    # Dropping Unnessecary Columns
    columnMap = {
        '0':'bw_level',
        '1':'ipath_0',
        '2':'ipath_1',
        '3':'node_count',
        '4':'power_cap',
        '5':'thread_count',
        '6':'Domain_cg',
        '7':'Domain_perfvar',
        '8':'algorithm_pak',
        '9':'algorithm_rand',
        '10':'algorithm_spr',
        '11':'app_cg'
        }
    x = x.rename(columns=columnMap)
    x = x.drop(columns=['bw_level','ipath_0','ipath_1','Domain_cg','Domain_perfvar','app_cg'])

    # Reversing the min max scaling that occured during model training. 
    x_max = [4096.0, 115.0, 24.0, 1.0,1.0,1.0]
    x_min = [512.0,   64.0, 16.0, 0.0,0.0,0.0]
    for i in range(0,6):
        x.iloc[:,i:i+1] = x.iloc[:,i:i+1]*(x_max[i]-x_min[i])+x_min[i]

    y_max = 127.70653
    y_min = 9.39000
    y.iloc[:,0] = y.iloc[:,0]*(y_max-y_min) + y_min
    pred.iloc[:,0] = pred.iloc[:,0]*(y_max-y_min) + y_min

    # Matching runtimes to input features
    totalDF = x.copy(deep=True)
    totalDF['runtime'] = y
    totalDF['pred_runtime'] = pred
    totalDF['node_count'] = round(totalDF['node_count'])

    # Generating queue, 
    jobQueue = None
    for i in range(0,int(numJobs/totalDF.shape[0])):
        jobQueue = pd.concat([jobQueue,totalDF.sample(frac=1,random_state=randomSeed)])
    jobQueue = pd.concat([jobQueue,totalDF.sample(numJobs%totalDF.shape[0], random_state=randomSeed)])

    # Generating synthetic wall times

# Random sampling
# storing run data in csv file for visualization
# make test bench
# job stream simulation, use two d time matrix that jae seung talked about
# 
"""
