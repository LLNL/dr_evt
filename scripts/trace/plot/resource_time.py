import matplotlib.pyplot as plt
import numpy as np
import sys
import pandas as pd
pd.options.display.max_rows = 100000

filename = sys.argv[1]
df = pd.read_csv(filename, header=0, parse_dates=[0], sep="\t", usecols = ['submit_time', 'busy_nodes', 'queue'])
df2 = df.loc[df['queue'] == 'pbatch', ['submit_time', 'busy_nodes']]
#df2 = df2.loc[df2['busy_nodes'] <= 756, ['submit_time', 'busy_nodes']]
X = df2['submit_time']
Y = df2['busy_nodes']

plt.figure(figsize=(15, 5))
plt.plot_date(X, Y, linewidth=0.3, linestyle='-', marker=' ')
plt.gca().set_ylim(bottom=0)
plt.grid()
plt.title('Number of nodes in use at the moment by other jobs from pbatch queue on Lassen')
plt.xlabel('Job submit time')
plt.ylabel('Number of nodes in use')
plt.savefig("resource_time.pdf", format="pdf", bbox_inches="tight")
plt.savefig("resource_time.png", format="png", bbox_inches="tight")
plt.show()
