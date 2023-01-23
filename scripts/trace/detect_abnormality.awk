#!/bin/awk -f
# Use the filtered output file
# Run this script as
# "./detect_abnormality.awk [-v chk_all=1] [-v show_all=1] filtered_file"

# Filtered file column names
# 1 no.
# 2 num_nodes
# 3 begin_time
# 4 end_time
# 5 submit_time
# 6 time_limit
# 7 wait_time
# 8 exec_time
# 9 busy_nodes
# 10 queue

# Output will add another column at the beginning that indicates any issue
# detected. If there is no issue, then 0 will be displayed. Otherwise, the
# issue tag(s) will be shown, which can be a single numer or multiple
# numbers.
# 
# 1: batch job time limit issue 
# 2: batch job max node request issue
# 3: Number of busy nddes exceeds the total number of nodes for batch queue
# 4: Number of busy nodes exceeds the total number of nodes in the cluster
# 5: job runs longer than the requested time limit

BEGIN {
    FS = "\t"
    batch_time_limit = 43200 # 12 hours, Lassen config
    batch_j_max_nodes = 256 # Lassen config
    batch_q_max_nodes = 756 # Lassen config
    total_nodes = 795 # Lassen setup
    grace_time = 180 # my pick
}

(NR == 1) {
    print "tag\t" $0
    if (chk_all) {
        chk_bt_lim = 1;  # tag 1
        chk_bjn_max = 1; # tag 2
        chk_bqn_max = 1; # tag 3
        chk_n_max = 1    # tag 4
        chk_t_lim = 1    # tag 5
    }
}

(NR>1) {
    tag = ""
    # Check against the maximum allocation time a batch job can request
    # (requested_time_limit > batch_time_limit) && (queue == "pbatch") ?
    if (chk_bt_lim && ($6 > batch_time_limit) && ($10 != "pall")) {
        tag = tag "1"
    }
    # Check against the maximum node allocation a batch job can request
    # (num_nodes > batch_j_max_nodes) && (queue == "pbatch")
    if (chk_bjn_max && ($2 > batch_j_max_nodes) && ($10 != "pall")) {
        tag = tag "2"
    }
    # Check against the maximum number of nodes a batch job can request
    # If all the jobs running are batch jobs, this should not happen.
    # If some batch jobs are preempted and DAT starts, it might happen.
    # (busy_nodes > batch_q_max_nodes) && (queue == "pbatch")
    if (chk_bqn_max && ($9 > batch_q_max_nodes) && ($10 != "pall")) {
        tag = tag "3"
    }
    # Check against the total nodes available on Lassen
    # (busy_nodes > total_nodes) ?
    if (chk_n_max && ($9 > total_nodes)) {
        tag = tag "4"
    }
    # Check if the job time limit has been violated
    # (requested_time_limit < exec_time) ?
    if (chk_t_liim && ($6 + grace_time < $8)) {
        tag = tag "5"
    }

    if (tag != "") {
        print tag "\t" $0
    } else if (show_all) {
        print "0\t" $0
    }
}
