.section .rodata,"a",@progbits
.macro blob name,file
.balign 64
.global \name
\name:
.incbin "\file"
.global \name\()_end
\name\()_end:
.endm
blob intro_video,build/opening.mjpg
blob intro_audio,build/opening.pcm
blob audio_driver,audsrv.irx

blob konami,assets/frontend/konami.565
blob company,assets/frontend/company.565
blob title_track,assets/frontend/title.pcm
