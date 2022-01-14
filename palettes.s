! Palettes

		.text

		.align 2
palEHZ:
		.incbin "ehz/pal.bin", 0, 0x200



		.global _palEHZ
_palEHZ:
		.long palEHZ
