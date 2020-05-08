#!/usr/bin/env python3

import json
import argparse
import yaml
import torch
import datetime
import sys
import os
from datetime import datetime, timedelta
import numpy as np
from multiprocessing import Process, Array, Pool
import pandas as pd
import matplotlib
import gc
import subprocess
matplotlib.use('TkAgg')
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator, FormatStrFormatter
from sklearn.decomposition import PCA   

from IPython import embed    
data_dir = "./ans2/"
local_folder= "/Users/gengjinkun/Documents/fork_puffer/export_figs/"
FUTURE_CHUNKS = 5
def reduce_dim(date_str, result_dict):
    reduced_dict = {}
    for key in result_dict:
        reduced_key = key[0:-5]
        if reduced_key not in reduced_dict:
            reduced_dict[reduced_key] = np.zeros(FUTURE_CHUNKS)
        model_index = int(key[-4])
        reduced_dict[reduced_key][model_index] = result_dict[key]
    # for key in reduced_dict:
    #     print(key," ", reduced_dict[key], "\n")
        #reduced_dict[key] = np.mean(reduced_dict[key])
    df = pd.DataFrame(reduced_dict)
    df = df.T
    df.columns=[(date_str+"-"+str(i)) for i in range(FUTURE_CHUNKS) ]
    return df
def pca_analysis(df):
    df = df -df.mean()
    pca = PCA(n_components = 2)
    pca.fit(df)
    [v1, v2] = pca.components_
    df_values = df.values
    x_vals = np.dot(df_values, v1)
    y_vals = np.dot(df_values, v2)
    df.insert(0,'x', x_vals)
    df.insert(1,'y', y_vals)

    bbr_model_types = ["bbr-201902", "bbr-201903","bbr-201904", 
    "bbr-201905","bbr-201906", "bbr-201907","bbr-201908", "bbr-201909", "bbr-201910", "bbr-201911",
    "bbr-201912","bbr-202001", "bbr-202002","bbr-202003"]
    model_types = bbr_model_types
    model_groups = {}
    for model_type in model_types:
        model_groups[model_type] = []
        for idx in df.index:
            if idx.startswith(model_type):
                model_groups[model_type].append(idx)
    x_groups = []
    y_groups = []

    for model_type in model_types:
        x_groups.append(df.loc[model_groups[model_type]]['x'])
        y_groups.append(df.loc[model_groups[model_type]]['y'])
    
    for i in range(1, len(model_types)+1):
        fig_name = local_folder+"pca-bbr-"+str(i)+".png"
        fig,ax = plt.subplots(figsize=(5,5))
        for group_i in range(i):
            ax.scatter(x_groups[group_i], y_groups[group_i], s=10**2, 
                        linewidth=0, label=model_types[group_i])
        ax.legend(model_types)
        num1 = 1
        num2=0
        num3=3
        num4=0
        ax.legend(bbox_to_anchor=(num1, num2), loc=num3, borderaxespad=num4)
        plt.savefig(fig_name, bbox_inches='tight')

def boxplot_for_one_model(df_list_by_model_index, fig_name, fig, ax, title):
    accuracy_rows = [ None for i in range(FUTURE_CHUNKS) ]
    for i in range(FUTURE_CHUNKS):
        accuracy_row = df_list_by_model_index[i].values.tolist()
        accuracy_rows[i] = accuracy_row
    xlabels = [ ("Model-"+str(i)) for i in range(FUTURE_CHUNKS)]
    
    ax.set_ylim(0,1.0)
    ax.set_ylabel("Accuracy")
    ax.set_title(title)
    ax.boxplot(
    x=accuracy_rows,
    labels=xlabels,
    boxprops={'color':'black'},
    flierprops={'marker':'o','color':'black'},#设置异常值属性，点的形状、填充颜色和边框色
    meanprops={'marker':'D'},#设置均值点的属性，点的颜色和形状
    medianprops={"linestyle":'--','color':'orange'}#设置中位数线的属性，线的类型和颜色
    )
    plt.savefig(fig_name, bbox_inches='tight')
    plt.cla()
def draw_boxplot(df, cc):
    df_index_list = df.index.tolist()
    df_column_list = df.columns.tolist()
    df_columns_by_model_index = [ [] for i in range(FUTURE_CHUNKS) ]
    for i in range(FUTURE_CHUNKS):
        for column_name in df_column_list:
            if column_name.endswith(str(i)):
                df_columns_by_model_index[i].append(column_name)
    if not os.path.exists(local_folder+"{cc}-boxplots/".format(cc=cc)):
        os.makedirs(local_folder+"{cc}-boxplots/".format(cc=cc))
    fig,ax = plt.subplots(figsize=(5,5))
    for model_name in df_index_list:
        if model_name.startswith(cc):
            df_row_by_model_name = df.loc[model_name]
            df_list_by_model_index =[]
            for i in range(FUTURE_CHUNKS):
                df_columns_by_model_i = df_row_by_model_name[df_columns_by_model_index[i]]
                df_list_by_model_index.append(df_columns_by_model_i)
            fig_name = local_folder+"{cc}-boxplots/".format(cc=cc)+model_name+"-boxplot.png"
            title = model_name
            boxplot_for_one_model(df_list_by_model_index, fig_name, fig, ax, title)

def draw_average_performance(df):
    df_index_list = df.index.tolist()
    df_column_list = df.columns.tolist()
    df_columns_by_model_index = [ [] for i in range(FUTURE_CHUNKS) ]
    for i in range(FUTURE_CHUNKS):
        for column_name in df_column_list:
            if column_name.endswith(str(i)):
                df_columns_by_model_index[i].append(column_name)
    df_list_by_model_index =[]
    for i in range(FUTURE_CHUNKS):
        df_columns_by_model_i = df[df_columns_by_model_index[i]]
        df_list_by_model_index.append(df_columns_by_model_i)
    average_performance_list = []
    for i in range(FUTURE_CHUNKS):
        average_performance_for_model_i = df_list_by_model_index[i].mean(1)
        average_performance_list.append(average_performance_for_model_i)
    average_performance = pd.concat(average_performance_list,axis=1)
    mean_value = average_performance.mean()
    fig,ax = plt.subplots(figsize=(5,5))
    model_indexes = np.arange(FUTURE_CHUNKS)
    x_labels = [ ("Model-"+str(i)) for i in range(FUTURE_CHUNKS) ]
    ax.bar(x_labels, mean_value, alpha=0.5, width=0.3, color='yellow', edgecolor='red', lw=3)
    for x,y in zip(model_indexes,mean_value):
        plt.text(x+0.05,y+0.02,'%.3f' %y, ha='center',va='bottom')
    ax.set_title("Mean Value of Average Model Accuracy")
    ax.set_ylim(0,1.0)
    ax.set_ylabel("Accuracy")
    fig_name = local_folder+"mean-value-avgerage-perfomrance.png"
    plt.savefig(fig_name, bbox_inches='tight')
    #plt.show()

    fig,ax = plt.subplots(figsize=(5,5))
    median_value = average_performance.median()
    ax.set_title("Median Value of Average Model Accuracy")
    model_indexes = np.arange(FUTURE_CHUNKS)
    x_labels = [ ("Model-"+str(i)) for i in range(FUTURE_CHUNKS) ]
    ax.bar(x_labels, median_value, alpha=0.5, width=0.3, color='yellow', edgecolor='red', lw=3)
    for x,y in zip(model_indexes,median_value):
        plt.text(x+0.05,y+0.02,'%.3f' %y, ha='center',va='bottom')
    ax.set_ylim(0,1.0)
    ax.set_ylabel("Accuracy")
    fig_name = local_folder+"median-value-avgerage-perfomrance.png"
    plt.savefig(fig_name, bbox_inches='tight')
    #plt.show()


def draw_average_performance_scatter(df):
    df_index_list = df.index.tolist()
    df_column_list = df.columns.tolist()
    df_columns_by_model_index = [ [] for i in range(FUTURE_CHUNKS) ]
    for i in range(FUTURE_CHUNKS):
        for column_name in df_column_list:
            if column_name.endswith(str(i)):
                df_columns_by_model_index[i].append(column_name)
    df_list_by_model_index =[]
    for i in range(FUTURE_CHUNKS):
        df_columns_by_model_i = df[df_columns_by_model_index[i]]
        df_list_by_model_index.append(df_columns_by_model_i)
    average_performance_list = []
    for i in range(FUTURE_CHUNKS):
        average_performance_for_model_i = df_list_by_model_index[i].mean(1)
        average_performance_list.append(average_performance_for_model_i)
    average_performance = pd.concat(average_performance_list,axis=1)
    fig,ax = plt.subplots(figsize=(5,5))
    model_indexes = np.arange(FUTURE_CHUNKS)
    x_labels = [ ("Model-"+str(i)) for i in range(FUTURE_CHUNKS) ]
    
    X = np.arange(average_performance.shape[0])

    for model_indx in range(FUTURE_CHUNKS):
        ax.scatter(X, average_performance[model_indx].tolist(), label=x_labels[model_indx])
        fig_name = local_folder+"scatter-avgerage-perfomrance-"+str(model_indx)+".png"
        plt.savefig(fig_name, bbox_inches='tight')

    ax.set_title("Average Accuracy of Each Model")
    ax.set_ylim(0,1.0)
    ax.set_ylabel("Accuracy")
    ax.set_xlabel("Models")
    num1 = 1
    num2=0
    num3=3
    num4=0
    ax.legend(bbox_to_anchor=(num1, num2), loc=num3, borderaxespad=num4)
    fig_name = local_folder+"scatter-avgerage-perfomrance.png"
    plt.savefig(fig_name, bbox_inches='tight')

    plt.show()



def draw_std(df):
    df_index_list = df.index.tolist()
    df_column_list = df.columns.tolist()
    df_columns_by_model_index = [ [] for i in range(FUTURE_CHUNKS) ]
    for i in range(FUTURE_CHUNKS):
        for column_name in df_column_list:
            if column_name.endswith(str(i)):
                df_columns_by_model_index[i].append(column_name)
    df_list_by_model_index =[]
    for i in range(FUTURE_CHUNKS):
        df_columns_by_model_i = df[df_columns_by_model_index[i]]
        df_list_by_model_index.append(df_columns_by_model_i)
    
    
    # ax.set_xlim(0.0, 0.5)
    fig,ax = plt.subplots(figsize=(5,5))
    for i in range(FUTURE_CHUNKS):
        std_list = df_list_by_model_index[i].std().tolist()
        ax.hist(std_list,bins=20, alpha=0.5, label="Model-"+str(i)) 
    ax.set_xlim(0.0, 0.1)
    ax.set_title("Standard Deviation of Model Accuracy")
    #fig_name = local_folder+"hist-std-"+str(i)+".png"
    fig_name = local_folder+"hist-std.png"
    num1 = 1
    num2=0
    num3=3
    num4=0
    ax.legend(bbox_to_anchor=(num1, num2), loc=num3, borderaxespad=num4)
    plt.savefig(fig_name, bbox_inches='tight')



def main():
    dir1 = os.listdir(data_dir)
    df = None
    file_cnt = 0
    for file_name in dir1:
        result_dict = json.load(open(data_dir+"/"+file_name))
        if file_name.endswith("-result"):
            date_str=file_name[0:10]
            df_item = reduce_dim(date_str, result_dict)
            if df is None:
                df = df_item
            else:
                df = df.join(df_item)
            file_cnt += 1
            if file_cnt % 10 == 0:
                print("file_cnt=",file_cnt)


    df = df.sort_index()
    df = df.T
    df = df.sort_index()
    df = df.T

    df_index_list = df.index.tolist()
    df_column_list = df.columns.tolist()
    df_columns_by_model_index = [ [] for i in range(FUTURE_CHUNKS) ]
    for i in range(FUTURE_CHUNKS):
        for column_name in df_column_list:
            if column_name.endswith(str(i)):
                df_columns_by_model_index[i].append(column_name)
    df_list_by_model_index =[]
    for i in range(FUTURE_CHUNKS):
        df_columns_by_model_i = df[df_columns_by_model_index[i]]
        df_list_by_model_index.append(df_columns_by_model_i)

   
    
    # draw_boxplot(df, "cubic")
    #draw_boxplot(df, "bbr")
    # draw_average_performance(df)
    # draw_average_performance_scatter(df)
    draw_std(df)
    embed()
    exit(0)

    bbr_model_types = ["bbr-201902", "bbr-201903","bbr-201904", 
    "bbr-201905","bbr-201906", "bbr-201907","bbr-201908", "bbr-201909", "bbr-201910", "bbr-201911",
    "bbr-201912","bbr-202001", "bbr-202002","bbr-202003"]

    sample_df = None
    
    idx_list = []
    export_folder = "27"
    os.makedirs(local_folder+"/"+export_folder)
    for month in bbr_model_types:
        idx = month+export_folder+"-py"
        idx_list.append(idx)
    
    df_col_list = df.columns.tolist()
    col_lists = [ [] for i in range(FUTURE_CHUNKS) ]
    for i in range(FUTURE_CHUNKS):
        suffix = "-"+str(i)
        for col_name in df_col_list:
            if col_name.endswith(suffix):
                col_lists[i].append(col_name)

    row_reduced_df = df.loc[idx_list]
    sample_df_list = [None for i in range(FUTURE_CHUNKS)]
    for i in range(FUTURE_CHUNKS):
        sample_df_list[i] = row_reduced_df[col_lists[i]]
    for model_idx in range(FUTURE_CHUNKS):
        sample_df = sample_df_list[model_idx]
        sum_categories = sample_df.index 
        for i in range(1, len(sum_categories)+1):
            categories = sum_categories[0:i]
            fig_name = local_folder+"/"+export_folder+"/"+"accuracy_cmp-"+str(model_idx)+"-"+str(i)+".png"
            fig,ax = plt.subplots(figsize=(5,7))
            X = np.arange(len(sample_df.columns))
            for category in categories:
                ax.scatter(X, sample_df.loc[category].tolist(), label=category)
            ax.legend()
            ax.set_ylim(0,1.0)
            ax.set_ylabel("Accuracy")
            ax.set_xlabel("Time Series")
            num1 = 1
            num2=0
            num3=3
            num4=0
            ax.legend(bbox_to_anchor=(num1, num2), loc=num3, borderaxespad=num4)
            plt.savefig(fig_name, bbox_inches='tight')
    


    
   




if __name__ == '__main__':
    main()


# if len(result_dict) < 2529:
#     print(file_name, " ", len(result_dict))
#     subprocess.call("rm -f "+ data_dir+"/"+file_name, shell=True) 