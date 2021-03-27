
ss 90
ns 600 1
sp 15 36 0 0 2 0.0 0.0 0 1.0 1.0
inc ns 600 1
sp 15 36 0 0 2 0.0 0.0 0 1.0 1.0
inc ns 600 1
sp 15 36 0 0 2 0.0 0.0 0 1.0 1.0
inc ns 600 1
sp 15 36 0 0 2 0.0 0.0 0 1.0 1.0
inc ns 600 1
sp 15 36 0 0 2 0.0 0.0 0 1.0 1.0
inc ns 600 1
sp 15 36 0 0 2 0.0 0.0 0 1.0 1.0
inc ns 600 1
sp 15 36 0 0 2 0.0 0.0 0 1.0 1.0
inc ns 600 1
sp 15 36 0 0 2 0.0 0.0 0 1.0 1.0

inc
ge /home/fuzz/c/cilium/beat1.wav
sp 15 36 0 0 0 0.0 0.0 0 1.0 1.0
nf 0.5

inc
sp 15 36 0 0 2 0.0 0.0 0 1.0 1.0

ss 90

sch 0 90 0 91 0 92 0 93 0 94 0 95 0 96 0 97 0 98 0 99 -1

! jack_connect cilium:0 system:playback_1
! jack_connect cilium:1 system:playback_2
! jack_connect cilium:0 system:playback_3
! jack_connect cilium:1 system:playback_4
! jack_connect system:capture_1 cilium:in1
! jack_connect system:capture_2 cilium:in2
! jack_lsp -c
! aconnect 20 45
! aconnect 24 45

pi stdin
