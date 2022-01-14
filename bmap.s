! Block Maps

		.text

		.align 2
bmapEHZ:
		.incbin "ehz/bmap.bin", 0, 0x2000



		.global _bmapEHZ
_bmapEHZ:
		.long bmapEHZ
