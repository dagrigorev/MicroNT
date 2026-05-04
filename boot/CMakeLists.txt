# boot/CMakeLists.txt
# The boot assembly is compiled as part of the kernel target in kernel/CMakeLists.txt.
# This file exists for documentation and future standalone boot tooling.
#
# To assemble manually:
#   nasm -f elf64 boot.asm -o boot.o
