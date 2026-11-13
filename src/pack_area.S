/* A file-backed slot inside a loadable ELF segment, filled before boot. */
.section .rodata.fm_pack,"a",@progbits
.balign 16
.ascii "RGFM_DATA_SLOT_1"
.global pack_capacity
pack_capacity:
.word 0x180000
.global pack_area
.type pack_area,@object
pack_area:
.space 0x180000,0
.size pack_area,.-pack_area
