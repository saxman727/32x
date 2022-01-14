! Art

		.text

		.align 2
artEHZ:
		.incbin "ehz/art.bin", 0, 0x20000



		.global _artEHZ
_artEHZ:
		.long artEHZ
