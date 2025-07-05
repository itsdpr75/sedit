clang sedit.c -o sedit -Oz -fdata-sections -ffunction-sections -Wl,--gc-sections -s
strip --strip-unneeded sedit