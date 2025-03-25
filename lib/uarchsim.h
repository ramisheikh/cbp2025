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


#include <unordered_map>
#include <list>
#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h"
//#include "cbp.h"
#include "value_predictor_interface.h"
#include "stride_prefetcher.h"
using namespace std;

#ifndef _RISCV_UARCHSIM_H
#define _RISCV_UARCHSIM_H

#define RFSIZE 66   // integer: r0-r31.  fp/simd: r32-r63. flags: r64.
#define RFFLAGS 64  // flags register is r64 (65th register)
#define RFZERO 65   // zero register is r65 (66th register)

struct window_t {
   uint64_t seq_no;
   uint8_t piece;
   uint64_t PC;
   uint64_t fetch_cycle;
   uint64_t decode_cycle;
   uint64_t exec_cycle;
   ExecuteInfo exec_info;
   uint64_t retire_cycle;
   bool pred_taken;
   uint64_t addr;
   uint64_t value;
   uint64_t latency;
   window_t (uint64_t _seq_no, uint8_t _piece, uint64_t _PC, uint64_t _fetch_cycle, uint64_t _decode_cycle, uint64_t _exec_cycle, ExecuteInfo _exec_info, uint64_t _retire_cycle, uint64_t _addr, uint64_t _value, uint64_t _latency)
     : seq_no(_seq_no)
     , piece(_piece)
     , PC(_PC)
     , fetch_cycle(_fetch_cycle)
     , decode_cycle(_decode_cycle)
     , exec_cycle(_exec_cycle)
     , exec_info(_exec_info)
     , retire_cycle(_retire_cycle)
     , pred_taken(false)
     , addr(_addr)
     , value(_value)
     , latency(_latency)
   {}

   window_t()
     : seq_no(UINT64_MAX)
     , piece(UINT8_MAX)
     , PC(UINT64_MAX)
     , fetch_cycle(UINT64_MAX)
     , decode_cycle(UINT64_MAX)
     , exec_cycle(UINT64_MAX)
     , retire_cycle(UINT64_MAX)
     , pred_taken(false)
     , addr(UINT64_MAX)
     , value(UINT64_MAX)
     , latency(UINT64_MAX)
   {
   }

   void update_pred_taken(bool _pred_taken)
   {
      pred_taken = _pred_taken;
   }

   friend std::ostream& operator<<(std::ostream& os, const window_t& entry)
   {
       os<<"{";
       os<<" ["<<entry.seq_no<<","<<(uint64_t)entry.piece<<"]";
       os<<" PC:0x"<<std::hex<<entry.PC<<std::dec;
       os<<" Class:"<<cInfo[static_cast<uint8_t>(entry.exec_info.dec_info.insn_class)];
       os<<" TakenPopulated:"<<entry.exec_info.taken.has_value();
       os<<" TakenVal:"<<entry.exec_info.taken.value_or(false);
       os<<" fetch_cycle:"<<entry.fetch_cycle;
       os<<" decode_cycle:"<<entry.decode_cycle;
       os<<" exec_cycle:"<<entry.exec_cycle;
       os<<" ExecInfo:"<<entry.exec_info;
       os<<" retire_cycle:"<<entry.retire_cycle;
       os<<"}";
       return os;
   }
};

struct store_queue_t {
   uint64_t exec_cycle; // store's execution cycle
   uint64_t ret_cycle;  // store's commit cycle
};

// Class for a microarchitectural simulator.

class uarchsim_t {
   private:
      // Add your class member variables here to facilitate your limit study.

      // Modeling resources: (1) finite fetch bundle, (2) finite window, and (3) finite execution lanes.
      uint64_t num_fetched;
      uint64_t num_fetched_branch;
      //fifo_t<window_t> window;
      std::deque<window_t> window;
      uint64_t window_capacity;
      resource_schedule *alu_lanes;
      resource_schedule *ldst_lanes;

      // register timestamps
      uint64_t RF[RFSIZE];

      // store queue byte timestamps
      unordered_map<uint64_t, store_queue_t> SQ;

      std::deque<std::tuple<uint64_t/*seq_no*/, uint8_t/*piece*/, uint64_t/*decode_cycle*/>> DQ;
      std::list<std::tuple<uint64_t/*seq_no*/, uint8_t/*piece*/, uint64_t/*agen_cycle*/>> AQ; // agen_queue
      std::list<std::tuple<uint64_t/*seq_no*/, uint8_t/*piece*/, uint64_t/*exec_cycle*/>> EQ;

      // memory block timestamps
      cache_t L3;
      cache_t L2;
      cache_t L1;

      // fetch timestamp
      uint64_t fetch_cycle;
      uint64_t previous_fetch_cycle = 0;
   
      // Branch predictor.
      bp_t BP;

      // Instruction cache.
      cache_t IC;

      //Prefetcher
      StridePrefetcher prefetcher;
      // Instruction and cycle counts for IPC.
      uint64_t num_inst;
      uint64_t num_uop;
      uint64_t cycle;

      // Instructions, cycle_count, for each epoch
      std::vector<uint64_t> num_insts_per_epoch;
      std::vector<uint64_t> num_cycles_per_epoch;
      uint64_t last_epoch_end_cycle;

      // CVP measurements
      uint64_t num_eligible;
      uint64_t num_correct;
      uint64_t num_incorrect;

      // stats
      uint64_t num_load;
      uint64_t num_load_sqmiss;
      uint64_t cycles_on_wrong_path;

      uint64_t stat_pfs_issued_to_mem = 0;

      // Helper for oracle hit/miss information
      uint64_t get_load_exec_cycle(db_t *inst) const;

      DecodeInfo _current_decode_info;
      ExecuteInfo _current_execute_info;
      void populate_exec_info(db_t *inst); 
      void populate_decode_info(db_t *inst); 
      const window_t& locate_entry_in_window(uint64_t seq_no, uint8_t piece) const;
      void end_current_begin_new_epoch(const bool first_epoch, const bool last_epoch, const uint64_t epoch_end_cycle);

   public:
      uarchsim_t();
      ~uarchsim_t();

      //void set_funcsim(processor_t *funcsim);
      void step(db_t *inst);
      void eval_decode(std::ostream& activity_trace, bool& activity_observed, const uint64_t current_fetch_cycle) ;
      void eval_aq(std::ostream& activity_trace, bool& activity_observed, const uint64_t current_fetch_cycle) ;
      void eval_exec(std::ostream& activity_trace, bool& activity_observed, const uint64_t current_fetch_cycle) ;
      void eval_retire(std::ostream& activity_trace, bool& activity_observed, const uint64_t current_fetch_cycle) ;
      void output();
      uint64_t get_current_fetch_cycle() const;
      PredictionRequest get_value_prediction_req_for_track(uint64_t cycle, uint64_t seq_no, uint8_t piece, db_t *inst);
};

#endif
