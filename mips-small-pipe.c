#include "mips-small-pipe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/************************************************************/
int main(int argc, char *argv[]) {
  short i;
  char line[MAXLINELENGTH];
  state_t state;
  FILE *filePtr;

  if (argc != 2) {
    printf("error: usage: %s <machine-code file>\n", argv[0]);
    return 1;
  }

  memset(&state, 0, sizeof(state_t));

  state.pc = state.cycles = 0;
  state.IFID.instr = state.IDEX.instr = state.EXMEM.instr = state.MEMWB.instr =
      state.WBEND.instr = NOPINSTRUCTION; /* nop */

  /* read machine-code file into instruction/data memory (starting at address 0)
   */

  filePtr = fopen(argv[1], "r");
  if (filePtr == NULL) {
    printf("error: can't open file %s\n", argv[1]);
    perror("fopen");
    exit(1);
  }

  for (state.numMemory = 0; fgets(line, MAXLINELENGTH, filePtr) != NULL;
       state.numMemory++) {
    if (sscanf(line, "%x", &state.dataMem[state.numMemory]) != 1) {
      printf("error in reading address %d\n", state.numMemory);
      exit(1);
    }
    state.instrMem[state.numMemory] = state.dataMem[state.numMemory];
    printf("memory[%d]=%x\n", state.numMemory, state.dataMem[state.numMemory]);
  }

  printf("%d memory words\n", state.numMemory);

  printf("\tinstruction memory:\n");
  for (i = 0; i < state.numMemory; i++) {
    printf("\t\tinstrMem[ %d ] = ", i);
    printInstruction(state.instrMem[i]);
  }

  run(&state);

  return 0;
}
/************************************************************/
void run(Pstate state) {
  state_t new;
  int stallPipeline;
  int rs, rt;
  int destReg;
  int instIF, opIF;
  int opID, opEXM, opMWB;
  int f;

  int aluA, aluB; /* forwarded operands in EX */
  int rsID, rtID; /* source regs of IDEX instr */
  int destEXMEM, destMEMWB, destWBEND;
  int opEXFwd, opMWBFwd, opWBFwd;  /* opcodes for forwarding */
  int immIF, immEX;                /* branch immediates */
  int predictedTaken, actualTaken; /* branch prediction */

  while (1) {
    printState(state);
    /* Halt when HALT instruction hits MEMWB */
    if (opcode(state->MEMWB.instr) == HALT_OP) {
      printf("machine halted\n");
      printf("total of %d cycles executed\n", state->cycles);
      exit(0);
    }

    /* start new state as a copy of the old */
    memcpy(&new, state, sizeof(state_t));
    new.cycles = state->cycles + 1;

    /* --------------------- IF stage --------------------- */
    instIF = state->instrMem[memaddr(state->pc)];
    new.IFID.instr = instIF;
    new.IFID.pcPlus1 = state->pc + 4;
    opIF = opcode(instIF);
    if (opIF == BEQZ_OP) {
      immIF = field_imm(instIF);
      if (immIF & 0x8000) {
        immIF |= 0xFFFF0000;
      }

      /* Predict taken for negoffset, not-taken for non-neg */
      if (immIF < 0) {
        new.pc = state->pc + 4 + immIF; /*  taken */
      } else {
        new.pc = state->pc + 4; /*not taken */
      }
    } else {
      new.pc = state->pc + 4;
    }

    /* --------------------- ID stage --------------------- */
    stallPipeline = 0;
    rs = -1;
    rt = -1;

    instIF = state->IFID.instr;
    opIF = opcode(instIF);

    /* determine source registers of IFID instruction */
    if (opIF == REG_REG_OP) {
      rs = field_r1(instIF);
      rt = field_r2(instIF);
    } else if (opIF == SW_OP) {
      rs = field_r1(instIF);
      rt = field_r2(instIF);
    } else if (opIF == ADDI_OP || opIF == LW_OP || opIF == BEQZ_OP) {
      rs = field_r1(instIF);
    }
    /*load hazard */
    opID = opcode(state->IDEX.instr);
    if (opID == LW_OP) {
      destReg = field_r2(state->IDEX.instr);
      if (destReg != 0 && (destReg == rs || destReg == rt)) {
        stallPipeline = 1;
      }
    }

    if (stallPipeline) {
      /* insert bubble into IDEX and freeze IF/PC */
      new.IDEX.instr = NOPINSTRUCTION;
      new.IDEX.pcPlus1 = 0;
      new.IDEX.readRegA = 0;
      new.IDEX.readRegB = 0;
      new.IDEX.offset = offset(NOPINSTRUCTION);
      new.IFID.instr = state->IFID.instr;
      new.IFID.pcPlus1 = state->IFID.pcPlus1;
      new.pc = state->pc;
    } else {
      /* normal ID path */
      new.IDEX.instr = state->IFID.instr;
      new.IDEX.pcPlus1 = state->IFID.pcPlus1;
      new.IDEX.readRegA = state->reg[field_r1(state->IFID.instr)];
      new.IDEX.readRegB = state->reg[field_r2(state->IFID.instr)];
      new.IDEX.offset = offset(state->IFID.instr);
    }

    /* --------------------- EX stage --------------------- */
    new.EXMEM.instr = state->IDEX.instr;
    opID = opcode(state->IDEX.instr);
    aluA = state->IDEX.readRegA;
    aluB = state->IDEX.readRegB;

    rsID = -1;
    rtID = -1;
    if (opID == REG_REG_OP || opID == SW_OP) {
      rsID = field_r1(state->IDEX.instr);
      rtID = field_r2(state->IDEX.instr);
    } else if (opID == ADDI_OP || opID == LW_OP || opID == BEQZ_OP) {
      rsID = field_r1(state->IDEX.instr);
    }
    /* Destination regs of the 3 instr*/
    destEXMEM = -1;
    opEXFwd = opcode(state->EXMEM.instr);
    if (opEXFwd == REG_REG_OP) {
      destEXMEM = field_r3(state->EXMEM.instr);
    } else if (opEXFwd == ADDI_OP) {
      destEXMEM = field_r2(state->EXMEM.instr);
    }
    /* Don't forward from EXMEM when LW, val not ready*/

    destMEMWB = -1;
    opMWBFwd = opcode(state->MEMWB.instr);
    if (opMWBFwd == REG_REG_OP) {
      destMEMWB = field_r3(state->MEMWB.instr);
    } else if (opMWBFwd == ADDI_OP || opMWBFwd == LW_OP) {
      destMEMWB = field_r2(state->MEMWB.instr);
    }

    destWBEND = -1;
    opWBFwd = opcode(state->WBEND.instr);
    if (opWBFwd == REG_REG_OP) {
      destWBEND = field_r3(state->WBEND.instr);
    } else if (opWBFwd == ADDI_OP || opWBFwd == LW_OP) {
      destWBEND = field_r2(state->WBEND.instr);
    }

    /* Forward to rsID (exmem first) */
    if (rsID != -1 && rsID != 0) {
      if (destEXMEM == rsID && opEXFwd != LW_OP) {
        aluA = state->EXMEM.aluResult;
      } else if (destMEMWB == rsID) {
        aluA = state->MEMWB.writeData;
      } else if (destWBEND == rsID) {
        aluA = state->WBEND.writeData;
      }
    }

    /* Forward to rtID*/
    if (rtID != -1 && rtID != 0) {
      if (destEXMEM == rtID && opEXFwd != LW_OP) {
        aluB = state->EXMEM.aluResult;
      } else if (destMEMWB == rtID) {
        aluB = state->MEMWB.writeData;
      } else if (destWBEND == rtID) {
        aluB = state->WBEND.writeData;
      }
    }

    new.EXMEM.readRegB = aluB;
    if (opID == REG_REG_OP) {
      f = func(state->IDEX.instr);
      if (f == ADD_FUNC) {
        new.EXMEM.aluResult = aluA + aluB;
      } else if (f == SUB_FUNC) {
        new.EXMEM.aluResult = aluA - aluB;
      } else if (f == SLL_FUNC) {
        new.EXMEM.aluResult = aluA << aluB;
      } else if (f == SRL_FUNC) {
        new.EXMEM.aluResult = ((unsigned int)aluA) >> aluB;
      } else if (f == AND_FUNC) {
        new.EXMEM.aluResult = aluA &aluB;
      } else if (f == OR_FUNC) {
        new.EXMEM.aluResult = aluA | aluB;
      } else {
        new.EXMEM.aluResult = 0;
      }
    } else if (opID == ADDI_OP) {
      new.EXMEM.aluResult = aluA + state->IDEX.offset;
    } else if (opID == LW_OP || opID == SW_OP) {
      new.EXMEM.aluResult = aluA + state->IDEX.offset;
    } else if (opID == BEQZ_OP) {
      immEX = state->IDEX.offset;
      new.EXMEM.aluResult = state->IDEX.pcPlus1 + immEX;

      actualTaken = (aluA == 0);
      predictedTaken = (immEX < 0);

      if (actualTaken != predictedTaken) {
        if (actualTaken) {
          /*branch was taken but we predicted not-taken */
          new.pc = state->IDEX.pcPlus1 + immEX;
        } else {
          /* branch was not-taken but we predicted taken */
          new.pc = state->IDEX.pcPlus1;
        }

        /* Squash last 2 instrin IF ID */
        new.IFID.instr = NOPINSTRUCTION;
        /* check*/
        new.IFID.pcPlus1 = 0;
        new.IDEX.instr = NOPINSTRUCTION;
        new.IDEX.pcPlus1 = 0;
        new.IDEX.readRegA = 0;
        new.IDEX.readRegB = 0;
        new.IDEX.offset = offset(NOPINSTRUCTION);
      }
    } else {
      new.EXMEM.aluResult = 0;
    }

    /* --------------------- MEM stage --------------------- */
    new.MEMWB.instr = state->EXMEM.instr;
    opEXM = opcode(state->EXMEM.instr);
    if (opEXM == LW_OP) {
      /* load word */
      destReg = field_r2(state->EXMEM.instr);
      new.MEMWB.writeData = state->dataMem[memaddr(state->EXMEM.aluResult)];
    } else if (opEXM == SW_OP) {
      new.dataMem[memaddr(state->EXMEM.aluResult)] = state->EXMEM.readRegB;
      new.MEMWB.writeData = state->EXMEM.readRegB;
    } else {
      new.MEMWB.writeData = state->EXMEM.aluResult;
    }
    /* --------------------- WB stage --------------------- */
    opMWB = opcode(state->MEMWB.instr);
    if (opMWB == REG_REG_OP) {
      new.reg[field_r3(state->MEMWB.instr)] = state->MEMWB.writeData;
    } else if (opMWB == LW_OP || opMWB == ADDI_OP) {
      new.reg[field_r2(state->MEMWB.instr)] = state->MEMWB.writeData;
    }
    new.reg[0] = 0;

    /* --------------------- end stage --------------------- */
    new.WBEND.instr = state->MEMWB.instr;
    new.WBEND.writeData = state->MEMWB.writeData;

    /* move new state into current */
    memcpy(state, &new, sizeof(state_t));
  }
}

/************************************************************/

/************************************************************/
int opcode(int instruction) { return (instruction >> OP_SHIFT) & OP_MASK; }
/************************************************************/

/************************************************************/
int func(int instruction) { return (instruction & FUNC_MASK); }
/************************************************************/

/************************************************************/
int field_r1(int instruction) { return (instruction >> R1_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_r2(int instruction) { return (instruction >> R2_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_r3(int instruction) { return (instruction >> R3_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_imm(int instruction) { return (instruction & IMMEDIATE_MASK); }
/************************************************************/

/************************************************************/
int offset(int instruction) {
  /* only used for lw, sw, beqz */
  return convertNum(field_imm(instruction));
}
/************************************************************/

/************************************************************/
int convertNum(int num) {
  /* convert a 16 bit number into a 32-bit Sun number */
  if (num & 0x8000) {
    num -= 65536;
  }
  return (num);
}
/************************************************************/

/************************************************************/
void printState(Pstate state) {
  short i;
  printf("@@@\nstate before cycle %d starts\n", state->cycles);
  printf("\tpc %d\n", state->pc);

  printf("\tdata memory:\n");
  for (i = 0; i < state->numMemory; i++) {
    printf("\t\tdataMem[ %d ] %d\n", i, state->dataMem[i]);
  }
  printf("\tregisters:\n");
  for (i = 0; i < NUMREGS; i++) {
    printf("\t\treg[ %d ] %d\n", i, state->reg[i]);
  }
  printf("\tIFID:\n");
  printf("\t\tinstruction ");
  printInstruction(state->IFID.instr);
  printf("\t\tpcPlus1 %d\n", state->IFID.pcPlus1);
  printf("\tIDEX:\n");
  printf("\t\tinstruction ");
  printInstruction(state->IDEX.instr);
  printf("\t\tpcPlus1 %d\n", state->IDEX.pcPlus1);
  printf("\t\treadRegA %d\n", state->IDEX.readRegA);
  printf("\t\treadRegB %d\n", state->IDEX.readRegB);
  printf("\t\toffset %d\n", state->IDEX.offset);
  printf("\tEXMEM:\n");
  printf("\t\tinstruction ");
  printInstruction(state->EXMEM.instr);
  printf("\t\taluResult %d\n", state->EXMEM.aluResult);
  printf("\t\treadRegB %d\n", state->EXMEM.readRegB);
  printf("\tMEMWB:\n");
  printf("\t\tinstruction ");
  printInstruction(state->MEMWB.instr);
  printf("\t\twriteData %d\n", state->MEMWB.writeData);
  printf("\tWBEND:\n");
  printf("\t\tinstruction ");
  printInstruction(state->WBEND.instr);
  printf("\t\twriteData %d\n", state->WBEND.writeData);
}
/************************************************************/

/************************************************************/
void printInstruction(int instr) {

  if (opcode(instr) == REG_REG_OP) {

    if (func(instr) == ADD_FUNC) {
      print_rtype(instr, "add");
    } else if (func(instr) == SLL_FUNC) {
      print_rtype(instr, "sll");
    } else if (func(instr) == SRL_FUNC) {
      print_rtype(instr, "srl");
    } else if (func(instr) == SUB_FUNC) {
      print_rtype(instr, "sub");
    } else if (func(instr) == AND_FUNC) {
      print_rtype(instr, "and");
    } else if (func(instr) == OR_FUNC) {
      print_rtype(instr, "or");
    } else {
      printf("data: %d\n", instr);
    }

  } else if (opcode(instr) == ADDI_OP) {
    print_itype(instr, "addi");
  } else if (opcode(instr) == LW_OP) {
    print_itype(instr, "lw");
  } else if (opcode(instr) == SW_OP) {
    print_itype(instr, "sw");
  } else if (opcode(instr) == BEQZ_OP) {
    print_itype(instr, "beqz");
  } else if (opcode(instr) == HALT_OP) {
    printf("halt\n");
  } else {
    printf("data: %d\n", instr);
  }
}
/************************************************************/

/************************************************************/
void print_rtype(int instr, const char *name) {
  printf("%s %d %d %d\n", name, field_r3(instr), field_r1(instr),
         field_r2(instr));
}
/************************************************************/

/************************************************************/
void print_itype(int instr, const char *name) {
  printf("%s %d %d %d\n", name, field_r2(instr), field_r1(instr),
         offset(instr));
}
/************************************************************/
