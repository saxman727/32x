! Art

		.text

		.align 2
blayoutEHZ1:
		.incbin "ehz/blayout1.bin", 0, 0x80



		.global _blayoutEHZ1
_blayoutEHZ1:
		.long blayoutEHZ1
