! Art

		.text

		.align 2
flayoutEHZ1:
		.incbin "ehz/flayout1.bin", 0, 0x800



		.global _flayoutEHZ1
_flayoutEHZ1:
		.long flayoutEHZ1
