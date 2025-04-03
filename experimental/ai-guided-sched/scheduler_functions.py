import pandas as pd
import numpy as np

def backfill(df, timeAvail, nodesAvail, jobStack, globalClock, jobTracker, use_config = False, use_pr = False, use_perfect_pr=False):
    # Query job queue to find valid jobs
    if use_pr:
        validDf = df[df['pred_runtime'] <= timeAvail]
    elif use_perfect_pr:
        validDf = df[df['runtime'] <= timeAvail]
    else:
        validDf = df[df['wall_time'] <= timeAvail]

    # Additional Query to make sure job has been submitted to queue yet
    validDf = validDf[validDf['submit_time'] <= globalClock]

    # Use user submitted node count, or use best config available
    if use_config:
        validDf = validDf[validDf['config_node_count'] <= nodesAvail]
    else:
        validDf = validDf[validDf['node_count'] <= nodesAvail]
    
    while(validDf.shape[0] > 0):
        # Schedule Job
        jobItem = {}
        jobItem['start'] = globalClock
        if use_config:
            jobItem['runtime'] = validDf.iloc[0,]['config_runtime'] 
            jobItem['nodes'] = validDf.iloc[0,]['config_node_count']
        else:
            jobItem['runtime'] = validDf.iloc[0,]['runtime'] 
            jobItem['nodes'] = validDf.iloc[0,]['node_count']
        jobItem['end'] = jobItem['start'] + jobItem['runtime']
        jobItem['wall_time'] = validDf.iloc[0,]['wall_time'] + jobItem['start']
        jobItem['status'] = "backfill"
        jobItem['id'] = validDf.iloc[0,].name + 1
        jobItem['submit_time'] = validDf.iloc[0,]['submit_time']
        if(jobItem['end'] < jobItem['submit_time']):
            pass
           # print(f"BACKFILL GRRJob End = {jobItem['end']}, Job Submit = {jobItem['submit_time']}")
        jobItem['tracker'] = 'Backfill'
        jobStack.append(jobItem) # Keeps track of all running jobs
        nodesAvail -= jobItem['nodes']
        jobTracker[jobItem['id']] = validDf.iloc[0,].copy(deep=True)
        df.drop(validDf.iloc[0,].name, axis="index", inplace=True) # Drop scheudled job from job queue
        # Query job queue again 
        if use_pr:
            validDf = df[df['pred_runtime'] <= timeAvail]
        elif use_perfect_pr:
            validDf = df[df['runtime'] <= timeAvail]
        else:
            validDf = df[df['wall_time'] <= timeAvail]
        if use_config:
            validDf = validDf[validDf['config_node_count'] <= nodesAvail]
        else:
            validDf = validDf[validDf['node_count'] <= nodesAvail]
        # Additional Query to make sure job has been submitted to queue yet
        validDf = validDf[validDf['submit_time'] <= globalClock]

    # Return nodes available after backfill to dynamically keep track of nodes available in outer loop
    return nodesAvail

def frontfill(df, nodes, globalClock, currentWallTime, jobStack, jobTracker, use_config):
    while (not use_config) and (not df.empty) and (df.iloc[0,]['node_count'] <= nodes) and (df.iloc[0,]['submit_time'] <= globalClock):
        currentJob = df.iloc[0,].copy(deep=True)
        jobItem = {}
        jobItem['start'] = globalClock
        jobItem['runtime'] = currentJob['runtime'] 
        jobItem['nodes'] = currentJob['node_count']
        jobItem['end'] = jobItem['start'] + jobItem['runtime']
        jobItem['wall_time'] = df.iloc[0,]['wall_time'] + jobItem['start']
        jobItem['status'] = "standard"
        jobItem['id'] = df.iloc[0,].name + 1
        if currentWallTime < jobItem['wall_time']:
            currentWallTime = jobItem['wall_time']
            currentWallTimeId = jobItem['id']
        
        jobItem['submit_time'] = currentJob['submit_time']
        if(jobItem['end'] < jobItem['submit_time']):
            print(f"Job End = {jobItem['end']}, Job Submit = {jobItem['submit_time']}")
        jobItem['tracker'] = 'frontfill - stock'
        jobStack.append(jobItem) 
        nodes -= jobItem['nodes']
        jobTracker[jobItem['id']] = currentJob
        df.drop(df.iloc[0,].name, axis="index", inplace=True)

    while use_config and (not df.empty) and (df.iloc[0,]['config_node_count'] <= nodes) and (df.iloc[0,]['submit_time'] <= globalClock):
        currentJob = df.iloc[0,].copy(deep=True)
        #print(f"Start - {globalClock}, Submit - {currentJob['submit_time']} ")
        jobItem = {}
        jobItem['start'] = globalClock
        jobItem['runtime'] = currentJob['config_runtime'] 
        jobItem['nodes'] = currentJob['config_node_count']
        jobItem['end'] = jobItem['start'] + jobItem['runtime']
        jobItem['wall_time'] = df.iloc[0,]['wall_time'] + jobItem['start']
        jobItem['status'] = "standard"
        jobItem['id'] = df.iloc[0,].name + 1
        if currentWallTime < jobItem['wall_time']:
            currentWallTime = jobItem['wall_time']
            currentWallTimeId = jobItem['id']
        jobItem['submit_time'] = currentJob['submit_time']
        if(jobItem['end'] < jobItem['submit_time']):
            print(f"Job End = {jobItem['end']}, Job Submit = {jobItem['submit_time']}")
        jobItem['tracker'] = 'frontfill - config'
        jobStack.append(jobItem) 
        nodes -= jobItem['nodes']
        jobTracker[jobItem['id']] = currentJob
        df.drop(df.iloc[0,].name, axis="index", inplace=True)

    return nodes, currentWallTime   

def schedule(df, nodes, use_config = False, use_pr=False, use_perfect_pr = False):
    globalClock = 0.0
    startingNumJobs = df.shape[0]
    print(f"Num jobs - {startingNumJobs}")
    avgWaitTime = 0
    jobTracker = {}
    jobStack = [] # used for backfill
    jobLists = [] # used for visualization (can ignore)
    currentWallTime = 0 # Find the largest wall time available for backfill
    currentWallTimeId = 0 # Find the largest wall time id available for backfill
    while not df.empty:
        # At this state, no jobs can be backfilled, and all jobs previously scheduled have finished running
        # Schedule as many jobs as you can, do not consider backfill window yet

        # Normal Scheduling
        nodes, currentWallTime = frontfill(df, nodes, globalClock, currentWallTime, jobStack, jobTracker, use_config)
        
        # Start Backfilling
        newWindowTime = currentWallTime-globalClock # Time available to backfilll
        nodes = backfill(df, newWindowTime, nodes, jobStack, globalClock, jobTracker, use_config, use_pr, use_perfect_pr)

        # Sort job stack by end time, we can then pop the jobs in order of completion
        jobStack = sorted(jobStack, key = lambda x: x['end'], reverse=True)
        
        # Creating the backfill bar for visualization ( can ignore )
        # tempJob = jobStack[0].copy()
        # tempJob['nodes'] = "BACKFILL WINDOW"
        # tempJob['status'] = "wall"
        # tempJob['end'] = currentWallTime
        # tempJob['id'] *= -1
        #jobLists.append(tempJob)
        if (len(jobStack) == 0):
            #print("hereereljrlerjelkrjl")
            globalClock = df.iloc[0,]['submit_time']
        # Run Jobs & Continue to Backfill as resources are freed
        lastSubmitTime = 0
        while(len(jobStack) > 0):
            # Reallocate resources after quickest job finishes 
            currJob = jobStack.pop()
            lastSubmitTime = currJob['submit_time']
            nodes += currJob['nodes']
            # Run backfill again with new window 
            newWindowTime = currentWallTime-currJob['end']
            # checking if job went over backfill window 
            if newWindowTime > 0:
                globalClock = currJob['end']
                jobLists.append(currJob)
                if currJob['end'] < currJob['submit_time']:
                    print(f"start - {currJob['start']}, submit - {currJob['submit_time']}, from {currJob['tracker']}")
                    pass
                avgWaitTime += currJob['end'] - currJob['submit_time']
                nodes, currentWallTime = frontfill(df, nodes, globalClock, currentWallTime, jobStack, jobTracker, use_config)
                nodes = backfill(df, newWindowTime, nodes, jobStack, globalClock, jobTracker, use_config, use_pr, use_perfect_pr)
            else:
                # add a 10% increase in time
                print("ERORR HERE PERHAPS")
                globalClock = currentWallTime
                df = pd.concat([df,jobTracker[currJob['id']]], ignore_index=True)
            jobStack = sorted(jobStack, key = lambda x: x['end'], reverse=True)
            
    avgWaitTime /= startingNumJobs 
    print(f"Scheduling finished with runtime of {globalClock} seconds. The average turnaround time was {avgWaitTime} seconds.")
    print(f"The diff in submit times was {lastSubmitTime} seconds")
    return globalClock, jobLists, avgWaitTime 
