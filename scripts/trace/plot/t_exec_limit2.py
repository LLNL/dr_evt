# This script takes the same input file as the other script 'resource_time.py'

import matplotlib.pyplot as plt
import numpy as np
import sys
import pandas as pd
pd.options.display.max_rows = 100000

filename = sys.argv[1]
df = pd.read_csv(filename, header=0, parse_dates=[0], sep="\t", usecols = ['time_limit', 'exec_time', 'queue'])
df2 = df.loc[df['queue'] == 'pbatch', ['time_limit', 'exec_time']]
df2 = df2.astype(float)
df2.sort_values(by = ['time_limit', 'exec_time'], ascending=[True, True], inplace=True)

# Additional filtering
max_batch_time = 12*60*60
grace_period = 120
df2 = df2.loc[df2['exec_time'] < df2['time_limit'] + grace_period, ['time_limit', 'exec_time']]
df2 = df2.loc[df2['time_limit'] < max_batch_time, ['time_limit', 'exec_time']]


X = df2['time_limit'].to_numpy()
Y = df2['exec_time'].to_numpy()
plt.figure(figsize=(5, 5))
plt.scatter(X, Y, s=1)
plt.gca().set_xlim(left=0)
plt.gca().set_ylim(bottom=0)
#plt.gca().set_ylim(top=100000)
plt.grid()
plt.title('Requested timeout vs execution time of a job')
plt.xlabel('Timeout requested (sec)')
plt.ylabel('Execution time (sec)')
plt.savefig("t_exec_limit.pdf", format="pdf", bbox_inches="tight")
plt.savefig("t_exec_limit.png", format="png", bbox_inches="tight")
plt.show()
