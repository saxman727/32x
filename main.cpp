#include <stdlib.h>

#include "32x.h"
#include "hw_32x.h"

#include "palettes.h"
#include "art.h"
#include "bmap.h"
#include "meta.h"
#include "flayout.h"
#include "blayout.h"


// 0x00000-0x3FFFF	>>	SDRAM	 256 KB
// =======================================
// 0x00000-0x17FFF	>>	Art		  96 KB		1536 tiles
// 0x18000-0x1FFFF	>>	Meta	  32 KB		256 meta blocks
// 0x20000-0x21FFF	>>	Bmap	   8 KB		1024 blocks
// 0x22000-0x221FF	>>	Pal		 512 Bytes	256 colors
// =======================================
// 0x22200-0x3FFFF	>>	FREE!	119.5 KB

#define SDRAM_ART		(*(volatile unsigned short *)(0x26000000 + 0x00000))
#define SDRAM_META		(*(volatile unsigned short *)(0x26000000 + 0x20000))
#define SDRAM_BMAP		(*(volatile unsigned short *)(0x26000000 + 0x28000))
#define SDRAM_FLAYOUT	(*(volatile unsigned short *)(0x26000000 + 0x2A000))
#define SDRAM_BLAYOUT	(*(volatile unsigned short *)(0x26000000 + 0x2A800))
#define SDRAM_PAL		(*(volatile unsigned short *)(0x26000000 + 0x2AA00))

#define SDRAM_UNUSED	(*(volatile unsigned short *)(0x26000000 + 0x2AC00))

/*
DRAM needed for drawing screen:
	(320+16) * (224+16) = 80640 (78.75 KB)

DRAM needed to store quick redraw:
	256 * 192 = 49152 (48 KB)

Total DRAM used:
	80640 + 49152 = 129792 (126.75 KB)

Total DRAM free:
	131072 - 129792 = 1280 (1.25 KB)
*/


//volatile unsigned char sdramArt[0x20000/2];
volatile unsigned char sdramArt[0x20000];
volatile unsigned short sdramMetablockMap[0x8000/2];
volatile unsigned short sdramBlockMap[0x2000/2];
volatile unsigned char sdramFrontLayout[0x800];
volatile unsigned char sdramBackLayout[0x80];
volatile unsigned short sdramPalette[0x200/2];


#define TILE_DRAW_QUEUE_SIZE	(40+1) * (28+1) * 2
volatile unsigned short tileDrawQueue[TILE_DRAW_QUEUE_SIZE];
volatile short tileDrawQueueIndex = 0;


static int bufferSize = ((320*224)/2)+256;

bool cycler = false;
char cyclerIncrement = 1;

bool showDebug = false;
bool showFps = false;

bool clearMdLayer = false;

short buttonsPressed = 0;
short buttonsHeld = 0;

short cameraX = 0x0000;
short cameraY = 0x0200;
short cameraXHistory[2] = {0x0000, 0x0000};
short cameraYHistory[2] = {0x0200, 0x0200};

bool updateTileDrawQueue = true;
bool updateFrameBufferLineTable[2] = {true, true};


void initColorPalette() {
	volatile unsigned short *palette = &MARS_CRAM;
	
	for (int i=0; i < 256; i++) {
		palette[i] = palEHZ[i];
	}
}

void loadPalette() {
	for (int i=0; i < (0x200/2); i++) {
		sdramPalette[i] = palEHZ[i];
	}
}

void loadArt() {
	volatile unsigned short *sdramArt16 = (volatile unsigned short *)sdramArt;
	
	for (int i=0; i < (0x20000/2); i++) {
		sdramArt16[i] = artEHZ[i];
	}
}

void loadBlockMap() {
	for (int i=0; i < (0x2000/2); i++) {
		sdramBlockMap[i] = bmapEHZ[i];
	}
}

void loadMetablockMap() {
	for (int i=0; i < (0x8000/2); i++) {
		sdramMetablockMap[i] = metaEHZ[i];
	}
}

void loadFrontLayout() {
	for (int i=0; i < (0x800); i++) {
		sdramFrontLayout[i] = flayoutEHZ1[i];
	}
	
	sdramFrontLayout[0x00] = 0x02;
	sdramFrontLayout[0x01] = 0x03;
	sdramFrontLayout[0x02] = 0x04;
	sdramFrontLayout[0x03] = 0x05;
	sdramFrontLayout[0x04] = 0x06;
	sdramFrontLayout[0x05] = 0x07;
	sdramFrontLayout[0x80] = 0x0F;
	sdramFrontLayout[0x81] = 0x0F;
	sdramFrontLayout[0x82] = 0x0F;
	sdramFrontLayout[0x83] = 0x0F;
	sdramFrontLayout[0x84] = 0x0F;
	sdramFrontLayout[0x85] = 0x0F;
	//sdramFrontLayout[0x80] = 0x0F;
	//sdramFrontLayout[0x81] = 0x26;
	//sdramFrontLayout[0x82] = 0x0F;
}

void loadBackLayout() {
	for (int i=0; i < (0x80); i++) {
		sdramBackLayout[i] = blayoutEHZ1[i];
	}
}


void drawHeader() {	
	//HwMdPuts((char*)"\n  ==== ProSonic Engine 32X ====", 0x2000, 2, 2);
	HwMdPuts((char*)"TESTING", 0x4000, 2, 4);
}



void queueMetablockTiles(unsigned char metaBlock, short offsetX, short offsetY) {
	// Grab blocks referenced by the metablock.
	short redrawWidth = cameraX - cameraXHistory[0];	//TODO: Finish working with this!
	short redrawHeight = cameraY - cameraYHistory[0];	//TODO: Finish working with this!
	char startX = (offsetX < 0) ? (((cameraX) & 0x78) >> 3) : 0;
	char startY = (offsetY < 0) ? (((cameraY) & 0x78) << 1) : 0;
	char endX = (offsetX > (320-128+7)) ? (((320+7) - offsetX) >> 3) : 16;
	char endY = (offsetY > (224-128+7)) ? (((224+7) - offsetY) >> 3) : 16;
	char quantityX = endX - startX;
	char quantityY = endY - startY;
	
	char metaBlockSegment = (startY << 2) | (startX >> 1);
	char segmentIncrementX = 8 - ((quantityX+1) >> 1);
	char segmentIncrementY = 8 - ((quantityY+1) >> 1);
	
	//short tileDrawQueueOffset = tileDrawQueueIndex + ((((((offsetY+128+7) >> 3)&0xFF)-16) + (startY<<1)) * (40+1)) + ((((offsetX+128+7) >> 3)&0xFF)-16) + (startX<<1);
	short tileDrawQueueOffset = tileDrawQueueIndex;
	if (offsetY > 0) {
		tileDrawQueueOffset += (((offsetY+7) >> 3) * (40+1));
	}
	if (offsetX > 0) {
		tileDrawQueueOffset += ((offsetX+7) >> 3);
	}
	
	for (int y=startY; y < endY; y += 2) {
		for (int x=startX; x < endX; x += 2) {
			unsigned short blockBaseOffset = (sdramMetablockMap[(metaBlock<<6) + metaBlockSegment] & 0xFFC);
			
			// Grab tiles referenced by the block.
			if (x & 1) {
				tileDrawQueue[tileDrawQueueOffset] = (sdramBlockMap[blockBaseOffset+1] & 0xFFE0);
				tileDrawQueue[tileDrawQueueOffset+(40+1)] = (sdramBlockMap[blockBaseOffset+3] & 0xFFE0);
				tileDrawQueueOffset++;
				x--;
			} else if (x == (endX-1)) {
				tileDrawQueue[tileDrawQueueOffset] = (sdramBlockMap[blockBaseOffset] & 0xFFE0);
				tileDrawQueue[tileDrawQueueOffset+(40+1)] = (sdramBlockMap[blockBaseOffset+2] & 0xFFE0);
				tileDrawQueueOffset++;
			} else {
				tileDrawQueue[tileDrawQueueOffset] = (sdramBlockMap[blockBaseOffset] & 0xFFE0);
				tileDrawQueue[tileDrawQueueOffset+1] = (sdramBlockMap[blockBaseOffset+1] & 0xFFE0);
				tileDrawQueue[tileDrawQueueOffset+(40+1)] = (sdramBlockMap[blockBaseOffset+2] & 0xFFE0);
				tileDrawQueue[tileDrawQueueOffset+(40+1)+1] = (sdramBlockMap[blockBaseOffset+3] & 0xFFE0);
				tileDrawQueueOffset += 2;
			}
			
			metaBlockSegment++;
		}
		
		tileDrawQueueOffset += ((40+1)*2) - quantityX;
		metaBlockSegment += segmentIncrementX;
	}
	
	updateTileDrawQueue = false;
}

void queueTiles() {
	//cameraX = 80;
	//cameraY = 512;
	
	//previousCameraX = 80;
	//previousCameraY = 512;
	
	unsigned int bufferWidth = (320+16)/2;
	unsigned int metaBlockOffsetX = cameraX>>1;
	unsigned int metaBlockOffsetY = cameraY;
	
	unsigned short levelLayoutIndex = (cameraY & 0xFF80) | (cameraX >> 7);
	
	unsigned char metaBlockArray[3][4];
	
	metaBlockArray[0][0] = sdramFrontLayout[levelLayoutIndex];
	metaBlockArray[0][1] = sdramFrontLayout[levelLayoutIndex+1];
	metaBlockArray[0][2] = sdramFrontLayout[levelLayoutIndex+2];
	metaBlockArray[0][3] = sdramFrontLayout[levelLayoutIndex+3];
	
	metaBlockArray[1][0] = sdramFrontLayout[levelLayoutIndex+128];
	metaBlockArray[1][1] = sdramFrontLayout[levelLayoutIndex+129];
	metaBlockArray[1][2] = sdramFrontLayout[levelLayoutIndex+130];
	metaBlockArray[1][3] = sdramFrontLayout[levelLayoutIndex+131];
	
	metaBlockArray[2][0] = sdramFrontLayout[levelLayoutIndex+256];
	metaBlockArray[2][1] = sdramFrontLayout[levelLayoutIndex+257];
	metaBlockArray[2][2] = sdramFrontLayout[levelLayoutIndex+258];
	metaBlockArray[2][3] = sdramFrontLayout[levelLayoutIndex+259];
	
	for (int y=0; y < 3; y++) {
		for (int x=0; x < 4; x++) {
			queueMetablockTiles(
					metaBlockArray[y][x],
					(x<<7) - (cameraX & 0x7F),
					(y<<7) - (cameraY & 0x7F));
		}
	}
	//queueMetablockTiles(15, 0, 112);
	
	/*tileDrawQueue[((40+1)*(28+1))+0] = 'S';
	tileDrawQueue[((40+1)*(28+1))+1] = 'E';
	tileDrawQueue[((40+1)*(28+1))+2] = 'G';
	tileDrawQueue[((40+1)*(28+1))+3] = 'A';
	tileDrawQueue[((40+1)*(28+1))+4] = ' ';
	tileDrawQueue[((40+1)*(28+1))+5] = ' ';
	tileDrawQueue[((40+1)*(28+1))+6] = ' ';
	tileDrawQueue[((40+1)*(28+1))+7] = ' ';*/
}


// Word-based draw
void drawLevel() {
	volatile unsigned short *frameBuffer16 = &(*(volatile unsigned short *)0x24000000);
	volatile unsigned short *sdramArt16 = (volatile unsigned short *)sdramArt;
	
	// Has the camera been moved at all? If so, flag the frame buffers for line table updates.
	if (cameraX != cameraXHistory[0] || cameraY != cameraYHistory[0]) {
		updateFrameBufferLineTable[0] = true;
		updateFrameBufferLineTable[1] = true;
	}
	
	bool pixelShift = MARS_VDP_SCRSFT & 1;
	
	// Update the line table if needed.
	if (updateFrameBufferLineTable[MARS_VDP_FBCTL & 1]) {
		unsigned short wrapX = cameraX;
		unsigned short wrapY = cameraY % 224;
		
		unsigned short lineOffset = 0x100 + (((wrapY * 578) + wrapX) >> 1);
		for (unsigned short line=0; line < 224; line++) {
			if (lineOffset < 0x100) {
				// Wrap to the end of the frame buffer.
				lineOffset += (((578*224)-320) >> 1);
			} else if (lineOffset >= (((578*224)-320) >> 1) + 0x100) {
				// Wrap to the beginning of the frame buffer.
				lineOffset -= (((578*224)-320) >> 1);
			}
			
			if ((lineOffset & 0xFF) == 0xFF) {
				// Use alternative line offset to accommodate for pixel shift bug on real hardware.
				frameBuffer16[line] = ((578*224) >> 1) + 0x100;
			} else {
				frameBuffer16[line] = lineOffset;
			}
			
			lineOffset += (578 >> 1);
		}
		
		// The line table of this frame no longer needs to be updated.
		updateFrameBufferLineTable[MARS_VDP_FBCTL & 1] = false;
	}
	
	int tileIndex = tileDrawQueueIndex;
	unsigned short frameBufferOffset;
	unsigned short shiftX = (cameraX & 7) >> 1; // Align tile offset to camera's X position within four words.
	unsigned short shiftY = (cameraY & 7) << 2; // Align tile offset to camera's Y position within eight lines.
	
	int tilePixelY = shiftY;
	
	// Plot pixels onto the frame buffer for each line.
	for (unsigned char line = 0; line < 224; line++) {
		frameBufferOffset = frameBuffer16[line];
		
		// Handle 1st tile.
		unsigned int tileOffset = tileDrawQueue[tileIndex++] + tilePixelY + shiftX;
		for (unsigned char tilePixelX = shiftX; tilePixelX < (8/2); tilePixelX++) {
			frameBuffer16[frameBufferOffset++] = sdramArt16[tileOffset++];
		}
		
		// Handle the next 39 tiles.
		for (unsigned char tileX = 1; tileX < 40; tileX++) {
			tileOffset = tileDrawQueue[tileIndex++] + tilePixelY;
			frameBuffer16[frameBufferOffset++] = sdramArt16[tileOffset++];
			frameBuffer16[frameBufferOffset++] = sdramArt16[tileOffset++];
			frameBuffer16[frameBufferOffset++] = sdramArt16[tileOffset++];
			frameBuffer16[frameBufferOffset++] = sdramArt16[tileOffset++];
		}
		
		// Handle 41st tile if needed.
		tileOffset = tileDrawQueue[tileIndex++] + tilePixelY;
		for (unsigned char tilePixelX = 0; tilePixelX < (shiftX + pixelShift); tilePixelX++) {
			frameBuffer16[frameBufferOffset++] = sdramArt16[tileOffset++];
		}
		
		// Go to the next tile line.
		tilePixelY += 4;
		tilePixelY &= 31;
		
		if (tilePixelY != 0) {
			// We haven't drawn all the tile lines yet, so reset the 'tileIndex' value.
			tileIndex -= 41;
		}
	}
	
	// TESTING: White pixels
	/*frameBuffer16[0x4940] = 0x4646;
	frameBuffer16[0x4941] = 0x4747;
	frameBuffer16[0x4942] = 0x4646;
	frameBuffer16[0x4943] = 0x4747;
	frameBuffer16[0x4944] = 0x4646;
	frameBuffer16[0x4945] = 0x4747;
	frameBuffer16[0x4946] = 0x4646;
	frameBuffer16[0x4947] = 0x4747;
	
	// TESTING: Red pixels
	frameBuffer16[0x100] = 0x4C4C;
	frameBuffer16[0x101] = 0x4D4D;
	frameBuffer16[0x102] = 0x4C4C;
	frameBuffer16[0x103] = 0x4D4D;
	frameBuffer16[0x104] = 0x4C4C;
	frameBuffer16[0x105] = 0x4D4D;
	frameBuffer16[0x106] = 0x4C4C;
	frameBuffer16[0x107] = 0x4D4D;*/
}

void drawScreen(int metaBlock) {
	if (clearMdLayer) {
		HwMdClearScreen();
		drawHeader();
		clearMdLayer = 0;
	}
	
	if (showDebug) {
		HwMdPrintHexValue(5, 6, 0x0000, cameraX, 4);
		HwMdPrintHexValue(10, 6, 0x0000, cameraY, 4);
		
		HwMdPrintHexValue(1, 16, 0x0000, tileDrawQueue[(41*0) + 0x00], 4);
		HwMdPrintHexValue(1, 17, 0x0000, tileDrawQueue[(41*0) + 0x01], 4);
		HwMdPrintHexValue(1, 18, 0x0000, tileDrawQueue[(41*0) + 0x02], 4);
		HwMdPrintHexValue(1, 19, 0x0000, tileDrawQueue[(41*0) + 0x03], 4);
		HwMdPrintHexValue(1, 20, 0x0000, tileDrawQueue[(41*0) + 0x04], 4);
		HwMdPrintHexValue(1, 21, 0x0000, tileDrawQueue[(41*0) + 0x05], 4);
		HwMdPrintHexValue(1, 22, 0x0000, tileDrawQueue[(41*0) + 0x06], 4);
		HwMdPrintHexValue(1, 23, 0x0000, tileDrawQueue[(41*0) + 0x07], 4);
		HwMdPrintHexValue(6, 16, 0x0000, tileDrawQueue[(41*2) + 0x00], 4);
		HwMdPrintHexValue(6, 17, 0x0000, tileDrawQueue[(41*2) + 0x01], 4);
		HwMdPrintHexValue(6, 18, 0x0000, tileDrawQueue[(41*2) + 0x02], 4);
		HwMdPrintHexValue(6, 19, 0x0000, tileDrawQueue[(41*2) + 0x03], 4);
		HwMdPrintHexValue(6, 20, 0x0000, tileDrawQueue[(41*2) + 0x04], 4);
		HwMdPrintHexValue(6, 21, 0x0000, tileDrawQueue[(41*2) + 0x05], 4);
		HwMdPrintHexValue(6, 22, 0x0000, tileDrawQueue[(41*2) + 0x06], 4);
		HwMdPrintHexValue(6, 23, 0x0000, tileDrawQueue[(41*2) + 0x07], 4);
		
		HwMdPrintHexValue(30, 16, 0x0000, tileDrawQueue[(41*0) + 0x20], 4);
		HwMdPrintHexValue(30, 17, 0x0000, tileDrawQueue[(41*0) + 0x21], 4);
		HwMdPrintHexValue(30, 18, 0x0000, tileDrawQueue[(41*0) + 0x22], 4);
		HwMdPrintHexValue(30, 19, 0x0000, tileDrawQueue[(41*0) + 0x23], 4);
		HwMdPrintHexValue(30, 20, 0x0000, tileDrawQueue[(41*0) + 0x24], 4);
		HwMdPrintHexValue(30, 21, 0x0000, tileDrawQueue[(41*0) + 0x25], 4);
		HwMdPrintHexValue(30, 22, 0x0000, tileDrawQueue[(41*0) + 0x26], 4);
		HwMdPrintHexValue(30, 23, 0x0000, tileDrawQueue[(41*0) + 0x27], 4);
		HwMdPrintHexValue(35, 16, 0x0000, tileDrawQueue[(41*2) + 0x20], 4);
		HwMdPrintHexValue(35, 17, 0x0000, tileDrawQueue[(41*2) + 0x21], 4);
		HwMdPrintHexValue(35, 18, 0x0000, tileDrawQueue[(41*2) + 0x22], 4);
		HwMdPrintHexValue(35, 19, 0x0000, tileDrawQueue[(41*2) + 0x23], 4);
		HwMdPrintHexValue(35, 20, 0x0000, tileDrawQueue[(41*2) + 0x24], 4);
		HwMdPrintHexValue(35, 21, 0x0000, tileDrawQueue[(41*2) + 0x25], 4);
		HwMdPrintHexValue(35, 22, 0x0000, tileDrawQueue[(41*2) + 0x26], 4);
		HwMdPrintHexValue(35, 23, 0x0000, tileDrawQueue[(41*2) + 0x27], 4);
		
		//HwMdPrintHexValue(35, 13, 0x4000, (*(volatile unsigned short *)(0x24000000+(31*2))), 4);
		
		HwMdPrintHexValue(5, 8, 0x0000, tileDrawQueueIndex, 4);
		HwMdPrintHexValue(5, 9, 0x0000, MARS_FRAMEBUFFER, 4);
		HwMdPrintHexValue(10, 9, 0x0000, MARS_VDP_SCRSFT, 1);
		HwMdPrintHexValue(5, 11, 0x0000, updateFrameBufferLineTable[0], 1);
		HwMdPrintHexValue(7, 11, 0x0000, updateFrameBufferLineTable[1], 1);
	}
	
	if (updateTileDrawQueue) {
		queueTiles();
	}
	drawLevel();
	
	Hw32xScreenFlip(true);
	
	if (cameraX & 1) {
		MARS_VDP_SCRSFT |= 1;		// Set flag
	} else {
		MARS_VDP_SCRSFT &= 0xFFFE;	// Clear flag
	}
}


int main(void)
{
	unsigned short fps10 = 0;
	unsigned int frameCount = -1;
	unsigned int previousVblankCount;
	short buttonRead = 0;
	short timeInterval = -1;
	
	int metaBlock = 0;
	
	Hw32xInit(MARS_VDP_MODE_256, 0);
	
	Hw32xSetFGColor(255, 16, 24, 16);
	Hw32xSetBGColor(0, 6, 12, 6);
	
	drawHeader();
	
	
	loadArt();
	loadMetablockMap();
	loadBlockMap();
	loadFrontLayout();
	loadPalette();
	
	
	initColorPalette();
	
	
	while (true)
	{
		timeInterval += 1;
		volatile unsigned short *frameBuffer16 = &MARS_FRAMEBUFFER;
		
		Hw32xDelay(1);
		
		short buttonsRead = HwMdReadPad(0);
		buttonsPressed = buttonsRead & (~buttonsHeld);
		buttonsHeld = buttonsRead;
		
		if (buttonsHeld & SEGA_CTRL_UP) {
			if (buttonsHeld & SEGA_CTRL_MODE) {
				cameraY -= 8;
			} else {
				cameraY -= 1;
			}
			updateTileDrawQueue = true;
		}
		if (buttonsHeld & SEGA_CTRL_DOWN) {
			if (buttonsHeld & SEGA_CTRL_MODE) {
				cameraY += 8;
			} else {
				cameraY += 1;
			}
			updateTileDrawQueue = true;
		}
		if (buttonsHeld & SEGA_CTRL_LEFT) {
			if (buttonsHeld & SEGA_CTRL_MODE) {
				cameraX -= 8;
			} else {
				cameraX -= 1;
			}
			updateTileDrawQueue = true;
		}
		if (buttonsHeld & SEGA_CTRL_RIGHT) {
			if (buttonsHeld & SEGA_CTRL_MODE) {
				cameraX += 8;
			} else {
				cameraX += 1;
			}
			updateTileDrawQueue = true;
		}
		if (buttonsPressed & SEGA_CTRL_A) {
			Hw32xSetBGColor(0, 12, 6, 6);
		}
		if (buttonsPressed & SEGA_CTRL_B) {
			Hw32xSetBGColor(0, 6, 12, 6);
		}
		if (buttonsPressed & SEGA_CTRL_C) {
			Hw32xSetBGColor(0, 6, 6, 12);
		}
		if (buttonsPressed & SEGA_CTRL_START) {
			Hw32xSetBGColor(0, 0, 0, 0);
		}
		if (buttonsPressed & SEGA_CTRL_X) {
			showFps ^= true;
			clearMdLayer = 1;
			frameCount = -1;
		}
		if (buttonsPressed & SEGA_CTRL_Y) {
			showDebug ^= true;
			clearMdLayer = 1;
			frameCount = -1;
		}
		if (buttonsPressed & SEGA_CTRL_Z) {
		}
		if (buttonsPressed & SEGA_CTRL_MODE) {
		}
		
		
		if (cameraX < 0) {
			cameraX = 0;
		} else if (cameraX > 0x3E80) {
			cameraX = 0x3E80;
		}
		
		if (cameraY < 0) {
			cameraY = 0;
		} else if (cameraY > 0x700) {
			cameraY = 0x700;
		}
		
		tileDrawQueueIndex += ((cameraX & 0xFFF8) - (cameraXHistory[0] & 0xFFF8)) >> 3;
		tileDrawQueueIndex += (40+1) * (((cameraY & 0xFFF8) - (cameraYHistory[0] & 0xFFF8)) >> 3);
		
		if (tileDrawQueueIndex < 0) {
			tileDrawQueueIndex += ((40+1)*(28+1));
		} else if (tileDrawQueueIndex > (40+1)*(28+1)) {
			tileDrawQueueIndex -= ((40+1)*(28+1));
		}
		
		drawScreen(metaBlock);
		
		if (showFps) {
			frameCount++;
			
			if (frameCount == 0) {
				HwMdPrintFloatValue(1, 1, 0x0000, fps10, 2, 1);
				//previousVblankCount = MARS_SYS_COMM12;	//Hw32xGetVblankCount();
			}
			
			if (frameCount == 60) {
				unsigned int currentVblankCount = Hw32xGetVblankCount();
				fps10 = 600000 / (((currentVblankCount - previousVblankCount) * 100) / 6);
				
				HwMdPrintFloatValue(1, 1, 0x0000, fps10, 2, 1);
				HwMdPrintDecValue(10, 1, 0x0000, currentVblankCount, 5);
				HwMdPrintDecValue(16, 1, 0x0000, previousVblankCount, 5);
				
				frameCount = 0;
				previousVblankCount = currentVblankCount;
			}
		}
		
		cameraXHistory[1] = cameraXHistory[0];
		cameraYHistory[1] = cameraYHistory[1];
		cameraXHistory[0] = cameraX;
		cameraYHistory[0] = cameraY;
		
		
		if (cycler) {
			metaBlock = ((metaBlock + cyclerIncrement) & 0xFF);
		}
		
		if ((timeInterval & 16) == 0) {
			HwMdPutc('_', 0x2000, 2, 6);
		} else {
			HwMdPutc(' ', 0x2000, 2, 6);
		}
	}
}