# Training VESPA with your own dataset

### Step 0

Do not forget to generate your binaries with relocations if you intend to perform
function rearrangement. Although it is not advisable performing this optimization with
the static inferred predictors presented currently in this project, due to some regressions
observed when performing inteprocedural optimizations in

See [Step 0](https://github.com/angelica-moreira/BOLT/blob/master/README.md#step-0) from Standard BOLT Usage section.

### Step 1: Collect Profile

See [Step 1](https://github.com/angelica-moreira/BOLT/blob/master/README.md#step-1-collect-profile) from Standard BOLT Usage section.

### Step 2:  Convert Profile to BOLT Format

See [Step 2](https://github.com/angelica-moreira/BOLT/blob/master/README.md#step-2-convert-profile-to-bolt-format) from Standard BOLT Usage section.

### Step 3: Extract Code Features

In this step we will generate the csv feature file (`features.csv`), where its branch has dynamic
counter data attached to it. To perform it you must do as follows:

```
$ llvm-bolt <executable> -data=perf.fdata -gen-features
```

### Step 4: Train VESPA predictor
First we will run the preprocessing script with option 1, since we want to generate a summarization
of our raw input data that will be used for training the ML model. Now that you have the condensed dataset of
your rwa input dataset, you can use it to perform the training in the current VESPA ml model implementation, like as follows:

```
$ VESPA_SCRIPTS_HOME=${LLVM_BOLT_DIRECTORY}/utils/vespa_scripts/
$ source ${VIRTUAL_ENV_DIR}/vespa-env/bin/activate
$ PYTHON_HOME=${VIRTUAL_ENV_DIR}/vespa-env/bin
$ ${PYTHON_HOME}/python3 ${VESPA_SCRIPTS_HOME}/preprocessing.py <features_path> <stage_code> <result_path>
$ ${PYTHON_HOME}/python3 ${VESPA_SCRIPTS_HOME}/python3 ./vespa_scripts/generate_vespa.py <data_base_path> <output_model_path>
$ deactivate
```

Note: You must always make sure you have the python environment activated when running our python scripts
a description of how you can create and install all the required dependencies in this virtual environment
can be found at [Step 2](https://github.com/angelica-moreira/BOLT/blob/master/README.md#step-2-generate-inferred-profile) from BOLT + VESPA Usage.

The input variable features_path represents the path where the features.csv is, the stage_code can assume values 0 (train) or
1 (prediction), and result_path is the path where the preprocessed csv file will be persisted. The input variable
`output_model_pathis` the path where vespa ml model will be persisted is, and the data_base_path represents the path where the
preprocessed csv file was persisted. 
