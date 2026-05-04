#pragma once
// elf64.h - Minimal ELF64 type definitions for the MicroNT bootloader.

using Elf64_Addr  = unsigned long long;
using Elf64_Off   = unsigned long long;
using Elf64_Half  = unsigned short;
using Elf64_Word  = unsigned int;
using Elf64_Xword = unsigned long long;

// ELF magic
constexpr unsigned char ELFMAG0 = 0x7F;
constexpr unsigned char ELFMAG1 = 'E';
constexpr unsigned char ELFMAG2 = 'L';
constexpr unsigned char ELFMAG3 = 'F';

// e_ident indices
constexpr int EI_MAG0    = 0;
constexpr int EI_MAG1    = 1;
constexpr int EI_MAG2    = 2;
constexpr int EI_MAG3    = 3;
constexpr int EI_CLASS   = 4;
constexpr int EI_DATA    = 5;
constexpr int EI_NIDENT  = 16;

constexpr unsigned char ELFCLASS64  = 2;
constexpr unsigned char ELFDATA2LSB = 1;  // little-endian

// e_type
constexpr Elf64_Half ET_EXEC = 2;
constexpr Elf64_Half ET_DYN  = 3;

// e_machine
constexpr Elf64_Half EM_X86_64 = 62;

// ELF64 file header
struct Elf64_Ehdr {
    unsigned char e_ident[EI_NIDENT];
    Elf64_Half    e_type;
    Elf64_Half    e_machine;
    Elf64_Word    e_version;
    Elf64_Addr    e_entry;      // entry point virtual address
    Elf64_Off     e_phoff;      // program header table offset
    Elf64_Off     e_shoff;      // section header table offset
    Elf64_Word    e_flags;
    Elf64_Half    e_ehsize;
    Elf64_Half    e_phentsize;
    Elf64_Half    e_phnum;      // number of program headers
    Elf64_Half    e_shentsize;
    Elf64_Half    e_shnum;
    Elf64_Half    e_shstrndx;
};

// Program header types
constexpr Elf64_Word PT_NULL    = 0;
constexpr Elf64_Word PT_LOAD    = 1;
constexpr Elf64_Word PT_NOTE    = 4;

// Program header flags
constexpr Elf64_Word PF_X = 1;  // execute
constexpr Elf64_Word PF_W = 2;  // write
constexpr Elf64_Word PF_R = 4;  // read

// ELF64 program header
struct Elf64_Phdr {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;
    Elf64_Off   p_offset;   // offset in file
    Elf64_Addr  p_vaddr;    // virtual address
    Elf64_Addr  p_paddr;    // physical address
    Elf64_Xword p_filesz;   // size in file
    Elf64_Xword p_memsz;    // size in memory (may be > p_filesz for .bss)
    Elf64_Xword p_align;
};
