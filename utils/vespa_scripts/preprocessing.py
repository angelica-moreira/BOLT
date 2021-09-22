import os
import sys
import pandas as pd
import numpy as np
from time import time

df_prof = pd.DataFrame()
file_name_prof = "features.csv"

def join_table(file_name: str, path: str, program_name: str, stage: str):
    global df_prof

    file_name_with_path = path + "/" + file_name
    if file_name == file_name_prof:
        df_temp = create_temp_prof_df(file_name_with_path, program_name, stage)
        df_prof = df_prof.append(df_temp, sort=False)
      
    
def create_temp_prof_df(file_name_with_path: str, program_name: str, stage: str) -> pd.DataFrame:
    executed_branches = pd.read_csv(file_name_with_path, na_values=['no info', '.'], low_memory=False)
    executed_branches.columns = executed_branches.columns.str.strip()
    executed_branches.infer_objects()
    executed_branches.replace([np.inf, -np.inf], np.nan)
    executed_branches.dropna(inplace=True)
    executed_branches.insert(0, 'PROGRAM_NAME', program_name)
    
    filtered_columns = ['FS_POSTDOMINATES','FS_DOMINATES','TS_POSTDOMINATES','TS_DOMINATES']
    for column_name in filtered_columns:
        executed_branches = executed_branches[executed_branches[column_name] != -1].copy()

    executed_branches['OPERAND_RA_TYPE'] = executed_branches['OPERAND_RA_TYPE'].replace(-1, 10)
    executed_branches['OPERAND_RB_TYPE'] = executed_branches['OPERAND_RB_TYPE'].replace(-1, 10)

    if stage == '0':
        executed_branches = executed_branches[(executed_branches['COUNT_TAKEN']>0) \
                                              | (executed_branches['COUNT_NOT_TAKEN']>0)]
        
        local_total_branches = (executed_branches['COUNT_TAKEN']+executed_branches['COUNT_NOT_TAKEN'])
        
        total_executed_branches = np.sum(local_total_branches)
        
        normalized_weight = local_total_branches / total_executed_branches
        executed_branches.insert(1, 'NORMALIZED_BRANCH_WEIGHT', normalized_weight)

        taken_prob = (executed_branches['COUNT_TAKEN'] / local_total_branches) * 100
        executed_branches.insert(2, 'PROB_TAKEN', taken_prob)

        not_taken_prob = (executed_branches['COUNT_NOT_TAKEN'] / local_total_branches) * 100
        executed_branches.insert(3, 'PROB_NOT_TAKEN', not_taken_prob)
        
        normalized_weight2 = local_total_branches / executed_branches.shape[0]
        executed_branches.insert(4, 'NORMALIZED_BRANCH_WEIGHT2', normalized_weight2)
        
        executed_branches['PROB_NOT_TAKEN']= executed_branches['PROB_NOT_TAKEN'].round(2)

        executed_branches['PROB_TAKEN']= executed_branches['PROB_TAKEN'].round(2)


    return executed_branches


def generate_csv_file(df, path, file_name):
    df.to_csv(path + '/'+ file_name, encoding='utf-8', index=False, float_format='%.3f')
    print("File: "+file_name+ " persisted at: "+path)
    
    
def read_csv_files(files_dir: str, path: str, stage: str) -> int:
    count = 0
    found = False
    for root, dirs, files in os.walk(files_dir):
        quant_files = len(files)
        for file_name in files:
            if file_name == file_name_prof:
                count += 1
                program_name = os.path.basename(root)
                join_table(file_name, root, program_name, stage)
                found = True
        found = False
    message = str(count) + " file(s) analysed.\n"
    print(message)
    return count


def main():
    
    if len(sys.argv) != 4:
        raise Exception('The correct format is: [path/to/the/folder/where/the/file(s)/is(are)] '
                '[the stage (0 - train or 1 - test)] '
                '[followed by the path/where/to/save/the/result/files]\n '
                'try: python3 ' + sys.argv[0] + ' [folder_path] [stage_code] [result_path]')

    files_dir = sys.argv[1]
    if not os.path.exists(files_dir):
        raise Exception("Error: Directory [" + files_dir + "] does not exist!")

    stage = sys.argv[2]
    if stage != '0' and stage != '1':
        raise Exception("Error: Stage option code [" + stage + "] does not exist!\n"
                "Type 0 to genareate train files or type 1 to generate test files.\n")

    path = sys.argv[3]
    if not os.path.exists(path):
        os.makedirs(path)
    path = os.path.abspath(path)

    num_files = read_csv_files(files_dir, path, stage)
    
    summarized_data_path = os.path.abspath(path+"/summarized_data/")
    if not os.path.exists(summarized_data_path):
        os.makedirs(summarized_data_path)
        
    generate_csv_file(df_prof, summarized_data_path, 'Combined_Data_Stage_' + stage + '.csv')


if __name__ == "__main__":
    main()
