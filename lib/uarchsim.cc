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


#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sstream>
#include <assert.h>
//#include "cbp.h"
#include "value_predictor_interface.h"
#include "trace_reader.h"
#include "fifo.h"
#include "cache.h"
#include "bp.h"
#include "cbp.h"
#include "resource_schedule.h"
#include "uarchsim.h"
#include "parameters.h"

//uarchsim_t::uarchsim_t():window(WINDOW_SIZE),
uarchsim_t::uarchsim_t()
      :window_capacity(WINDOW_SIZE)
      ,L3(L3_SIZE, L3_ASSOC, L3_BLOCKSIZE, L3_LATENCY, (cache_t *)NULL)
      ,L2(L2_SIZE, L2_ASSOC, L2_BLOCKSIZE, L2_LATENCY, &L3)
      ,L1(L1_SIZE, L1_ASSOC, L1_BLOCKSIZE, L1_LATENCY, &L2)
      ,BP()
      ,IC(IC_SIZE, IC_ASSOC, IC_BLOCKSIZE, 0, &L2) 
{
   assert(WINDOW_SIZE != 0);
   //assert(FETCH_WIDTH);

   //setup logger
   // Set this to "spdlog::level::debug" for verbose debug prints
   spdlog::set_level(spdlog::level::info);
   spdlog::set_pattern("[%l]  %v");

   assert(NUM_LDST_LANES > 0);
   assert(NUM_ALU_LANES > 0);
   ldst_lanes = ((NUM_LDST_LANES > 0) ? (new resource_schedule(NUM_LDST_LANES)) : ((resource_schedule *)NULL));
   alu_lanes = ((NUM_ALU_LANES > 0) ? (new resource_schedule(NUM_ALU_LANES)) : ((resource_schedule *)NULL));

   for (int i = 0; i < RFSIZE; i++)
      RF[i] = 0;

   num_fetched = 0;
   num_fetched_branch = 0;
   fetch_cycle = 0;

   num_inst = 0;
   num_uop = 0;
   cycle = 0;

   num_insts_per_epoch.clear();
   num_cycles_per_epoch.clear();
   last_epoch_end_cycle = 0;
   end_current_begin_new_epoch(true/*first_epoch*/, false/*last_epoch*/, 0/*epoch_end_cycle*/);
 
   // CVP measurements
   num_eligible = 0;
   num_correct = 0;
   num_incorrect = 0;

   // stats
   num_load = 0;
   num_load_sqmiss = 0;
}

uarchsim_t::~uarchsim_t() {
}

void uarchsim_t::end_current_begin_new_epoch(const bool first_epoch, const bool last_epoch, const uint64_t epoch_end_cycle)
{
    if(!first_epoch)
    {
        assert(epoch_end_cycle > last_epoch_end_cycle);
        // update cycles for the previous epoch
        num_cycles_per_epoch.back() = epoch_end_cycle - last_epoch_end_cycle;
    }

    last_epoch_end_cycle = epoch_end_cycle;

    if(!last_epoch)
    {
        // begin new epoch
        num_insts_per_epoch.emplace_back(0);
        num_cycles_per_epoch.emplace_back(0);
        BP.notify_begin_new_epoch();
    }
}

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) > (b)) ? (b) : (a))

PredictionRequest uarchsim_t::get_value_prediction_req_for_track(uint64_t cycle, uint64_t seq_no, uint8_t piece, db_t *inst)
{
   PredictionRequest req;
   req.seq_no = seq_no;
   req.pc = inst->pc;
   req.piece = piece;
   req.cache_hit = HitMissInfo::Invalid;


   switch(VPTracks(VP_TRACK)){
   case VPTracks::ALL:
         req.is_candidate = true;
         break;
   case VPTracks::LoadsOnly:
         req.is_candidate = inst->is_load;
         break;
   case VPTracks::LoadsOnlyHitMiss:
   {
         req.is_candidate = inst->is_load;
     
         if(req.is_candidate)
         {
            req.cache_hit = HitMissInfo::Miss;
            uint64_t exec_cycle = get_load_exec_cycle(inst);
            if(L1.is_hit(exec_cycle, inst->addr))
            {
               req.cache_hit = HitMissInfo::L1DHit;
            }
            else if(L2.is_hit(exec_cycle, inst->addr))
            {
               req.cache_hit = HitMissInfo::L2Hit;
            }
            else if(L3.is_hit(exec_cycle, inst->addr))
            {
               req.cache_hit = HitMissInfo::L3Hit;
            }
         }
         break;
   }
   default:
         assert(false && "Invalid Track\n");
         break;
   }
   return req;
}

uint64_t uarchsim_t::get_load_exec_cycle(db_t *inst) const
{
   uint64_t exec_cycle = fetch_cycle;

   // No need to re-access ICache because fetch_cycle has already been updated    
   exec_cycle = exec_cycle + PIPELINE_FILL_LATENCY;

   if (inst->A.valid) {
      assert(inst->A.log_reg < RFSIZE);
      if(inst->A.log_reg != RFZERO)
      {
          exec_cycle = MAX(exec_cycle, RF[inst->A.log_reg]);
      }
   }
   if (inst->B.valid) {
      assert(inst->B.log_reg < RFSIZE);
      if(inst->A.log_reg != RFZERO)
      {
          exec_cycle = MAX(exec_cycle, RF[inst->B.log_reg]);
      }
   }
   if (inst->C.valid) {
      assert(inst->C.log_reg < RFSIZE);
      if(inst->A.log_reg != RFZERO)
      {
          exec_cycle = MAX(exec_cycle, RF[inst->C.log_reg]);
      }
   }

   if (ldst_lanes) exec_cycle = ldst_lanes->try_schedule(exec_cycle);

   // AGEN takes 1 cycle.
   exec_cycle = (exec_cycle + 1);

   return exec_cycle;
}

void uarchsim_t::populate_exec_info(db_t *inst) 
{
    _current_execute_info.reset();

    populate_decode_info(inst);
    _current_execute_info.dec_info = _current_decode_info;

    if(is_br(inst->insn_class))
    {
        const bool branch_taken = inst->is_taken;
        if(!is_cond_br(inst->insn_class))
        {
            assert(branch_taken);
        }
        _current_execute_info.taken.emplace(branch_taken);
        //_current_execute_info.taken_target.emplace(inst->next_pc);
    }
    _current_execute_info.next_pc = inst->next_pc;

    if(inst->is_load || inst->is_store)
    {
        _current_execute_info.mem_va.emplace(inst->addr);
        _current_execute_info.mem_sz.emplace(inst->size);
    }

    if (inst->D.valid)
    {
        assert(inst->D.log_reg < RFSIZE);
        _current_execute_info.dst_reg_value.emplace(inst->D.value);
    }
}

void uarchsim_t::populate_decode_info(db_t *inst) 
{
    _current_decode_info.reset();
    _current_decode_info.insn_class = inst->insn_class;

    if (inst->A.valid) {
        assert(inst->A.log_reg < RFSIZE);
        _current_decode_info.src_reg_info.push_back(inst->A.log_reg);
    }
    if (inst->B.valid) {
        assert(inst->B.log_reg < RFSIZE);
        _current_decode_info.src_reg_info.push_back(inst->B.log_reg);
    }
    if (inst->C.valid) {
        assert(inst->C.log_reg < RFSIZE);
        _current_decode_info.src_reg_info.push_back(inst->C.log_reg);
    }

    // Anything to do if inst->D.log_reg != RFFLAGS
    if (inst->D.valid)
    {
        assert(inst->D.log_reg < RFSIZE);
        _current_decode_info.dst_reg_info.emplace(inst->D.log_reg);
    }
}

const window_t& uarchsim_t::locate_entry_in_window(uint64_t seq_no, uint8_t piece) const
{
    const auto window_entry_it = std::lower_bound(window.begin(), window.end(), seq_no, [](const auto& window_entry, const uint64_t _my_seq)
    {
        return window_entry.seq_no < _my_seq;
    });
    assert(window_entry_it != window.end());
    assert(window_entry_it->seq_no == seq_no);
    assert(window_entry_it->piece == piece);
    return *window_entry_it;
}


#if 0
void uarchsim_t::step(db_t *inst) 
{
   spdlog::debug("Stepping, FC: {}",fetch_cycle);
   inst->printInst(fetch_cycle);
}
#endif

#if 1

////////////////////////
// Manage DQ
////////////////////////
void uarchsim_t::eval_decode(std::ostream& activity_trace, bool& activity_observed, const uint64_t current_cycle) 
{
   if(!DQ.empty())
   {
        bool process_dq = true;
        while(process_dq)
        {
            auto dq_it = DQ.begin();
            const auto [seq_no, piece, decode_cycle] = *dq_it;
            assert(current_cycle <= decode_cycle);
            if(current_cycle == decode_cycle)
            {
                const auto& window_entry = locate_entry_in_window(seq_no, piece);
                assert(decode_cycle == window_entry.decode_cycle);
                notify_instr_decode(window_entry.seq_no, window_entry.piece, window_entry.PC, window_entry.exec_info.dec_info, current_cycle);
                DQ.pop_front();
                process_dq = !DQ.empty();
            }
            else
            {
                process_dq = false;

            }
        }
   }
}

////////////////////////
// Manage AGEN
////////////////////////
void uarchsim_t::eval_aq(std::ostream& activity_trace, bool& activity_observed, const uint64_t current_cycle) 
{
   auto aq_it = AQ.begin();
   while(aq_it != AQ.end())
   {
       const auto [seq_no, piece, agen_cycle] = *aq_it;
       assert(current_cycle <= agen_cycle);
       if(current_cycle == agen_cycle)
       {
           const auto& window_entry = locate_entry_in_window(seq_no, piece);
           assert(is_mem(window_entry.exec_info.dec_info.insn_class));
           assert(current_cycle > window_entry.decode_cycle);
           assert(current_cycle <= window_entry.exec_cycle);
           notify_agen_complete(window_entry.seq_no, window_entry.piece, window_entry.PC, window_entry.exec_info.dec_info, window_entry.exec_info.mem_va.value(), window_entry.exec_info.mem_sz.value(), current_cycle);
           activity_trace<<current_cycle<<"::AGEN:"<<window_entry<<"\n";
           activity_observed = true;
           aq_it = AQ.erase(aq_it);
       }
       else
       {
           ++aq_it;
       }
   }
}


////////////////////////
// Manage Execute
////////////////////////
void uarchsim_t::eval_exec(std::ostream& activity_trace, bool& activity_observed, const uint64_t current_cycle) 
{
   auto eq_it = EQ.begin();
   while(eq_it != EQ.end())
   {
       const auto [seq_no, piece, exec_cycle] = *eq_it;
       assert(current_cycle <= exec_cycle);
       if(current_cycle == exec_cycle)
       {
           const auto& window_entry = locate_entry_in_window(seq_no, piece);
           assert(window_entry.exec_cycle == exec_cycle);
           notify_instr_execute_resolve(window_entry.seq_no, window_entry.piece, window_entry.PC, window_entry.pred_taken, window_entry.exec_info, current_cycle);
           activity_trace<<current_cycle<<"::Executed:"<<window_entry<<"\n";
           activity_observed = true;
           eq_it = EQ.erase(eq_it);
       }
       else
       {
           ++eq_it;
       }
   }
}

/////////////////////////////
// Manage window: retire.
/////////////////////////////
void uarchsim_t::eval_retire(std::ostream& activity_trace, bool& activity_observed, const uint64_t current_cycle) 
{
   while (!window.empty() && (current_cycle >= window.front().retire_cycle)) {
      //window_t w = window.pop();
      window_t w = window.front();
      activity_trace<<current_cycle<<"::Retired:"<<w<<"\n";
      activity_observed = true;

      //window.pop();
      window.pop_front();
      notify_instr_commit(w.seq_no, w.piece, w.PC, w.pred_taken, w.exec_info, current_cycle);
      if (VP_ENABLE && !VP_PERFECT)
         updatePredictor(w.seq_no, w.addr, w.value, w.latency);
   }
}

void uarchsim_t::step(db_t *inst) 
{
   spdlog::debug("Stepping, FC: {}",fetch_cycle);
   bool activity_observed = false;
   std::ostringstream activity_trace;

   // Preliminary step: determine which piece of the instruction this is.
   static uint8_t piece = UINT8_MAX;
   //static uint64_t prev_pc = 0xdeadbeef;
   piece = (piece == UINT8_MAX) ? 0 : (piece + 1);
   //prev_pc = inst->pc;

   assert(previous_fetch_cycle <= fetch_cycle);
   // advancing the pipe for the cycles skipped due to mispred/flush etc
   if(previous_fetch_cycle != fetch_cycle)
   {
       uint64_t temp_fetch_cycle = previous_fetch_cycle;
       while(temp_fetch_cycle <= fetch_cycle)
       {
           eval_decode(activity_trace, activity_observed, temp_fetch_cycle);
           eval_aq(activity_trace, activity_observed, temp_fetch_cycle);
           eval_exec(activity_trace, activity_observed, temp_fetch_cycle);
           eval_retire(activity_trace, activity_observed, temp_fetch_cycle);
           temp_fetch_cycle++;
       }
   }

 
   // CVP variables
   uint64_t seq_no = num_uop;
   bool predictable = (inst->D.valid && (inst->D.log_reg != RFFLAGS));
   PredictionResult pred;
   uint64_t latency = 0;
   // 
   // Schedule the instruction's execution cycle.
   //
   uint64_t i;
   uint64_t addr;

   if (FETCH_MODEL_ICACHE)
   {
      const uint64_t next_fetch_cycle = IC.access(fetch_cycle, true/*read*/, inst->pc);   // Note: I-cache hit latency is "0" (above), so fetch cycle doesn't increase on hits.
      assert(next_fetch_cycle >= fetch_cycle);
      // advancing the pipe for the cycles skipped due to L1I$ miss
      if(next_fetch_cycle != fetch_cycle)
      {
          uint64_t temp_fetch_cycle = fetch_cycle;
          while(temp_fetch_cycle <= next_fetch_cycle)
          {
              eval_decode(activity_trace, activity_observed, temp_fetch_cycle);
              eval_aq(activity_trace, activity_observed, temp_fetch_cycle);
              eval_exec(activity_trace, activity_observed, temp_fetch_cycle);
              eval_retire(activity_trace, activity_observed, temp_fetch_cycle);
              temp_fetch_cycle++;
          }
          fetch_cycle = next_fetch_cycle;
      }
   }

   // Predict at fetch time
   if (VP_ENABLE)
   {
      if (VP_PERFECT)
      {
         PredictionRequest req = get_value_prediction_req_for_track(fetch_cycle, seq_no, piece, inst);
         pred.predicted_value = inst->D.value;
         pred.speculate = predictable && req.is_candidate;
         predictable &= req.is_candidate;
      }
      else
      {
         PredictionRequest req = get_value_prediction_req_for_track(fetch_cycle, seq_no, piece, inst);
         pred = getPrediction(req);
         speculativeUpdate(seq_no, predictable, ((predictable && pred.speculate && req.is_candidate) ? ((pred.predicted_value == inst->D.value) ? 1 : 0) : 2),
                           inst->pc, inst->next_pc, static_cast<uint8_t>(inst->insn_class), piece,
                           (inst->A.valid ? inst->A.log_reg : 0xDEADBEEF),
                           (inst->B.valid ? inst->B.log_reg : 0xDEADBEEF),
                           (inst->C.valid ? inst->C.log_reg : 0xDEADBEEF),
                           (inst->D.valid ? inst->D.log_reg : 0xDEADBEEF));
         // Override any predictor attempting to predict an instruction that is not candidate.
         pred.speculate &= req.is_candidate;
         predictable &= req.is_candidate;
      }
   }
   else {
      pred.speculate = false;
   }
 
   uint64_t exec_cycle = fetch_cycle + PIPELINE_FILL_LATENCY;

   // instr src register readiness
   if (inst->A.valid) {
      assert(inst->A.log_reg < RFSIZE);
      if(inst->A.log_reg != RFZERO)
      {
          exec_cycle = MAX(exec_cycle, RF[inst->A.log_reg]);
      }
   }
   if (inst->B.valid) {
      assert(inst->B.log_reg < RFSIZE);
      if(inst->A.log_reg != RFZERO)
      {
          exec_cycle = MAX(exec_cycle, RF[inst->B.log_reg]);
      }
   }
   if (inst->C.valid) {
      assert(inst->C.log_reg < RFSIZE);
      if(inst->A.log_reg != RFZERO)
      {
          exec_cycle = MAX(exec_cycle, RF[inst->C.log_reg]);
      }
   }

   // Schedule an execution lane. -> earliest an execution lane is available
   if (inst->is_load || inst->is_store) {
      if (ldst_lanes) exec_cycle = ldst_lanes->schedule(exec_cycle);
   }
   else 
   {
      if (alu_lanes) exec_cycle = alu_lanes->schedule(exec_cycle);
   }

   const uint64_t agen_cycle = is_mem(inst->insn_class) ? (exec_cycle + 1) : UINT64_MAX;

   if (inst->is_load) {
     
      latency = exec_cycle; // record start of execution

      // AGEN takes 1 cycle.
      exec_cycle = (exec_cycle + 1);

      // Train the prefetcher when the load finds out its outcome in the L1D
      if (PREFETCHER_ENABLE)
      {
         // Generate prefetches ahead of time as in "Effective Hardware-Based Data Prefetching for High-Performance Processors"
         // Instruction PC will be 4B aligned.
         prefetcher.lookahead((inst->pc >> 2), fetch_cycle);

         // Train the prefetcher 
         const bool hit = L1.is_hit(exec_cycle, inst->addr);
         PrefetchTrainingInfo info{inst->pc >> 2, inst->addr, 0, hit};
         prefetcher.train(info);
      }

      // Search D$ using AGEN's cycle.
      uint64_t data_cache_cycle;
      if (PERFECT_CACHE)
         data_cache_cycle = exec_cycle + L1_LATENCY;
      else
         data_cache_cycle = L1.access(exec_cycle, true/*read*/, inst->addr);

      // Search of SQ takes 1 cycle after AGEN cycle.
      exec_cycle = (exec_cycle + 1);

      bool inc_sqmiss = false;
      uint64_t temp_cycle = 0;
      for (i = 0, addr = inst->addr; i < inst->size; i++, addr++) {
         if ((SQ.find(addr) != SQ.end()) && (exec_cycle < SQ[addr].ret_cycle)) {
            // SQ hit: the byte's timestamp is the later of load's execution cycle and store's execution cycle
            temp_cycle = MAX(temp_cycle, MAX(exec_cycle, SQ[addr].exec_cycle));
         }
         else {
            // SQ miss: the byte's timestamp is its availability in L1 D$
            temp_cycle = MAX(temp_cycle, data_cache_cycle);
            inc_sqmiss = true;
         }
      }

      num_load++;                   // stat
      num_load_sqmiss += (inc_sqmiss ? 1 : 0);      // stat

      assert(temp_cycle >= exec_cycle);
      exec_cycle = temp_cycle;

      latency = (exec_cycle - latency); // end of execution minus start of execution
      assert(latency >= 2); // 2 cycles if all bytes hit in SQ
   }
   else {
      // Determine the fixed execution latency based on ALU type.
      if (inst->insn_class == InstClass::fpInstClass)
         latency = FP_EXEC_LATENCY;
      else if (inst->insn_class == InstClass::slowAluInstClass)
         latency = SLOW_ALU_EXEC_LATENCY;
      else
         latency = DEFAULT_EXEC_LATENCY;

      // Account for execution latency.
      exec_cycle += latency;
   }

   activity_observed = true;

   // Drain prefetches from PF Queue
   // The idea is that a prefetch can go only if there is a free LDST slot "this" cycle
   // Here, "this" means all the cycles between the previous fetch cycle and the current one since all fetched ld/st will have been
   // scheduled and prefetch can correctly "steal" ld/st slots.
   if(PREFETCHER_ENABLE)
   {
      uint64_t tmp_previous_fetch_cycle;
      Prefetch p;
      bool issued;
      while(prefetcher.issue(p, fetch_cycle))
      {
         tmp_previous_fetch_cycle = MAX(previous_fetch_cycle, p.cycle_generated);
         issued = false;
         while(tmp_previous_fetch_cycle <= fetch_cycle)
         {
            spdlog::debug("Issuing prefetch:{}", p);
            uint64_t cycle_pf_exec = tmp_previous_fetch_cycle;

            if(ldst_lanes) cycle_pf_exec = ldst_lanes->schedule(cycle_pf_exec, 0);

            if(cycle_pf_exec != MAX_CYCLE)
            {
               L1.access(cycle_pf_exec, true, p.address, true);
               ++stat_pfs_issued_to_mem;
               issued = true;
               break;
            }
            else
            {
               tmp_previous_fetch_cycle++;
               spdlog::debug("Could not find empty LDST slot for PF this cycle, increasing");
            }
         }
         
         if(!issued)
         {
            prefetcher.put_back(p);
            break;
         }
      }
   }

   // Update the instruction count and simulation cycle (max. completion cycle among all scheduled instructions).
   num_uop += 1;
   num_inst += inst->is_last_piece;
   cycle = MAX(cycle, exec_cycle);

   // Update destination register timestamp.
   bool squash = false;
   if (inst->D.valid) {
      assert(inst->D.log_reg < RFSIZE);
      //if ((inst->D.log_reg != RFFLAGS) && (inst->D.log_reg != RFZERO)) 
      if (inst->D.log_reg != RFZERO)
      {
         squash = (pred.speculate && (pred.predicted_value != inst->D.value));         
         RF[inst->D.log_reg] = ((pred.speculate && (pred.predicted_value == inst->D.value)) ? fetch_cycle : exec_cycle);
         activity_observed = true;
      }
   }

   // Update SQ byte timestamps.
   if (inst->is_store) {
      uint64_t data_cache_cycle;
      if (!WRITE_ALLOCATE || PERFECT_CACHE)
         data_cache_cycle = exec_cycle;
      else
         data_cache_cycle = L1.access(exec_cycle, true, inst->addr);

      uint64_t ret_cycle = MAX(data_cache_cycle, (window.empty() ? 0 : window.back().retire_cycle));
      for (i = 0, addr = inst->addr; i < inst->size; i++, addr++) {
         SQ[addr].exec_cycle = exec_cycle;
         SQ[addr].ret_cycle = ret_cycle;
      }
   }

   // CVP measurements
   num_eligible += (predictable ? 1 : 0);
   num_correct += ((predictable && pred.speculate && !squash) ? 1 : 0);
   num_incorrect += ((predictable && pred.speculate && squash) ? 1 : 0);

   /////////////////////////////
   // Manage window: dispatch.
   /////////////////////////////
   //window.push({MAX(exec_cycle, (window.empty() ? 0 : window.peektail().retire_cycle)),
   //            seq_no,
   //            ((inst->is_load || inst->is_store) ? inst->addr : 0xDEADBEEF),
   //            ((inst->D.valid && (inst->D.log_reg != RFFLAGS)) ? inst->D.value : 0xDEADBEEF),
     //      latency});
   //window_t (uint64_t _seq_no, uint64_t _PC, uint64_t _fetch_cycle, uint64_t _decode_cycle, uint64_t _exec_cycle, ExecuteInfo _exec_info, uint64_t _retire_cycle, uint64_t _addr, uint64_t _value, uint64_t _latency)
   const uint64_t decode_cycle = fetch_cycle+DQ_LATENCY;
   populate_exec_info(inst);
   assert(fetch_cycle < exec_cycle);
   const uint64_t predict_cycle = fetch_cycle;
   window.push_back({seq_no,
               piece,
               inst->pc,
               fetch_cycle,
               decode_cycle,
               exec_cycle,
               _current_execute_info,
               MAX(exec_cycle, (window.empty() ? 0 : window.back().retire_cycle)), //retire_cycle
               ((inst->is_load || inst->is_store) ? inst->addr : 0xDEADBEEF), // addr
               ((inst->D.valid && (inst->D.log_reg != RFFLAGS)) ? inst->D.value : 0xDEADBEEF), //value
           latency}); //latency
   activity_trace<<fetch_cycle<<"::Fetched:"<<window.back()<<" Inst:"<<*inst<<"\n";
   activity_observed = true;
   assert(window.size() <= window_capacity);

   notify_instr_fetch(seq_no, piece, inst->pc, fetch_cycle);

   DQ.push_back(std::make_tuple(seq_no, piece, decode_cycle));
   if(is_mem(inst->insn_class))
   {
       AQ.push_back(std::make_tuple(seq_no, piece, agen_cycle));
       assert(AQ.size() <= window_capacity);
   }
   EQ.push_back(std::make_tuple(seq_no, piece, exec_cycle));

   /////////////////////////////
   // Manage fetch cycle.
   /////////////////////////////
   previous_fetch_cycle = fetch_cycle;
   assert(window.front().retire_cycle > fetch_cycle);

   const bool is_branch = is_br(inst->insn_class);
   if (squash) // control dependency on the retire cycle of the value-mispredicted instruction
   {            
      num_fetched = 0;          // new fetch bundle
      //assert(!window.empty() && (fetch_cycle < window.peektail().retire_cycle));
      //fetch_cycle = window.peektail().retire_cycle;
      assert(!window.empty() && (fetch_cycle < window.back().retire_cycle));
      fetch_cycle = window.back().retire_cycle;
   }
   else if (window.size() == window_capacity) 
   {
      if (fetch_cycle < window.front().retire_cycle) 
      {
         num_fetched = 0;       // new fetch bundle
         fetch_cycle = window.front().retire_cycle;
      }
   }
   else {               // fetch bundle constraints
       bool stop = false;

       // Finite fetch bundle.
       if (FETCH_WIDTH > 0) 
       {
           num_fetched += inst->is_last_piece;
           if (num_fetched == FETCH_WIDTH)
           {
               stop = true;
           }
       }

       // Finite branch throughput.
       if ((FETCH_NUM_BRANCH > 0) && is_branch) 
       {
           num_fetched_branch++;
           if (num_fetched_branch == FETCH_NUM_BRANCH)
           {
               stop = true;
           }
       }

       // Indirect branch constraint.
       if (FETCH_STOP_AT_INDIRECT && is_uncond_ind_br(inst->insn_class))
       {
           stop = true;
       }

       // Taken branch constraint.
       if(FETCH_STOP_AT_TAKEN && inst->is_taken)
       {
           const bool taken_branch = (is_cond_br(inst->insn_class) && (inst->next_pc != (inst->pc + 4))) || is_uncond_br(inst->insn_class);
           if(!taken_branch)
           {
               std::cout<<activity_trace.str()<<std::endl;
               std::cout<<"FailingInstr"<<*inst<<std::endl;
           }
           assert(taken_branch);
           stop = true;
       }

       if (stop) 
       {
           // new fetch bundle
           num_fetched = 0;
           num_fetched_branch = 0;
           fetch_cycle++;
       }
   }
   // Account for the effect of a mispredicted branch on the fetch cycle.
   // TODO:: capture taken_target
   bool br_mispred = false;
   if (!PERFECT_BRANCH_PRED && BP.predict(seq_no, piece, inst->insn_class, inst->pc, inst->next_pc, predict_cycle))
   {
       br_mispred = true;
       // setting fetched/fetched_branch for the next cycle
       num_fetched = 0;
       num_fetched_branch = 0;
       fetch_cycle = MAX(fetch_cycle, exec_cycle);
       assert(fetch_cycle > predict_cycle);
       cycles_on_wrong_path += (fetch_cycle - predict_cycle);
       BP.update_cycles_on_wrong_path(fetch_cycle - predict_cycle);
   }

   if(is_branch)
   {
       bool predicted_taken = false;
       if(is_cond_br(inst->insn_class))
       {
           predicted_taken = br_mispred ? !_current_execute_info.taken.value() : _current_execute_info.taken.value();
       }
       else
       {
           predicted_taken = true;
           if(!_current_execute_info.taken.value())
           {
               std::cout<<"About to assert!"<<std::endl;
               inst->printInst(fetch_cycle);
           }
           assert(_current_execute_info.taken.value());
       }
       window.back().update_pred_taken(predicted_taken);
   }

   spdlog::debug("Updating base_cycle to {}", MIN(fetch_cycle, prefetcher.get_oldest_pf_cycle()));

   // Attempt to advance the base cycles of resource schedules.
   // Note : We may have some prefetches to issue still that are older than the fetch cycle.
   if (ldst_lanes) ldst_lanes->advance_base_cycle(MIN(fetch_cycle, prefetcher.get_oldest_pf_cycle()));
   if (alu_lanes) alu_lanes->advance_base_cycle(MIN(fetch_cycle, prefetcher.get_oldest_pf_cycle()));
   const bool dump_activity = LOG_LEVEL != 0 && (fetch_cycle>= LOG_START_CYCLE) && (fetch_cycle<=LOG_END_CYCLE);
   if(dump_activity && activity_observed)
   {
       std::cout<<activity_trace.str();
   }

   if(inst->is_last_piece)
   {
       piece = UINT8_MAX;
   }

   num_insts_per_epoch.back() += inst->is_last_piece;
   const bool end_of_epoch = num_insts_per_epoch.back() == EPOCH_SIZE_INSTS;
   if(end_of_epoch)
   {
       end_current_begin_new_epoch(false/*first_epoch*/, false/*last_epoch*/, predict_cycle);
   }

}
#endif



#define KILOBYTE    (1<<10)
#define MEGABYTE    (1<<20)
#define SCALED_SIZE(size)   ((size/KILOBYTE >= KILOBYTE) ? (size/MEGABYTE) : (size/KILOBYTE))
#define SCALED_UNIT(size)   ((size/KILOBYTE >= KILOBYTE) ? "MB" : "KB")


uint64_t uarchsim_t::get_current_fetch_cycle() const {
    return fetch_cycle;
}

void uarchsim_t::output() 
{
   end_current_begin_new_epoch(false/*first_epoch*/, true/*last_epoch*/, cycle);
   //auto get_track_name = [] (uint64_t track){
   //   static std::string track_names [] = {
   //      "ALL",
   //      "LoadsOnly",
   //      "LoadsOnlyHitMiss",
   //   };
   //   //return track_names[static_cast<std::underlying_type<VPTracks>::type>(t)].c_str();
   //   return track_names[track].c_str();
   //};
   //printf("VP_ENABLE = %d\n", (VP_ENABLE ? 1 : 0));
   //printf("VP_PERFECT = %s\n", (VP_ENABLE ? (VP_PERFECT ? "1" : "0") : "n/a"));
   //printf("VP_TRACK = %s\n", (VP_ENABLE ? get_track_name(VP_TRACK) : "n/a"));
   printf("WINDOW_SIZE = %lu\n", WINDOW_SIZE);
   printf("FETCH_WIDTH = %lu\n", FETCH_WIDTH);
   printf("FETCH_NUM_BRANCH = %lu\n", FETCH_NUM_BRANCH);
   printf("FETCH_STOP_AT_INDIRECT = %s\n", (FETCH_STOP_AT_INDIRECT ? "1" : "0"));
   printf("FETCH_STOP_AT_TAKEN = %s\n", (FETCH_STOP_AT_TAKEN ? "1" : "0"));
   printf("FETCH_MODEL_ICACHE = %s\n", (FETCH_MODEL_ICACHE ? "1" : "0"));
   printf("PERFECT_BRANCH_PRED = %s\n", (PERFECT_BRANCH_PRED ? "1" : "0"));
   printf("PERFECT_INDIRECT_PRED = %s\n", (PERFECT_INDIRECT_PRED ? "1" : "0"));
   printf("PIPELINE_FILL_LATENCY = %lu\n", PIPELINE_FILL_LATENCY);
   printf("NUM_LDST_LANES = %lu%s", NUM_LDST_LANES, ((NUM_LDST_LANES > 0) ? "\n" : " (unbounded)\n"));
   printf("NUM_ALU_LANES = %lu%s", NUM_ALU_LANES, ((NUM_ALU_LANES > 0) ? "\n" : " (unbounded)\n"));
   //BP.output();
   printf("MEMORY HIERARCHY CONFIGURATION---------------------\n");
   printf("STRIDE Prefetcher = %s\n", PREFETCHER_ENABLE ? "1" : "0");
   printf("PERFECT_CACHE = %s\n", (PERFECT_CACHE ? "1" : "0"));
   printf("WRITE_ALLOCATE = %s\n", (WRITE_ALLOCATE ? "1" : "0"));
   printf("Within-pipeline factors:\n");
   printf("\tAGEN latency = 1 cycle\n");
   printf("\tStore Queue (SQ): SQ size = window size, oracle memory disambiguation, store-load forwarding = 1 cycle after store's or load's agen.\n");
   printf("\t* Note: A store searches the L1$ at commit. The store is released\n");
   printf("\t* from the SQ and window, whether it hits or misses. Store misses\n");
   printf("\t* are buffered until the block is allocated and the store is\n");
   printf("\t* performed in the L1$. While buffered, conflicting loads get\n");
   printf("\t* the store's data as they would from the SQ.\n");
   if (FETCH_MODEL_ICACHE) {
      printf("I$: %lu %s, %lu-way set-assoc., %luB block size\n",
         SCALED_SIZE(IC_SIZE), SCALED_UNIT(IC_SIZE), IC_ASSOC, IC_BLOCKSIZE);
   }
   printf("L1$: %lu %s, %lu-way set-assoc., %luB block size, %lu-cycle search latency\n",
      SCALED_SIZE(L1_SIZE), SCALED_UNIT(L1_SIZE), L1_ASSOC, L1_BLOCKSIZE, L1_LATENCY);
   printf("L2$: %lu %s, %lu-way set-assoc., %luB block size, %lu-cycle search latency\n",
      SCALED_SIZE(L2_SIZE), SCALED_UNIT(L2_SIZE), L2_ASSOC, L2_BLOCKSIZE, L2_LATENCY);
   printf("L3$: %lu %s, %lu-way set-assoc., %luB block size, %lu-cycle search latency\n",
      SCALED_SIZE(L3_SIZE), SCALED_UNIT(L3_SIZE), L3_ASSOC, L3_BLOCKSIZE, L3_LATENCY);
   printf("Main Memory: %lu-cycle fixed search time\n", MAIN_MEMORY_LATENCY);
   printf("---------------------------STORE QUEUE MEASUREMENTS (Full Simulation i.e. Counts Not Reset When Warmup Ends)---------------------------\n");
   printf("Number of loads: %lu\n", num_load);
   printf("Number of loads that miss in SQ: %lu (%.2f%%)\n", num_load_sqmiss, 100.0*(double)num_load_sqmiss/(double)num_load);
   printf("Number of PFs issued to the memory system %lu\n", stat_pfs_issued_to_mem);
   printf("---------------------------------------------------------------------------------------------------------------------------------------\n");
   printf("------------------------MEMORY HIERARCHY MEASUREMENTS (Full Simulation i.e. Counts Not Reset When Warmup Ends)-------------------------\n");
   if (FETCH_MODEL_ICACHE) {
      printf("I$:\n"); IC.stats();
   }
   printf("L1$:\n"); L1.stats();
   printf("L2$:\n"); L2.stats();
   printf("L3$:\n"); L3.stats();
   printf("---------------------------------------------------------------------------------------------------------------------------------------\n");
   printf("----------------------------------------------Prefetcher (Full Simulation i.e. No Warmup)----------------------------------------------\n");
   prefetcher.print_stats();
   printf("---------------------------------------------------------------------------------------------------------------------------------------\n");
   printf("\n-------------------------------ILP LIMIT STUDY (Full Simulation i.e. Counts Not Reset When Warmup Ends)--------------------------------\n");
   printf("instructions = %lu\n", num_inst);
   printf("cycles       = %lu\n", cycle);
   printf("CycWP        = %lu\n", cycles_on_wrong_path);
   printf("IPC          = %.4f\n", ((double)num_inst/(double)cycle));
   printf("\n---------------------------------------------------------------------------------------------------------------------------------------\n");
   // Branch Prediction Measurements
   BP.output(num_inst);
   BP.output_periodic_info(num_insts_per_epoch, num_cycles_per_epoch);
}
