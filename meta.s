! Metablock Maps

		.text

		.align 2
metaEHZ:
		.incbin "ehz/meta.bin", 0, 0x8000



		.global _metaEHZ
_metaEHZ:
		.long metaEHZ
