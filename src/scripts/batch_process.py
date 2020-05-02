import sys
import argparse
from os import path
import subprocess
import shlex
import time
import numpy as np
import matplotlib
import matplotlib.pyplot as plt
from multiprocessing import Process, Array, Pool



def execute_cmd(fresh_size, old_size, model, fresh_data_source, test_type, suffix):
    if old_size == 0:
        cmd = ("python ttp_local3.py --train-and-test --fresh-data {fresh_data_source} ".format(fresh_data_source=fresh_data_source)
            + "--fresh-size {fresh_size}  ".format(fresh_size=fresh_size)
            +"--test-data /mnt/disks/mnt_dir/training_data/2020-01-17-1 "
            +"--load-model {model} ".format(model = model)
            +"--save-model {suffix}-{fresh_size}-{test_type}-{old_size}-old.pt --epoch-number 500".format(fresh_size=fresh_size, test_type=test_type, old_size=old_size, suffix = suffix) )
    else:
        cmd = ("python ttp_local3.py --train-and-test --fresh-data {fresh_data_source} ".format(fresh_data_source=fresh_data_source)
            + "--fresh-size {fresh_size}  --old-data old-data-sample --old-size {old_size} ".format(fresh_size=fresh_size, old_size=old_size)
            +"--test-data /mnt/disks/mnt_dir/training_data/2020-01-17-1 "
            +"--load-model {model} ".format(model = model)
            +"--save-model {suffix}-{fresh_size}-{test_type}-{old_size}-old.pt --epoch-number 500".format(fresh_size=fresh_size, test_type=test_type, old_size=old_size, suffix = suffix) )
    output_res = subprocess.call(cmd, shell=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--normal-data-source', dest='normal_data_source')
    parser.add_argument('--pos-data-source', dest='pos_data_source')
    parser.add_argument('--neg-data-source', dest='neg_data_source')
    parser.add_argument('--model', dest='model')
    parser.add_argument('--num-processes', dest='num_processes', type=int)
    parser.add_argument('--step-size', dest='step_size', type=int)
    parser.add_argument('--suffix', dest='suffix')

    # normal_data_source =  "20200116-sample-100000"
    # pos_data_source = "20200116-sample-100000-pos"
    # neg_data_source = "20200116-sample-100000-neg"
    # model = "model-20200101-20200115-100000/py-1.pt"
    # #model = "bbr-20200202-1/py-1.pt"
    # num_processes = 16
    # step_size =1000
    # suffix =  "model-20200101-20200115-10000-incre-10000"
    pool = Pool(processes= args.num_processes)
    results = []
    
    for fresh_size in range(args.step_size, 11*args.step_size, args.step_size):
        old_size = 10*args.step_size - fresh_size   
        results.append(pool.apply_async(execute_cmd, args=(fresh_size, old_size, args.model, args.normal_data_source, "normal", args.suffix )))
    for fresh_size in range(args.step_size, 11* args.step_size, args.step_size):
        old_size = 10*args.step_size - fresh_size   
        results.append(pool.apply_async(execute_cmd, args=(fresh_size, old_size, args.model, args.pos_data_source, "pos", args.suffix)))
    for fresh_size in range(step_size, 11*step_size, step_size):
        old_size = 10*step_size - fresh_size   
        results.append(pool.apply_async(execute_cmd, args=(fresh_size, old_size, args.model, args.neg_data_source, "neg", args.suffix )))
              
    for result in results:
        result.get()



if __name__ == '__main__':
    main()
