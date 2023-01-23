# This scripts take as an input the output file from dr_event with
# LIMIT_VS_EXEC_TIME_ONLY optiont set at compile time.
# The option is changeable in common.hpp

import matplotlib.pyplot as plt
import numpy as np
import sys

filename = sys.argv[1]
f = open(filename)
header = f.readline()
f.close()

#colname = header.split('\t')
#print(colname[0], colname[1])

X, Y = np.loadtxt(filename, delimiter='\t', unpack=True, skiprows=1)

plt.scatter(X, Y, s=1)
plt.title('From Lassen job history')
plt.xlabel('Job time limit (sec)')
plt.ylabel('Execution time (sec)')
plt.savefig("t_exec_limit.pdf", format="pdf", bbox_inches="tight")
plt.savefig("t_exec_limit.png", format="png", bbox_inches="tight")
plt.show()

sys.exit()
