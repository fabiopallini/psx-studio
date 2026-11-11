CFLAGS = -O3 -Xo$80010000 -Wall -pedantic

16bit:
	ccpsx $(CFLAGS) main.c object.c camera.c psx.c card.c -omain.cpe
	cpe2x /ce main.cpe
	..\cdrom\buildcd.exe -l -i..\cdrom\temp.img ..\cdrom\CONF.CTI
	..\cdrom\stripiso.exe s 2352 ..\cdrom\temp.img ..\cdrom\GAME.ISO
