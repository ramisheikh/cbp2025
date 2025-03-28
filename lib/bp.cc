/*

Copyright (c) 2019, North Carolina State University
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. The names “North Carolina State University”, “NCSU” and any trade-name, personal name,
trademark, trade device, service mark, symbol, image, icon, or any abbreviation, contraction or
simulation thereof owned by North Carolina State University must not be used to endorse or promote products derived from this software without prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

// Author: Eric Rotenberg (ericro@ncsu.edu)
// Modified by A. Seznec (andre.seznec@inria.fr) to include TAGE-SC-L predictor and the ITTAGE indirect branch predictor

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <iostream>
#include <numeric>
#include <cstdlib>
#include "sim_common_structs.h"
#include "bp.h"
#include "cbp.h"
#include "parameters.h"

#include "parameters.h"

bp_t::bp_t()
{
   if(!PERFECT_INDIRECT_PRED)
   {
       ITTAGE = new IPREDICTOR();
   }

   // Initialize measurements.
   //meas_conddir_n = 0;
   //meas_conddir_m = 0;
   //meas_jumpdir_n = 0;
   //meas_jumpind_n = 0;
   //meas_jumpind_m = 0;
   //meas_jumpret_n = 0;
   //meas_jumpret_m = 0;
   //meas_notctrl_n = 0;
   //meas_notctrl_m = 0;

   meas_conddir_n_per_epoch.clear();
   meas_conddir_m_per_epoch.clear();
   meas_jumpdir_n_per_epoch.clear();
   meas_jumpind_n_per_epoch.clear();
   meas_jumpind_m_per_epoch.clear();
   meas_jumpret_n_per_epoch.clear();
   meas_jumpret_m_per_epoch.clear();
   meas_notctrl_n_per_epoch.clear();
   meas_notctrl_m_per_epoch.clear();
   meas_cycles_on_wrong_path_per_epoch.clear();
}

bp_t::~bp_t() {
}

// Returns true if instruction is a mispredicted branch.
// Also updates all branch predictor structures as applicable.
bool bp_t::predict(uint64_t seq_no, uint8_t piece, InstClass inst_class, uint64_t pc, uint64_t next_pc, const uint64_t pred_cycle)
{
   bool taken = false;
   bool pred_taken = false;
   uint64_t pred_target;
   bool misp;

   if (inst_class == InstClass::condBranchInstClass)
   {
      // CONDITIONAL BRANCH

      // Determine the actual taken/not-taken outcome.
      taken = (next_pc != (pc + 4));

      // Make prediction.
      //pred_taken= TAGESCL->GetPrediction (pc);
      pred_taken = get_cond_dir_prediction (seq_no, piece, pc, pred_cycle);
      
      // Determine if mispredicted or not.
      misp = (pred_taken != taken);
      
      if(MISP_REDUCTION_PERC != 0 && misp)
      {
          const bool flip_mispred = (MISP_REDUCTION_PERC == 100) ? true : (static_cast<uint64_t>(rand_r(&mispred_correction_seed)%100) < MISP_REDUCTION_PERC);
          if(flip_mispred)
          {
              misp = false;
              pred_taken =  taken;
          }
      }
      
      /* A. Seznec: uodate TAGE-SC-L*/
      //TAGESCL-> UpdatePredictor (pc , 1,  taken, pred_taken, next_pc);
      //UpdateCondDirPredictor (pc , 1,  taken, pred_taken, next_pc);
 
      //// InOrder Update Option
      //spec_update(seq_no, piece, pc, inst_class, taken, pred_taken, next_pc);
      //temp_predictor_update_hook(seq_no, piece, pc, taken,pred_taken, next_pc);
      // OOO Update Option
      spec_update(seq_no, piece, pc, inst_class, taken, pred_taken, next_pc);
      // Update measurements.
      meas_conddir_n_per_epoch.back()++;
      meas_conddir_m_per_epoch.back() += misp;
   }
   else if (inst_class == InstClass::uncondDirectBranchInstClass || inst_class == InstClass::callDirectInstClass) {
      // CALL OR JUMP DIRECT

      // Target of JAL or J (rd=x0) will be available in either the fetch stage (BTB hit)
      // or the pre-decode/decode stage (BTB miss), so these are not predicted.
      // Never mispredicted.
      misp = false;
      
      /* A. Seznec: update branch  histories for TAGE-SC-L and ITTAGE */
      //TAGESCL->TrackOtherInst(pc , 0,  true,next_pc);
      //TrackOtherInst(pc , 0,  true,next_pc);
      spec_update(seq_no, piece, pc, inst_class, true/*taken*/, true/*pred_taken*/, next_pc);
      if(!PERFECT_INDIRECT_PRED)
      {
          ITTAGE->TrackOtherInst(pc , next_pc);
      }

      // Update measurements.
      meas_jumpdir_n_per_epoch.back()++;
   }
   else if (inst_class == InstClass::uncondIndirectBranchInstClass || inst_class == InstClass::callIndirectInstClass || inst_class==InstClass::ReturnInstClass) 
   {
      const bool is_ret = (inst_class == InstClass::ReturnInstClass);
      const bool ind_not_ret = !is_ret;
      meas_jumpind_n_per_epoch.back() += ind_not_ret;
      meas_jumpret_n_per_epoch.back() += is_ret;
      if (PERFECT_INDIRECT_PRED)
      {
          misp = false;
         // Update measurements.
      }
      else
      {
         // Make prediction.
         pred_target= ITTAGE->GetPrediction (pc);

         // Determine if mispredicted or not.
         misp = (pred_target != next_pc);
      
         /* A. Seznec: update ITTAGE*/
         ITTAGE-> UpdatePredictor (pc , next_pc);
      
         // Update measurements.
         meas_jumpind_m_per_epoch.back() += !is_ret && misp;
         meas_jumpret_m_per_epoch.back() += is_ret && misp;
      }

      spec_update(seq_no, piece, pc, inst_class, true/*taken*/, true/*pred_taken*/, next_pc);
      /* A. Seznec: update history for TAGE-SC-L */
      //TAGESCL->TrackOtherInst(pc , 2,  true,next_pc);
      //TrackOtherInst(pc , 2,  true,next_pc);
   }
   else
   {
      // not a control-transfer instruction
      misp = (next_pc != pc + 4);

      // Update measurements.
      meas_notctrl_n_per_epoch.back()++;
      meas_notctrl_m_per_epoch.back()+=misp;
   }

   return(misp);
}

void bp_t::notify_begin_new_epoch()
{
    meas_conddir_n_per_epoch.emplace_back(0);   // # conditional branches
    meas_conddir_m_per_epoch.emplace_back(0);   // # mispredicted conditional branches

    meas_jumpdir_n_per_epoch.emplace_back(0);   // # jumps, direct

    meas_jumpind_n_per_epoch.emplace_back(0);   // # jumps, indirect
    meas_jumpind_m_per_epoch.emplace_back(0);   // # mispredicted jumps, indirect

    meas_jumpret_n_per_epoch.emplace_back(0);   // # jumps, return
    meas_jumpret_m_per_epoch.emplace_back(0);   // # mispredicted jumps, return

    meas_notctrl_n_per_epoch.emplace_back(0);   // # non-control transfer instructions
    meas_notctrl_m_per_epoch.emplace_back(0);   // # non-control transfer instructions for which: next_pc != pc + 4

    meas_cycles_on_wrong_path_per_epoch.emplace_back(0);    // cycles_on_wrong_path
}

void bp_t::update_cycles_on_wrong_path(const uint64_t cycles_on_wrong_path)
{
    meas_cycles_on_wrong_path_per_epoch.back() += cycles_on_wrong_path;
}

#define BP_OUTPUT(str, n, m, i) \
    printf("%s%10ld %10ld %8.4lf%% %8.4lf\n", (str), (n), (m), 100.0*((double)(m)/(double)(n)), 1000.0*((double)(m)/(double)(i)))

void bp_t::output(const uint64_t num_inst)
{
   const uint64_t meas_conddir_n = std::accumulate(meas_conddir_n_per_epoch.begin(), meas_conddir_n_per_epoch.end(), 0);    // # conditional branches
   const uint64_t meas_conddir_m = std::accumulate(meas_conddir_m_per_epoch.begin(), meas_conddir_m_per_epoch.end(), 0);    // # mispredicted conditional branches
                                   
   const uint64_t meas_jumpdir_n = std::accumulate(meas_jumpdir_n_per_epoch.begin(), meas_jumpdir_n_per_epoch.end(), 0);    // # jumps, direct
                                   
   const uint64_t meas_jumpind_n = std::accumulate(meas_jumpind_n_per_epoch.begin(), meas_jumpind_n_per_epoch.end(), 0);    // # jumps, indirect
   const uint64_t meas_jumpind_m = std::accumulate(meas_jumpind_m_per_epoch.begin(), meas_jumpind_m_per_epoch.end(), 0);    // # mispredicted jumps, indirect
                                   
   const uint64_t meas_jumpret_n = std::accumulate(meas_jumpret_n_per_epoch.begin(), meas_jumpret_n_per_epoch.end(), 0);    // # jumps, return
   const uint64_t meas_jumpret_m = std::accumulate(meas_jumpret_m_per_epoch.begin(), meas_jumpret_m_per_epoch.end(), 0);    // # mispredicted jumps, return
                                   
   const uint64_t meas_notctrl_n = std::accumulate(meas_notctrl_n_per_epoch.begin(), meas_notctrl_n_per_epoch.end(), 0);    // # non-control transfer instructions
   const uint64_t meas_notctrl_m = std::accumulate(meas_notctrl_m_per_epoch.begin(), meas_notctrl_m_per_epoch.end(), 0);    // # non-control transfer instructions for which: next_pc != pc + 4

   //const uint64_t meas_cycles_on_wrong_path = std::accumulate(meas_cycles_on_wrong_path_per_epoch.begin(), meas_cycles_on_wrong_path_per_epoch.end(), 0);

   //uint64_t num_misp = (meas_conddir_m + meas_jumpind_m + meas_jumpret_m + meas_notctrl_m);
   printf("\n-----------------------------------------------BRANCH PREDICTION MEASUREMENTS (Full Simulation i.e. Counts Not Reset When Warmup Ends)----------------------------------------------\n");
   printf("Type                   NumBr     MispBr        mr     mpki\n");
   //BP_OUTPUT("All              ", num_inst, num_misp, num_inst);
   BP_OUTPUT("CondDirect       ", meas_conddir_n, meas_conddir_m, num_inst);
   BP_OUTPUT("JumpDirect       ", meas_jumpdir_n, (uint64_t)0, num_inst);
   BP_OUTPUT("JumpIndirect     ", meas_jumpind_n, meas_jumpind_m, num_inst);
   BP_OUTPUT("JumpReturn       ", meas_jumpret_n, meas_jumpret_m, num_inst);
   BP_OUTPUT("Not control      ", meas_notctrl_n, meas_notctrl_m, num_inst);
   printf("------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
}

void bp_t::output_periodic_info(const std::vector<uint64_t>&num_insts_per_epoch, const std::vector<uint64_t>&num_cycles_per_epoch)
{
   assert(num_insts_per_epoch.size() == num_cycles_per_epoch.size());

   {
      const uint64_t target_instr_count = 10000000;
      printf("\n------------------------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (Last 10M instructions)-----------------------------------------------------\n");
      printf("       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI\n");
      uint64_t my_instr_count = 0;
      uint64_t my_cycle_count = 0;
      uint64_t my_br_count = 0;
      uint64_t my_br_mispred_count = 0;
      uint64_t my_wpc_count = 0;
      for(int epoch_index = num_insts_per_epoch.size() -1; epoch_index >= 0; epoch_index--)
      {
           my_instr_count       += num_insts_per_epoch.at(epoch_index);
           my_cycle_count       += num_cycles_per_epoch.at(epoch_index);
           my_br_count          += meas_conddir_n_per_epoch.at(epoch_index);
           my_br_mispred_count  += meas_conddir_m_per_epoch.at(epoch_index);
           my_wpc_count         += meas_cycles_on_wrong_path_per_epoch.at(epoch_index);
           if(my_instr_count > target_instr_count)
           {
               break;
           }
      }
      const double cyc_wp_avg =  (my_br_mispred_count == 0) ? 0.00 : (double)my_wpc_count/(double)my_br_mispred_count;
      const double cyc_wp_pki =  (double)my_wpc_count*1000/(double)my_instr_count;
      printf("%12ld %12ld %8.4f %10ld %10ld %8.4lf %12.4lf %8.4lf%% %8.4lf %10ld %10.4lf %10.4lf\n", my_instr_count, my_cycle_count, (double)my_instr_count/(double)my_cycle_count, my_br_count, my_br_mispred_count, (double)(my_br_count)/(double)(my_cycle_count), (double)(my_br_mispred_count)/(double)(my_cycle_count), 100.0*((double)(my_br_mispred_count)/(double)(my_br_count)), 1000.0*((double)(my_br_mispred_count)/(double)(my_instr_count)), my_wpc_count, cyc_wp_avg, cyc_wp_pki);
      printf("------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
   }

   {
      const uint64_t target_instr_count = 25000000;
      printf("\n------------------------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (Last 25M instructions)-----------------------------------------------------\n");
      printf("       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI\n");
      uint64_t my_instr_count = 0;
      uint64_t my_cycle_count = 0;
      uint64_t my_br_count = 0;
      uint64_t my_br_mispred_count = 0;
      uint64_t my_wpc_count = 0;
      for(int epoch_index = num_insts_per_epoch.size() -1; epoch_index >= 0; epoch_index--)
      {
           my_instr_count       += num_insts_per_epoch.at(epoch_index);
           my_cycle_count       += num_cycles_per_epoch.at(epoch_index);
           my_br_count          += meas_conddir_n_per_epoch.at(epoch_index);
           my_br_mispred_count  += meas_conddir_m_per_epoch.at(epoch_index);
           my_wpc_count         += meas_cycles_on_wrong_path_per_epoch.at(epoch_index);
           if(my_instr_count > target_instr_count)
           {
               break;
           }
      }
      const double cyc_wp_avg =  (my_br_mispred_count == 0) ? 0.00 : (double)my_wpc_count/(double)my_br_mispred_count;
      const double cyc_wp_pki =  (double)my_wpc_count*1000/(double)my_instr_count;
      printf("%12ld %12ld %8.4f %10ld %10ld %8.4lf %12.4lf %8.4lf%% %8.4lf %10ld %10.4lf %10.4lf\n", my_instr_count, my_cycle_count, (double)my_instr_count/(double)my_cycle_count, my_br_count, my_br_mispred_count, (double)(my_br_count)/(double)(my_cycle_count), (double)(my_br_mispred_count)/(double)(my_cycle_count), 100.0*((double)(my_br_mispred_count)/(double)(my_br_count)), 1000.0*((double)(my_br_mispred_count)/(double)(my_instr_count)), my_wpc_count, cyc_wp_avg, cyc_wp_pki);
      printf("-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
   }

   const uint64_t total_instr = std::accumulate(num_insts_per_epoch.begin(), num_insts_per_epoch.end(), 0); // # mispredicted jumps, return
   {
      const uint64_t target_instr_count = total_instr/2;
      printf("\n---------------------------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (50 Perc instructions)---------------------------------------------------\n");
      printf("       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI\n");
      uint64_t my_instr_count = 0;
      uint64_t my_cycle_count = 0;
      uint64_t my_br_count = 0;
      uint64_t my_br_mispred_count = 0;
      uint64_t my_wpc_count = 0;
      for(int epoch_index = num_insts_per_epoch.size() -1; epoch_index >= 0; epoch_index--)
      {
           my_instr_count       += num_insts_per_epoch.at(epoch_index);
           my_cycle_count       += num_cycles_per_epoch.at(epoch_index);
           my_br_count          += meas_conddir_n_per_epoch.at(epoch_index);
           my_br_mispred_count  += meas_conddir_m_per_epoch.at(epoch_index);
           my_wpc_count         += meas_cycles_on_wrong_path_per_epoch.at(epoch_index);
           if(my_instr_count > target_instr_count)
           {
               break;
           }
      }
      const double cyc_wp_avg =  (my_br_mispred_count == 0) ? 0.00 : (double)my_wpc_count/(double)my_br_mispred_count;
      const double cyc_wp_pki =  (double)my_wpc_count*1000/(double)my_instr_count;
      printf("%12ld %12ld %8.4f %10ld %10ld %8.4lf %12.4lf %8.4lf%% %8.4lf %10ld %10.4lf %10.4lf\n", my_instr_count, my_cycle_count, (double)my_instr_count/(double)my_cycle_count, my_br_count, my_br_mispred_count, (double)(my_br_count)/(double)(my_cycle_count), (double)(my_br_mispred_count)/(double)(my_cycle_count), 100.0*((double)(my_br_mispred_count)/(double)(my_br_count)), 1000.0*((double)(my_br_mispred_count)/(double)(my_instr_count)), my_wpc_count, cyc_wp_avg, cyc_wp_pki);
      printf("------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
   }

   {
      const uint64_t total_instr = std::accumulate(num_insts_per_epoch.begin(), num_insts_per_epoch.end(), 0);  // # mispredicted jumps, return
      const uint64_t target_instr_count = total_instr;
      printf("\n-------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (Full Simulation i.e. Counts Not Reset When Warmup Ends)-------------------------------------\n");
      printf("       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI\n");
      uint64_t my_instr_count = 0;
      uint64_t my_cycle_count = 0;
      uint64_t my_br_count = 0;
      uint64_t my_br_mispred_count = 0;
      uint64_t my_wpc_count = 0;
      for(int epoch_index = num_insts_per_epoch.size() -1; epoch_index >= 0; epoch_index--)
      {
           my_instr_count       += num_insts_per_epoch.at(epoch_index);
           my_cycle_count       += num_cycles_per_epoch.at(epoch_index);
           my_br_count          += meas_conddir_n_per_epoch.at(epoch_index);
           my_br_mispred_count  += meas_conddir_m_per_epoch.at(epoch_index);
           my_wpc_count         += meas_cycles_on_wrong_path_per_epoch.at(epoch_index);
           if(my_instr_count > target_instr_count)
           {
               break;
           }
      }
      const double cyc_wp_avg =  (my_br_mispred_count == 0) ? 0.00 : (double)my_wpc_count/(double)my_br_mispred_count;
      const double cyc_wp_pki =  (double)my_wpc_count*1000/(double)my_instr_count;
      printf("%12ld %12ld %8.4f %10ld %10ld %8.4lf %12.4lf %8.4lf%% %8.4lf %10ld %10.4lf %10.4lf\n", my_instr_count, my_cycle_count, (double)my_instr_count/(double)my_cycle_count, my_br_count, my_br_mispred_count, (double)(my_br_count)/(double)(my_cycle_count), (double)(my_br_mispred_count)/(double)(my_cycle_count), 100.0*((double)(my_br_mispred_count)/(double)(my_br_count)), 1000.0*((double)(my_br_mispred_count)/(double)(my_instr_count)), my_wpc_count, cyc_wp_avg, cyc_wp_pki);
      printf("------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
   }


   if(PRINT_PER_EPOCH_STATS)
   {
      printf("EPOCH COUNT  = %lu\n", num_insts_per_epoch.size());
      printf("\n-------------------------------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION PER EPOCH MEASUREMENTS------------------------------------------------------------\n");
      printf("EPOCH       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI\n");
      for(uint64_t epoch_index = 0; epoch_index < num_insts_per_epoch.size(); epoch_index++)
      {
           const uint64_t my_instr_count = num_insts_per_epoch.at(epoch_index);
           const uint64_t my_cycle_count = num_cycles_per_epoch.at(epoch_index);
           const uint64_t my_br_count = meas_conddir_n_per_epoch.at(epoch_index);
           const uint64_t my_br_mispred_count = meas_conddir_m_per_epoch.at(epoch_index);
           const uint64_t my_wpc_count = meas_cycles_on_wrong_path_per_epoch.at(epoch_index);
           const double cyc_wp_avg =  (my_br_mispred_count == 0) ? 0.00 : (double)my_wpc_count/(double)my_br_mispred_count;
           const double cyc_wp_pki =  (double)my_wpc_count*1000/(double)my_instr_count;
           printf("%5ld %12ld %12ld %8.4f %10ld %10ld %8.4lf %12.4lf %8.4lf%% %8.4lf %10ld %10.4lf %10.4lf\n", epoch_index, my_instr_count, my_cycle_count, (double)my_instr_count/(double)my_cycle_count, my_br_count, my_br_mispred_count, (double)(my_br_count)/(double)(my_cycle_count), (double)(my_br_mispred_count)/(double)(my_cycle_count), 100.0*((double)(my_br_mispred_count)/(double)(my_br_count)), 1000.0*((double)(my_br_mispred_count)/(double)(my_instr_count)), my_wpc_count, cyc_wp_avg, cyc_wp_pki);
      }
      printf("------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
   }
}
