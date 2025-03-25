/*
  Copyright (C) ARM Limited 2008-2025  All rights reserved.                                                                                                                                                                                                                        

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

  3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once
#include "lib/sim_common_structs.h"

//
// beginCondDirPredictor()
// 
// This function is called by the simulator before the start of simulation.
// It can be used for arbitrary initialization steps for the contestant's code.
//
extern void beginCondDirPredictor();

//
// notify_instr_fetch(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t fetch_cycle)
// 
// This function is called when any instructions(not just branches) gets fetched.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction and cycle are also provided as inputs
//
extern void notify_instr_fetch(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t fetch_cycle);

//
// get_cond_dir_prediction(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t pred_cycle)
// 
// This function is called by the simulator for predicting conditional branches.
// input values are unique identifying ids(seq_no, piece) and PC of the branch.
// return value is the predicted direction. 
//
extern bool get_cond_dir_prediction(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t pred_cycle);

//
// spec_update(uint64_t seq_no, uint8_t piece, uint64_t pc, InstClass inst_class, const bool resolve_dir, const bool pred_dir, const uint64_t next_pc)
// 
// This function is called by the simulator for updating the history vectors and any state that needs to be updated speculatively.
// The function is called for all the branches (not just conditional branches). To faciliate accurate history updates, spec_update is called right
// after a prediction is made.
// input values are unique identifying ids(seq_no, piece), PC of the instruction, instruction class, predicted/resolve direction and the next_pc 
//
extern void spec_update(uint64_t seq_no, uint8_t piece, uint64_t pc, InstClass inst_class, const bool resolve_dir, const bool pred_dir, const uint64_t next_pc);

//
// notify_instr_decode(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t decode_cycle)
// 
// This function is called when any instructions(not just branches) gets decoded.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, decode info and cycle are also provided as inputs
//
extern void notify_instr_decode(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t decode_cycle);

//
// notify_agen_complete(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t mem_va, const uint64_t mem_sz, const uint64_t agen_cycle)
// 
// This function is called when any load/store instructions complete agen.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, decode info, mem_va and mem_sz and agen_cycle are also provided as inputs
//
extern void notify_agen_complete(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t mem_va, const uint64_t mem_sz, const uint64_t agen_cycle);

//
// notify_instr_execute_resolve(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t execute_cycle)
// 
// This function is called when any instructions(not just branches) gets executed.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, execute info and cycle are also provided as inputs
//
extern void notify_instr_execute_resolve(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t execute_cycle);

//
// notify_instr_commit(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t commit_cycle)
// 
// This function is called when any instructions(not just branches) gets committed.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, execute info and cycle are also provided as inputs
//
extern void notify_instr_commit(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t commit_cycle);


//
// endCondDirPredictor()
//
// This function is called by the simulator at the end of simulation.
// It can be used by the contestant to print out other contestant-specific measurements.
//
extern void endCondDirPredictor();
