/* The IOP modules the port loads from memory (platform_ps2.c). */
    .section .rodata
    .balign 16
#define BLOB(name, file) \
    .globl name; name: .incbin file; .globl name##_end; name##_end: .balign 16
BLOB(irx_iomanx, "iomanX.irx")
BLOB(irx_filexio, "fileXio.irx")
BLOB(irx_usbd, "usbd.irx")
BLOB(irx_bdm, "bdm.irx")
BLOB(irx_bdmfs, "bdmfs_fatfs.irx")
BLOB(irx_usbmass, "usbmass_bd.irx")
BLOB(irx_libsd, "libsd.irx")
BLOB(irx_audsrv, "audsrv.irx")
BLOB(irx_sio2man, "sio2man.irx")
BLOB(irx_padman, "padman.irx")
