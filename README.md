# cbp2025
Championship Branch Prediction 2025

## Build
The simulator expects a branch predictor to be implemented in the files my_cond_branch_predictor.{h/cc}. The predictor gets statically instantiated. For reference, check sample predictor file: [my_cond_branch_predictor.h](./my_cond_branch_predictor.h)

To build the simulator:

`make clean && make`

## Branch Predictor Interface

The simulator interacts with the branch predictor via the following interfaces:
* beginCondDirPredictor - Intended for any predictor initialization steps.
* notify_instr_fetch - Called when an instruction is fetched.
* get_cond_dir_prediction - invoke the predictor to get the prediction of the relevant branch. This is called only for conditional branches.
* spec_update - Intended to help update the predictor's history (GHR/LHIST ..etc.) This is called for all branches right after a prediction is made.
* notify_instr_decode - Called when an instruction is decoded.
* notify_agen_complete - Called when agen of a load/store instruction completes.
* notify_instr_execute_resolve - Called when any instruction is executed.
* notify_instr_commit - Called when any instruction is committed.
* endCondDirPredictor - Called at the end of simulation to allow contestants to dump any additional state.

These interfaces get exercised as the instruction flows through the cpu pipeline, and they provide the contestants with the relevant state available at that pipeline stage. The interfaces are defined in [cbp.h](./cbp.h) and must remain unchanged. The structures exposed via the interfaces are defined in [sim_common_structs.h](lib/sim_common_structs.h). This includes InstClass, DecodeInfo, ExecuteInfo ..etc.

See [cbp.h](./cbp.h) and [cond_branch_predictor_interface.cc](./cond_branch_predictor_interface.cc) for more details.

### Contestant Developed Predictor

The simulator comes with CBP2016 winner([64KB Tage-SC-L](./cbp2016_tage_sc_l.h)) as the conditional branch predictor. Contestants may retain the Tage-SC-L and add upto 128KB of additional prediction components, or discard it and use the entire 192KB for their own components. Contestants are also allowed to update tage-sc-l implementation.
Contestants are free to update the implementation within [cond_branch_predictor_interface.cc](./cond_branch_predictor_interface.cc) as long as they keep the branch predictor interfaces (listed above) untouched. E.g., they can modify the file to combine the predictions from the cbp2016 tage-sc-l and their own developed predictor.

In a processor, it is typical to have a structure that records prediction-time information that can be used later to update the predictor once the branch resolves. In the provided Tage-SC-L implementation, the predictor checkpoints history in an STL map(pred_time_histories) indexed by instruction id to serve this purpose. At update time, the same information is retrieved to update the predictor.
For the predictors developed by the contestants, they are free to use a similar approach. The amount of state needed to checkpoint histories will NOT be counted towards the predictor budget. For any questions, contestants are encouraged to email the CBP2025 Organizing Committee.

## Examples
See Simulator options:

`./cbp`

Running the simulator on `trace.gz`:

`./cbp trace.gz`

Running with periodic misprediction stats at every 1M instr(`-E <n>`)

`./cbp -E 1000000 trace.gz`

## Notes

Run `make clean && make` to ensure your changes are taken into account.

Sample traces are provided : [sample_traces](./sample_traces)

Script to run all traces and dump a csv is also provided : [trace_exec_training_list](scripts/trace_exec_training_list.py)

Reference results from the training set are included here : [reference_results](reference_results_training_set.csv)

To run the script, update the trace_folder and results dir inside the script and run:

`python trace_exec_training_list.py  --trace_dir sample_traces/ --results_dir  sample_results`

The script executes all the traces inside the trace directory and creates a directory structure with the logs similar to thr trace-directory with all the logs.

The script also parses all the logs to dump a csv with relevant stats.

## Getting Traces

[Link to Training Set- 105 traces](https://drive.google.com/drive/folders/10CL13RGDW3zn-Dx7L0ineRvl7EpRsZDW)

Contestants can also use 'gdown' do download the traces:

`pip install gdown`

`gdown --folder //drive.google.com/drive/folders/10CL13RGDW3zn-Dx7L0ineRvl7EpRsZDW`

To untar the traces:

`tar -xvf foo.tar.xz`

### Data Dependent conditional branch characterization

[Data Dependent Conditional Branch Characterization](https://ericrotenberg.wordpress.ncsu.edu/files/2025/02/CBP2025-data-dependent-branch-profiles.pdf) for the training traces is available. This may be leveraged to pursue interesting directions for the branch predictor design.

## Sample Output Per Run

```
WINDOW_SIZE = 1024
FETCH_WIDTH = 16
FETCH_NUM_BRANCH = 16
FETCH_STOP_AT_INDIRECT = 1
FETCH_STOP_AT_TAKEN = 1
FETCH_MODEL_ICACHE = 1
PERFECT_BRANCH_PRED = 0
PERFECT_INDIRECT_PRED = 1
PIPELINE_FILL_LATENCY = 10
NUM_LDST_LANES = 8
NUM_ALU_LANES = 16
MEMORY HIERARCHY CONFIGURATION---------------------
STRIDE Prefetcher = 1
PERFECT_CACHE = 0
WRITE_ALLOCATE = 1
Within-pipeline factors:
  AGEN latency = 1 cycle
  Store Queue (SQ): SQ size = window size, oracle memory disambiguation, store-load forwarding = 1 cycle after store's or load's agen.
  * Note: A store searches the L1$ at commit. The store is released
  * from the SQ and window, whether it hits or misses. Store misses
  * are buffered until the block is allocated and the store is
  * performed in the L1$. While buffered, conflicting loads get
  * the store's data as they would from the SQ.
I$: 128 KB, 8-way set-assoc., 64B block size
L1$: 128 KB, 8-way set-assoc., 64B block size, 3-cycle search latency
L2$: 4 MB, 8-way set-assoc., 64B block size, 12-cycle search latency
L3$: 32 MB, 16-way set-assoc., 128B block size, 50-cycle search latency
Main Memory: 150-cycle fixed search time
---------------------------STORE QUEUE MEASUREMENTS (Full Simulation i.e. Counts Not Reset When Warmup Ends)---------------------------
Number of loads: 417260
Number of loads that miss in SQ: 239412 (57.38%)
Number of PFs issued to the memory system 5411
---------------------------------------------------------------------------------------------------------------------------------------
------------------------MEMORY HIERARCHY MEASUREMENTS (Full Simulation i.e. Counts Not Reset When Warmup Ends)-------------------------
I$:
  accesses   = 1313295
  misses     = 44
  miss ratio = 0.00%
  pf accesses   = 0
  pf misses     = 0
  pf miss ratio = -nan%
L1$:
  accesses   = 582033
  misses     = 79
  miss ratio = 0.01%
  pf accesses   = 5411
  pf misses     = 58
  pf miss ratio = 1.07%
L2$:
  accesses   = 123
  misses     = 123
  miss ratio = 100.00%
  pf accesses   = 58
  pf misses     = 58
  pf miss ratio = 100.00%
L3$:
  accesses   = 123
  misses     = 89
  miss ratio = 72.36%
  pf accesses   = 58
  pf misses     = 28
  pf miss ratio = 48.28%
---------------------------------------------------------------------------------------------------------------------------------------
----------------------------------------------Prefetcher (Full Simulation i.e. No Warmup)----------------------------------------------
Num Trainings :417260
Num Prefetches generated :5432
Num Prefetches issued :14837
Num Prefetches filtered by PF queue :52
Num untimely prefetches dropped from PF queue :21
Num prefetches not issued LDST contention :9426
Num prefetches not issued stride 0 :139934
---------------------------------------------------------------------------------------------------------------------------------------

-------------------------------ILP LIMIT STUDY (Full Simulation i.e. Counts Not Reset When Warmup Ends)--------------------------------
instructions = 997741
cycles       = 193123
CycWP        = 61660
IPC          = 5.1663

---------------------------------------------------------------------------------------------------------------------------------------

-----------------------------------------------BRANCH PREDICTION MEASUREMENTS (Full Simulation i.e. Counts Not Reset When Warmup Ends)----------------------------------------------
Type                   NumBr     MispBr        mr     mpki
CondDirect           111265       1140   1.0246%   0.8680
JumpDirect            26868          0   0.0000%   0.0000
JumpIndirect              1          0   0.0000%   0.0000
JumpReturn            10589          0   0.0000%   0.0000
Not control         1164572          0   0.0000%   0.0000
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

------------------------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (Last 10M instructions)-----------------------------------------------------
       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI
      997741       193123   5.1663     111265       1140   0.5761       0.0059   1.0246%   1.1426      61660    54.0877    61.7996
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

------------------------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (Last 25M instructions)-----------------------------------------------------
       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI
      997741       193123   5.1663     111265       1140   0.5761       0.0059   1.0246%   1.1426      61660    54.0877    61.7996
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

---------------------------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (50 Perc instructions)---------------------------------------------------
       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI
      997741       193123   5.1663     111265       1140   0.5761       0.0059   1.0246%   1.1426      61660    54.0877    61.7996
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

-------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (Full Simulation i.e. Counts Not Reset When Warmup Ends)-------------------------------------
       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI
      997741       193123   5.1663     111265       1140   0.5761       0.0059   1.0246%   1.1426      61660    54.0877    61.7996
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 Read 997741 instrs 

```


## Sample Output if using python script for all the traces:

```
python scripts/trace_exec_training_list.py  --trace_dir ./sample_traces/ --results_dir  sample_results
Got 2 traces
Begin processing run:fp/sample_fp_trace
Begin processing run:int/sample_int_trace
Extracting data from : sample_results/fp/sample_fp_trace.log |  WL:fp | Run:sample_fp_trace
Extracting data from : sample_results/int/sample_int_trace.log |  WL:int | Run:sample_int_trace
  Workload               Run  TraceSize Status            ExecTime   Instr  Cycles     IPC   NumBr MispBr BrPerCyc MispBrPerCyc       MR    MPKI  CycWP  CycWPAvg CycWPPKI 50PercInstr 50PercCycles 50PercIPC 50PercNumBr 50PercMispBr 50PercBrPerCyc 50PercMispBrPerCyc 50PercMR 50PercMPKI 50PercCycWP 50PercCycWPAvg 50PercCycWPPKI
0       fp   sample_fp_trace   1.178618   Pass   6.901926040649414  997741  193123  5.1663  111265   1140   0.5761       0.0059  1.0246%  1.1426  61660   54.0877  61.7996      997741       193123    5.1663      111265         1140         0.5761             0.0059  1.0246%     1.1426       61660        54.0877        61.7996
1      int  sample_int_trace   1.334165   Pass  5.5803234577178955  997301  338310  2.9479  128874    264   0.3809       0.0008  0.2049%  0.2647  33348  126.3182  33.4382      997301       338310    2.9479      128874          264         0.3809             0.0008  0.2049%     0.2647       33348       126.3182        33.4382


----------------------------------Aggregate Metrics Per Workload Category----------------------------------

WL:fp         Branch Misprediction PKI(BrMisPKI) AMean : 1.1426
WL:fp         Cycles On Wrong-Path PKI(CycWpPKI) AMean : 61.7996
WL:int        Branch Misprediction PKI(BrMisPKI) AMean : 0.2647
WL:int        Cycles On Wrong-Path PKI(CycWpPKI) AMean : 33.4382
-----------------------------------------------------------------------------------------------------------


---------------------------------------------Aggregate Metrics---------------------------------------------

Branch Misprediction PKI(BrMisPKI) AMean : 0.70365
Cycles On Wrong-Path PKI(CycWpPKI) AMean : 47.6189
-----------------------------------------------------------------------------------------------------------
```
