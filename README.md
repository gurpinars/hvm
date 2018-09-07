# hvm
Virtual machine for the machine language from [From Nand to Tetris MOOC](https://www.coursera.org/learn/build-a-computer) on Coursera.
### Platform
The platform is a 16-bit von Neumann machine, consisting of a CPU,two separate memory modules serving as instruction memory and data memory.The CPU consists of the ALU and three registers called data register(D), address register (A), and program counter(PC). D and A are general-purpose 16-bit registers that can be manipulated by arithmetic and logical instructions like A=D-1, D=D|A, and so on.While the D-register is used solely to store data values, the contents of the A-register can be interpreted in three different ways, depending on the instruction’s context: as a data value, as a RAM address, or as a ROM address.

### Building
```bash
make clean
make
```
### Usage
```bash
./hvm [inputfile.hex]
```
