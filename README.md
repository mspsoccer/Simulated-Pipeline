# Simulated-Pipeline
Overview

This project implements a five-stage pipelined MIPS processor simulator.
The simulator models all major components of a pipelined CPU, including:

 ```
Instruction Fetch (IF)

Instruction Decode (ID)

Execute (EX)

Memory Access (MEM)

Write Back (WB)
```

The model supports cycle-accurate execution, prints detailed pipeline state each cycle, and correctly handles complex interactions between instructions.

Features
Data Forwarding
  Implements forwarding from EX/MEM, MEM/WB, and WB/END pipeline registers to eliminate unnecessary stalls.

Hazard Detection & Pipeline Stalling
  Detects load-use hazards and inserts bubbles as needed to preserve correctness.

Branch Handling
  Supports beqz control flow with:
    Static predict-taken-for-negative-offset logic
    Branch target computation
    Pipeline squashing on misprediction

Memory & Register File Simulation
  Accurately models:
    Register reads/writes 
    Memory loads (lw) and stores (sw)
    ALU operations and immediate instructions

Instruction Tracing
Every cycle prints the full pipeline state:
  PC
  Register values
  Pipeline registers (IFID, IDEX, EXMEM, MEMWB, WBEND)
  ALU results & write-back values

```
Project Structure
.
├── small-pipe.c            # Full MIPS pipeline simulator implementation
├── mips-small-pipe.h       # Instruction encoding macros & pipeline structs
├── Makefile                # Build instructions
└──  public-tests/           # Instructor-provided test inputs & expected output
```          

Building the Simulator

Run the provided Makefile:

make

This produces an executable:

./small-pipe <machine-code-file>



Learning Outcomes

Through building this simulator, I gained hands-on experience in:

CPU microarchitecture & pipeline design

Debugging systems-level C code

Hazard mitigation strategies (forwarding, stalling, flushing)

Control-flow prediction

Cycle-accurate pipeline modeling

This project significantly deepened my understanding of how real processors execute instructions efficiently.

Contact

If you’d like to discuss the project, improvements, or collaboration opportunities:

Milan Patel
milanpatel016@gmail.com
9087451461
