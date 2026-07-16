devmem 0xa1000080

devmem 0xa1000000 32 0
devmem 0xa1000000 32 1
devmem 0xa1000008 32 1
devmem 0xa1000008 32 0

CHN=0
devmem $((0xa0100100 + CHN*0x1000)) 32 1
devmem $((0xa0100104 + CHN*0x1000)) 32 10
devmem $((0xa0100108 + CHN*0x1000)) 32 2
devmem $((0xa010010c + CHN*0x1000)) 32 0
devmem $((0xa0100110 + CHN*0x1000)) 32 0
devmem $((0xa0100114 + CHN*0x1000)) 32 0
devmem $((0xa0100118 + CHN*0x1000)) 32 1
devmem $((0xa010011c + CHN*0x1000)) 32 1
devmem $((0xa0100120 + CHN*0x1000)) 32 1024
devmem $((0xa0100124 + CHN*0x1000)) 32 1024
devmem $((0xa0100128 + CHN*0x1000)) 32 1024
devmem $((0xa010012c + CHN*0x1000)) 32 1
devmem $((0xa0100130 + CHN*0x1000)) 32 0
devmem $((0xa0100134 + CHN*0x1000)) 32 0

devmem $((0xa0a00a00 + CHN*0x1000)) 32 0
devmem $((0xa0a00a04 + CHN*0x1000)) 32 115200
devmem $((0xa0a00a08 + CHN*0x1000)) 32 16
devmem $((0xa0a00a0c + CHN*0x1000)) 32 2710


