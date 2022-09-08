#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdint.h>
#include <elf.h>
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <errno.h>
#include <sys/wait.h>

#define PLT_STUB_SIZE 16
#define PLT_SECTION_NAME ".plt"
#define GOT_ADDRESS_OFFSET_IN_PLT_STUB 2


int verify_elf_format(Elf32_Ehdr *ehdr) {
    return ehdr->e_ident[EI_MAG0] == ELFMAG0 && ehdr->e_ident[EI_MAG1] == ELFMAG1 && ehdr->e_ident[EI_MAG2] == ELFMAG2 && ehdr->e_ident[EI_MAG3] == ELFMAG3;
}


int find_section_addr_and_ent_num(Elf32_Shdr *shdrTable, uint16_t shdrTableSize, char *stringTable, char *sectionName, uint16_t entSize, Elf32_Addr *p_addr, uint64_t *p_entNum) {
        for (int i = 0; i < shdrTableSize; i++) {
        if (!strcmp(stringTable + shdrTable[i].sh_name, sectionName)) {
            if (p_addr) {
                *p_addr = shdrTable[i].sh_addr;
            }
            if (p_entNum && entSize) {
                *p_entNum = shdrTable[i].sh_size / entSize;
            }
            return 1;
        }
    }
    return 0;
}


int main(int argc, char* argv[]) {
    char *exePath;
    int pid;
    int fd;
    struct stat st;
    uint8_t *exeContent;
    Elf32_Ehdr *ehdr;
    Elf32_Shdr *shdrTable;
    uint16_t shdrTableSize;
    char *stringTable;
    Elf32_Addr pltAddr;
    uint64_t pltStubNum;
    Elf32_Addr currentPltStubAddr;
    Elf32_Addr currentGotEntryAddress;
    Elf32_Addr currentGotEntryOriginalValue;
    int status;

    if (argc < 3) {
        printf("Usage: %s <exePath> <pid>\n", argv[0]);
        exit(0);
    }

    exePath = argv[1];
    pid = atoi(argv[2]);

    if ((fd = open(exePath, O_RDONLY)) < 0) {
        perror("open");
        exit(1);
    }

    if (fstat(fd, &st) < 0) {
        perror("stat");
        exit(1);
    }

    if ((exeContent = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    ehdr = (Elf32_Ehdr *) exeContent;
    if (!verify_elf_format(ehdr)) {
        fprintf(stderr, "The file is not in ELF format\n");
        exit(1);
    }

    if (ehdr->e_type != ET_EXEC) {
        fprintf(stderr, "The program is not an executable");
        exit(1);
    }

    shdrTable = (Elf32_Shdr *)(exeContent + ehdr->e_shoff);
    shdrTableSize = ehdr->e_shnum;
    if (!ehdr->e_shoff || !ehdr->e_shnum) {
        fprintf(stderr, "The program has no section header table\n");
        exit(1);
    }

    if (ehdr->e_shstrndx == SHN_UNDEF) {
        fprintf(stderr, "The program has no section name string table\n");
        exit(1);
    }
    stringTable = (char *)(exeContent + shdrTable[ehdr->e_shstrndx].sh_offset);
    
    if (!find_section_addr_and_ent_num(shdrTable, shdrTableSize, stringTable, PLT_SECTION_NAME, PLT_STUB_SIZE, &pltAddr, &pltStubNum)) {
        fprintf(stderr, "The program has no '%s' section\n", PLT_SECTION_NAME);
        exit(1);
    }
    printf("[*] The address of the '%s' section is 0x%x\n", PLT_SECTION_NAME, pltAddr);
    printf("[*] There are %ld '%s' stubs\n", pltStubNum, PLT_SECTION_NAME);
    
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) {
        perror("ptrace: PTRACE_ATTACH");
        exit(1);
    }
    waitpid(pid, &status, 0);
    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "The program hasn't stopped after attaching\n");
        exit(1);
    }

    for (int i = 1; i < pltStubNum; i++) { // we start from i=1 because PLT[0] is a stub which jumps to _dl_runtime_resolve
        currentPltStubAddr = pltAddr + i * PLT_STUB_SIZE;
        errno = 0;
        currentGotEntryAddress = ptrace(PTRACE_PEEKDATA, pid, currentPltStubAddr + GOT_ADDRESS_OFFSET_IN_PLT_STUB, NULL);
        if (errno) {
            perror("ptrace: PTRACE_PEEKDATA");
            exit(1);
        }
        printf("[*] The got entry address of plt stub %d is 0x%x", i, currentGotEntryAddress);
        currentGotEntryOriginalValue = currentPltStubAddr + GOT_ADDRESS_OFFSET_IN_PLT_STUB + sizeof(Elf32_Addr);
        if (ptrace(PTRACE_POKEDATA, pid, currentGotEntryAddress, currentGotEntryOriginalValue) < 0) {
            perror("ptrace: PTRACE_POKEDATA");
            exit(1);
        }
        printf(", and it has been modified to the value 0x%x\n", currentGotEntryOriginalValue);
    }

    if (ptrace(PTRACE_DETACH, pid, NULL, NULL) < 0) {
        perror("ptrace: PTRACE_DETACH");
        exit(1);
    }

    if (close(fd) < 0) {
        perror("close");
        exit(1);
    }

    if (munmap(exeContent, st.st_size) < 0) {
        perror("munmap");
        exit(1);
    }

    return 0;
}