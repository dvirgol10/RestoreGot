# RestoreGot
RestoreGot is a toy tool for restoring the Global Offset Table of 32-bit applications at runtime.

This program searches the .plt section by iterating through the section headers table of the elf.
Then it uses [ptrace](https://man7.org/linux/man-pages/man2/ptrace.2.html) to find the GOT entry of each PLT stub and to change its value back to the original
value which points back to the PLT stub (by that, the next call for each function in the PLT will result in calling `_dl_runtime_resolve` again
to resolve the "real" function's address).

Note: We do the above-mentioned for all of the PLT stubs except the first because the first PLT stub contains the code which calls `_dl_runtime_resolve` to resolve
the symbols in the PLT. 
