import os
import sys
import pandas as pd
import numpy as np
import tensorflow as tf
from tensorflow.python.client import device_lib 
from joblib import dump, load

def generate_vespa_probfile(df_data, file_path, bin_name, model_path):
  scaler_vespa = load(model_path+'/vespa_scaler.joblib') 
  model_vespa = tf.keras.models.load_model(model_path+'/vespa')

  categorical_features=['DIRECTION','LOOP_HEADER',
                    'PROCEDURE_TYPE', 'OPERAND_RA_TYPE','OPERAND_RB_TYPE', 
                    'TS_LOOP_HEADER','TS_BACKEDGE','TS_EXIT', 
                    'FS_LOOP_HEADER','FS_BACKEDGE', 
                    'TS_CALL','FS_CALL','LOCAL_EXITING_BLOCK',
                    'FS_EXIT', 'HAS_INDIRECT_CALLS', 'HAS_SELF_CALLS',
                    'LOCAL_LATCH_BLOCK', 'LOCAL_LOOP_HEADER',
                    'HAS_FS_INDIRECT_CALL', 'HAS_TS_INDIRECT_CALL', 
                    'OPCODE','CMP_OPCODE', 'TS_END_OPCODE', 'FS_END_OPCODE']

  numerical_features=['DELTA_TAKEN', 'LOOP_DEPTH']

  vespa_cat = categorical_features+numerical_features

  df_data['HAS_INDIRECT_CALLS'] = np.where(df_data['NUM_INDIRECT_CALLS']>0, 1, 0)
  df_data['HAS_SELF_CALLS'] = np.where(df_data['NUM_SELF_CALLS']>0, 1, 0)
  df_data['HAS_TS_INDIRECT_CALL'] = np.where(df_data['TS_NUM_INDIRECT_CALL']>0, 1, 0)
  df_data['HAS_FS_INDIRECT_CALL'] = np.where(df_data['FS_NUM_INDIRECT_CALL']>0, 1, 0)

  X_vespa = df_data[vespa_cat].copy()
  if len(numerical_features) > 0:
    df_tmp = X_vespa[numerical_features].copy()
    df_tmp.loc[:,numerical_features] = scaler_vespa.transform(df_tmp[numerical_features])
    X_vespa[numerical_features] = df_tmp
    del df_tmp
  
  y_predicted_vespa = model_vespa.predict(dict(X_vespa))
  df_data['predito'] = y_predicted_vespa.round(3).copy()
  dumpFile(df_data, file_path, bin_name)


def dumpFile(df_data, output_dir, bin_name):
  df_data.to_csv("output_data.csv", encoding='utf-8')
  df_ts = pd.DataFrame()
  df_ts['FROM_FUN_NAME'] = df_data['TS_FROM_FUN_NAME'].copy()
  df_ts['FROM_BB'] = df_data['TS_FROM_BB'].copy()
  df_ts['TO_FUN_NAME'] = df_data['TS_TO_FUN_NAME'].copy()
  df_ts['TO_BB'] = df_data['TS_TO_BB'].copy()
  df_ts['PROB_TAKEN'] = df_data['predito'] * 100000000000
  df_ts['FUN_ENTRY_ADDRESS'] = df_data['FUN_ENTRY_ADDRESS'].astype(object).copy()
  df_ts['FUN_ENTRY_FREQUENCY'] = df_data['FUN_ENTRY_FREQUENCY'].astype(object).copy()

  df_fs = pd.DataFrame()
  df_fs['FROM_FUN_NAME'] = df_data['FS_FROM_FUN_NAME'].copy()
  df_fs['FROM_BB'] = df_data['FS_FROM_BB'].copy()
  df_fs['TO_FUN_NAME'] = df_data['FS_TO_FUN_NAME'].copy()
  df_fs['TO_BB'] = df_data['FS_TO_BB'].copy()
  df_fs['PROB_TAKEN'] = (100000000000 - df_ts['PROB_TAKEN'])
  df_fs['FUN_ENTRY_ADDRESS'] = df_data['FUN_ENTRY_ADDRESS'].astype(object).copy()
  df_fs['FUN_ENTRY_FREQUENCY'] = df_data['FUN_ENTRY_FREQUENCY'].astype(object).copy()


  df_result = pd.concat([df_ts,df_fs])
  df_result.dropna(inplace=True)
  df_result = df_result.sort_values(by=['FUN_ENTRY_ADDRESS', 'FROM_BB'], ascending=True).reset_index(drop=True)
  df_result['PROB_TAKEN'] = df_result['PROB_TAKEN'].round(0).astype(int)

  grouped = df_result.groupby(by=['FROM_FUN_NAME',"FUN_ENTRY_ADDRESS","FUN_ENTRY_FREQUENCY"])

  output_file_name = (bin_name+"_ml.pdata" if not bin_name=="" else "prob.pdata")
  file_path = output_dir+"/"+output_file_name
  os.path.isfile(file_path)
  if(os.path.isfile(file_path)):
      os.remove(file_path)
      
  for name, group in grouped:
      f = open(file_path, "a")
      print('FUNCTION',name[0], name[1], name[2], file=f)
      f.close()
      
      df_aux = group.copy()
      df_aux['EDGE'] = 'EDGE'
      df_aux = df_aux[['EDGE', 'FROM_BB', 'TO_BB', 'PROB_TAKEN']]
      df_aux.to_csv(file_path,sep=' ',encoding='utf-8', index=False, float_format='%.3f',mode = 'a', header = False)
      
      f = open(file_path, "a")
      print('END', file=f)
      f.close()

  print("The probabilities file was saved at: "+file_path)

def main():
  print(len(sys.argv))
  if len(sys.argv) != 4 and len(sys.argv) != 5:
      raise Exception('The correct format is: [path/to/the/folder/where/the/models/are] '
                  '[path/to/where/the/database/is] '
                  '[path/to/the/folder/where/the/output/will/be/saved] '
                  '[binary name] <optional> \n'
                  'try: python3 ' + sys.argv[0] + ' [model_path] [data_base_path] [output_path] [bin_name]')

  model_path = sys.argv[1]
  if not os.path.exists(model_path):
      raise Exception("Error: File [" + model_path + "] does not exist!")
  model_path = os.path.abspath(model_path)

  file_path = sys.argv[2]
  file_name_with_path = os.path.abspath(file_path) + '/Combined_Data_Stage_1.csv'
  if not os.path.exists(file_name_with_path):
      raise Exception("Error: File [" + file_name_with_path + "] does not exist!")

  df_data = pd.read_csv(file_name_with_path, na_values=['no info', '.'], low_memory=False)

  output_path = sys.argv[3]
  output_path = os.path.abspath(output_path)
  bin_name = (sys.argv[4] if len(sys.argv) == 5 else "")
  generate_vespa_probfile(df_data, output_path, bin_name, model_path)

if __name__ == "__main__":
    main()
