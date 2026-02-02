// LinxISA register definitions (v0.1).

// Registers are encoded as 5-bit values in fields like SrcL/SrcR/RegDst.
class LinxISAReg<bits<16> Enc, string Name> : Register<Name> {
  let HWEncoding = Enc;
  let Namespace = "Linx";
}

// GPRs (architected) + queue/result pseudo-registers (RegDst/Src* encodings).
let isConstant = true in
def R0  : LinxISAReg<0,  "zero">;
def R1  : LinxISAReg<1,  "sp">;
def R2  : LinxISAReg<2,  "a0">;
def R3  : LinxISAReg<3,  "a1">;
def R4  : LinxISAReg<4,  "a2">;
def R5  : LinxISAReg<5,  "a3">;
def R6  : LinxISAReg<6,  "a4">;
def R7  : LinxISAReg<7,  "a5">;
def R8  : LinxISAReg<8,  "a6">;
def R9  : LinxISAReg<9,  "a7">;
def R10 : LinxISAReg<10, "ra">;
def R11 : LinxISAReg<11, "s0">;
def R12 : LinxISAReg<12, "s1">;
def R13 : LinxISAReg<13, "s2">;
def R14 : LinxISAReg<14, "s3">;
def R15 : LinxISAReg<15, "s4">;
def R16 : LinxISAReg<16, "s5">;
def R17 : LinxISAReg<17, "s6">;
def R18 : LinxISAReg<18, "s7">;
def R19 : LinxISAReg<19, "s8">;
def R20 : LinxISAReg<20, "x0">;
def R21 : LinxISAReg<21, "x1">;
def R22 : LinxISAReg<22, "x2">;
def R23 : LinxISAReg<23, "x3">;

def T1  : LinxISAReg<24, "t#1">;
def T2  : LinxISAReg<25, "t#2">;
def T3  : LinxISAReg<26, "t#3">;
def T4  : LinxISAReg<27, "t#4">;

def U1  : LinxISAReg<28, "u#1">;
def U2  : LinxISAReg<29, "u#2">;
def U3  : LinxISAReg<30, "u#3">;
def U4  : LinxISAReg<31, "u#4">;

// GPR_Arch: Architectural GPRs only (R0-R23), no T/U queue references.
// Use this for operands that must hold actual values (like comparison results).
def GPR_Arch : RegisterClass<"Linx", [i64, i32], 64,
    (sequence "R%i", 0, 23)>;

// GPR: Full GPR class including T/U queue read-only references.
// The T/U queue registers can be used as source operands to read from the queue.
def GPR : RegisterClass<"Linx", [i64, i32], 64,
    (add (sequence "R%i", 0, 23),
         T1, T2, T3, T4,
         U1, U2, U3, U4)>;

def CSR_Linx : CalleeSavedRegs<(add R10,
                                   (sequence "R%i", 11, 19))>;
