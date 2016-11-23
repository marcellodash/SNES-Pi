/*
 * 23s17.c:
 *      WiringPi test with an MCP23S17 SPI GPIO expander chip
 *
 * Copyright (c) 2012-2013 Gordon Henderson. <projects@drogon.net>
 ***********************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <wiringPi.h>
#include <mcp23s17.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "wiringPiSPI.h"
#include <time.h>

#define BASE    123
// ------------ Setup Register Definitions ------------------------------------------

#define _SNESAddressPins 0x20 // MCP23017 Chip with SNES Address Pins
#define _SNESBankAndData 0x22 // MCP23017 Chip with SNES Bank and Data
#define _IOControls 0x23        // MCP23017 Chip to control SNES IO Controls including MOSFET Power

#define IODIRA 0X00
#define IODIRB 0X01
#define GPIOA 0X12
#define GPIOB 0X13
#define GPINTENB 0x05
#define DEFVALB 0x07
#define INTCONB x09
#define IOCON_B 0x0B
#define GPPUB 0x0D

/*
# GPA0: /RD
# GPA1: /RESET
# GPA2: /WR
# GPA3: /CS
# GPA4: CART MOSFET
# GPA7: /IRQ 
*/	

#define _RD    0b00000001
#define _RESET 0b00000010
#define _WR    0b00000100
#define _CS    0b00001000
#define _POWER 0b00010000

int16_t currentBank = -1;
uint32_t ROMchecksum = 0;
uint32_t totalChecksum = 0;
uint32_t LowByteWrites = 0;
uint32_t HighByteWrites = 0;
uint32_t BankWrites = 0;
uint32_t DataReads = 0;


uint8_t readData(void);
void gotoAddr(int32_t, int);
void gotoBank(int16_t);
uint8_t readAddr(int32_t, int);
uint8_t readAddrBank(int32_t, uint8_t);
void gotoOffset(uint32_t,int);
uint8_t readOffset(uint32_t,int);
uint8_t getUpNibble(uint8_t);
uint8_t getLowNibble(uint8_t);
int power(int, unsigned int);
int16_t getROMsize(uint32_t, int);
int16_t getNumberOfPages(int16_t,int);
const char * returnNULLheader(void);
void CX4setROMsize(int16_t);
void ripROM (uint8_t, int, int16_t, uint8_t *);
void setIOControl(uint8_t );
void initInterface(void);
void shutdownInterface(void);



#define      CMD_WRITE       0x40
#define CMD_READ     0x41
/*
 * writeByte:
 *	Write a byte to a register on the MCP23s17 on the SPI bus.
 *********************************************************************************
 */

void writeByte (uint8_t spiPort, uint8_t devId, uint8_t reg, uint8_t data)
{
  uint8_t spiData [4] ;

  spiData [0] = CMD_WRITE | ((devId & 7) << 1) ;
  spiData [1] = reg ;
  spiData [2] = data ;

  wiringPiSPIDataRW (spiPort, spiData, 3) ;
}

/*
 * readByte:
 *	Read a byte from a register on the MCP23s17 on the SPI bus.
 *********************************************************************************
 */

uint8_t readByte (uint8_t spiPort, uint8_t devId, uint8_t reg){
  uint8_t spiData [4] ;

  spiData [0] = CMD_READ | ((devId & 7) << 1) ;
  spiData [1] = reg ;

  wiringPiSPIDataRW (spiPort, spiData, 3) ;

  return spiData [2] ;
}

uint8_t readData(void){
	DataReads++;
	return readByte (0, _SNESBankAndData, GPIOB); // SNESBankAndData._readRegister(GPIOB);
}

void gotoAddr(int32_t addr, int isLowROM){
	static int32_t currentAddr = -1;
	static int32_t currentUpByte = -1;
	static int32_t currentLowByte = -1;
	uint16_t upByte;
	uint16_t lowByte;
	
	if (addr <= 0xffff){
		upByte = (uint8_t)(addr/256);
		lowByte = (uint8_t)(addr - (upByte * 256));
		currentAddr = addr;
	
  
		if (isLowROM != 0){
			upByte = (upByte | 0x80); // ORs a 1 to A15 if LoROM
		}

		if (currentUpByte != upByte){
			writeByte (0, _SNESAddressPins, GPIOB, upByte); //SNESAddressPins._writeRegister(GPIOB,upByte);
			currentUpByte = upByte;
			HighByteWrites++;
		}
   
		if (currentLowByte != lowByte){
			writeByte (0, _SNESAddressPins, GPIOA, lowByte);//SNESAddressPins._writeRegister(GPIOA,lowByte)
			currentLowByte = lowByte;  
			LowByteWrites++;
		}
	}
	
	else{
		writeByte (0, _SNESAddressPins, GPIOA, 0x00);//SNESAddressPins._writeRegister(GPIOA,0x00)
		writeByte (0, _SNESAddressPins, GPIOB, 0x00);//SNESAddressPins._writeRegister(GPIOB,0x00)
		currentAddr = 0;
 }
		
	
}

void gotoBank(int16_t bank){
	//static int16_t currentBank = -1;
	
	if (bank != currentBank){
		writeByte (0, _SNESBankAndData, GPIOA, bank);//SNESBankAndData._writeRegister(GPIOA,bank)
		currentBank = bank;
		BankWrites++;
	}
	
}

uint8_t readAddr(int32_t addr, int isLowROM){
	gotoAddr(addr,isLowROM); 
	return readData();	
}

uint8_t readAddrBank(int32_t addr, uint8_t bank){
	gotoBank(bank); 
	gotoAddr(addr,0);
	return readData();
}
 
void gotoOffset(uint32_t offset,int isLowROM){
	//static uint32_t currentOffset = 0;
	//	printf("Forth\n");
	uint8_t bank = 0;
	uint32_t addr = 0;

	if (isLowROM == 0){
		bank = (uint8_t)( offset / 65536); //64Kilobyte pages
		addr = offset - (bank * 65536); //64kilobyte pages
	}

	else{
		bank = (uint8_t)( offset / 32768); //32kilobyte pages
		addr = offset - (bank * 32768); //32kilobyte pages
	}
    //printf("Fifth\n");
	gotoBank(bank);
	gotoAddr(addr,isLowROM);
  //  printf("BANK: %d, ADDR: %d\n",bank, addr);
	//currentOffset = offset;
}

uint8_t readOffset(uint32_t offset,int isLowROM){
	//printf("Seventh\n");
	gotoOffset(offset,isLowROM);
	return readData();
}
 
int compareROMchecksums(header,isLowROM){
	uint32_t currentOffset;
	uint32_t inverseChecksum;
	uint32_t ROMchecksum;
	//printf("Third\n");
 if (isLowROM == 1)
  writeByte (0, _IOControls, GPIOA, 0x06);//IOControls._writeRegister(GPIOA,0x06)#reset

//printf("Sixth\n");

 currentOffset = header + 28;
 inverseChecksum  = readOffset(currentOffset,isLowROM);
 inverseChecksum += readOffset(currentOffset+1,isLowROM) * 256;
 printf("Inverse Checksum: %X\n",inverseChecksum );

 currentOffset = header + 30;

 ROMchecksum  = readOffset(currentOffset,isLowROM);
 ROMchecksum += readOffset(currentOffset+1,isLowROM) * 256;
 printf( "Checksum: %X\n",ROMchecksum );


 if ( (inverseChecksum ^ ROMchecksum) == 0xFFFF)
  return 1;
 else
  return 0;
  }

uint8_t getUpNibble(uint8_t value){
 return (uint8_t)(value/16);
}

uint8_t getLowNibble(uint8_t value){
 return ( value - (getUpNibble(value) * 16) );
}

int power(int base, unsigned int exp) {
    int i, result = 1;
    for (i = 0; i < exp; i++)
        result *= base;
    return result;
 }

int16_t getROMsize(uint32_t offset, int isLowROM){
	uint8_t ROMsizeRegister = readOffset(offset,isLowROM);
	ROMsizeRegister -= 7;

	if (ROMsizeRegister >=0)
		return  power(2, ROMsizeRegister);
	else
		return -1;
}

int16_t getNumberOfPages(int16_t actualROMsize,int isLowROM){
	actualROMsize *= 2;
	if (isLowROM == 1)
		actualROMsize *= 2;
	
	return actualROMsize;
	}

const char * returnNULLheader(void){
 static char charStr[512] = "";
 return charStr;
}

void CX4setROMsize(int16_t ROMsize){
	gotoOffset(0x007F52,0);
	uint8_t ROMsizeRegister = readData();
	printf("$007F52 offset reads    %u", ROMsizeRegister );
	writeByte (0, _SNESBankAndData, IODIRB, 0x00);//SNESBankAndData._writeRegister(IODIRB,0x00) # Set MCP bank B to outputs  (SNES Data 0-7)
	writeByte (0, _IOControls, GPIOA, 0x03);//IOControls._writeRegister(GPIOA,0x03)#reset + /RD high
	
	/*
	# GPA0: /RD
	# GPA1: /RESET
	# GPA2: /WR
	# GPA3: /CS
	# GPA4: CART MOSFET
	# GPA7: /IRQ
	*/
	
	if (ROMsize > 8){
		if (ROMsizeRegister == 1){
			printf("ROM is larger than 8 megs, writing 0x00 to CX4 register");
			writeByte (0, _SNESBankAndData, GPIOB, 0x00);//SNESBankAndData._writeRegister(GPIOB,0x00)
		}
		
		else
			printf("CX4 register is at correct value, will not change");
	}

	else{
		if (ROMsizeRegister == 1)
			printf("CX4 register is at correct value, will not change");
		else{
			printf("ROM is 8 megs, writing 0x01 to CX4 register");
			writeByte (0, _SNESBankAndData, GPIOB, 0x01);//SNESBankAndData._writeRegister(GPIOB,0x01)
		}
	}
 
	writeByte (0, _IOControls, GPIOA, 0x06);//IOControls._writeRegister(GPIOA,0x06)#reset + /WR high
	writeByte (0, _SNESBankAndData, IODIRB, 0xFF);//SNESBankAndData._writeRegister(IODIRB,0xFF) # Set MCP bank B to back to inputs  (SNES Data 0-7)
	printf("$007F52 offset now reads %u",readData() );
}

void ripROM (uint8_t startBank, int isLowROM, int16_t numberOfPages, uint8_t *ROMdump){
	totalChecksum = 0;
 
	//static uint8_t ROMdump[5000000] = "";
	uint32_t pageChecksum = 0;
	uint8_t currentByte = 0;
	uint8_t bank = 0;
	uint32_t position = 0;
	uint32_t j = 0;
	uint32_t startOffset;
	uint32_t offset;
 
	if (isLowROM == 1)
		startOffset = startBank * 0x8000;
	else
		startOffset = startBank * 0x10000;
 
	offset = startOffset;   // Set current Offset to starting offset
	gotoOffset(startOffset,isLowROM); // Change current bank & address to offset

	printf ("----Start Cart Read------\n");
	position = 0;
	//Start at current bank, and increment the number of banks needed
	for (j = startBank; j < (numberOfPages + startBank); j++  ){
		
		printf("Current Bank:  DEC:  %d; HEX: %x\n", currentBank, currentBank );
  
		//If bank increments, exit the following inner loop, else keep scanning
		while (j == currentBank){
			currentByte = readData();
			*(ROMdump + position)  = currentByte;
			pageChecksum += currentByte;
			offset += 1; //Increment offset
			gotoOffset(offset,isLowROM); //goto new offset
			//printf("%d  --  %d:   %d\n", currentByte, *(ROMdump + position), position );
			position++;
		}
		
		if (isLowROM == 0 | (isLowROM == 1 && currentBank % 2 == 0) ){
			printf(" - Page Checksum:        %u\n", pageChecksum ); 
			totalChecksum += pageChecksum;
			pageChecksum = 0;
			printf("\nCurrent Checksum:        %d | Hex: %x\n", totalChecksum, totalChecksum);
			printf("Header Checksum:        %x\n", ROMchecksum);
		}
		
		
	}
  
 //return ROMdump;
 
}

/* def ripSRAM(SRAMsize, ROMsize, isLowROM):
 SRAMdump = ""
 pageChecksum = 0
 currentByte = 0
 bank = 0 

 startBank = 0
 startAddr = 0
 endAddr   = 0x7FFF 
 
 if isLowROM == 1:
  startBank = 0x70  
  startAddr = 0x0000
  endAddr   = 0x7FFF
 else: #Else, is HiROM
  startBank = 0x30
  startAddr = 0x6000
  endAddr   = 0x7FFF
  IOControls._writeRegister(GPIOA,0x0E)# RESET + /WR + /CS high
  # GPA7: /IRQ | GPA4: CART MOSFET | GPA3: /CS | GPA2: /WR | GPA1: /RESET  | GPA0: /RD
  
 SRAMsize = (SRAMsize / 8.0) * 1024
 gotoBank(startBank)
 gotoAddr(startAddr,0)

 while SRAMsize > currentByte:
  currentByte += 1
  SRAMdump += chr( readData() )

  if gotoAddr.currentAddr >= endAddr:
   gotoBank( gotoBank.currentBank + 1)
   gotoAddr(startAddr,0)
  else:  
   gotoAddr( gotoAddr.currentAddr +1, 0)

 IOControls._writeRegister(GPIOA,0x06)#reset + /WR high
 # GPA7: /IRQ | GPA4: CART MOSFET | GPA3: /CS | GPA2: /WR | GPA1: /RESET  | GPA0: /RD

 print str(currentByte) + " SRAM bytes read"

 return SRAMdump
*/ 

void setIOControl(uint8_t IOControls){
/*
# GPA0: /RD
# GPA1: /RESET
# GPA2: /WR
# GPA3: /CS
# GPA4: CART MOSFET
# GPA7: /IRQ 
*/	
//Inverses Power
IOControls = IOControls ^ _POWER;

	writeByte (0, _IOControls, GPIOA, IOControls);
	
}

void initInterface(void){
	
	writeByte (0, _IOControls, IOCON_B, 0x08);//IOControls._writeRegister(IOCON_B,0x08)

	writeByte (0, _SNESAddressPins, IODIRA, 0x00);//SNESAddressPins._writeRegister(IODIRA,0x00) # Set MCP bank A to outputs (SNES Addr 0-7)
	writeByte (0, _SNESAddressPins, IODIRB, 0x00);//SNESAddressPins._writeRegister(IODIRB,0x00) # Set MCP bank B to outputs (SNES Addr 8-15)

	writeByte (0, _SNESBankAndData, IODIRA, 0x00);//SNESBankAndData._writeRegister(IODIRA,0x00) # Set MCP bank A to outputs (SNES Bank 0-7)
	writeByte (0, _SNESBankAndData, IODIRB, 0xFF);//SNESBankAndData._writeRegister(IODIRB,0xFF) # Set MCP bank B to inputs  (SNES Data 0-7)

	writeByte (0, _SNESBankAndData, GPPUB, 0xFF);//SNESBankAndData._writeRegister(GPPUB,0xFF) # Enables Pull-Up Resistors on MCP SNES Data 0-7		
	
	writeByte (0, _IOControls, IODIRA, 0x80);//IOControls._writeRegister(IODIRA,0x80) # Set MCP bank A to outputs; WITH EXCEPTION TO IRQ
	writeByte (0, _IOControls, IODIRB, 0x00);//IOControls._writeRegister(IODIRB,0x00) # Set MCP bank B to outputs 

}

void shutdownInterface(void){
	
	gotoAddr(00,0);
	gotoBank(00);

	writeByte (0, _SNESBankAndData, GPPUB, 0x00);//SNESBankAndData._writeRegister(GPPUB,0x00) # Disables Pull-Up Resistors on MCP SNES Data 0-7
	writeByte (0, _SNESBankAndData, DEFVALB, 0xFF);//SNESBankAndData._writeRegister(DEFVALB,0xFF) # Expect MCP SNES Data 0-7 to default to 0xFF
	writeByte (0, _SNESBankAndData, GPINTENB, 0x00);//SNESBankAndData._writeRegister(GPINTENB,0x00) # Sets up all of SNES Data 0-7 to be interrupt disabled

	writeByte (0, _SNESAddressPins, IODIRA, 0xFF);//SNESAddressPins._writeRegister(IODIRA,0xFF) # Set MCP bank A to outputs (SNES Addr 0-7)
	writeByte (0, _SNESAddressPins, IODIRB, 0xFF);//SNESAddressPins._writeRegister(IODIRB,0xFF) # Set MCP bank B to outputs (SNES Addr 8-15)

	writeByte (0, _SNESBankAndData, IODIRA, 0xFF);//SNESBankAndData._writeRegister(IODIRA,0xFF) # Set MCP bank A to outputs (SNES Bank 0-7)
	writeByte (0, _SNESBankAndData, IODIRB, 0xFF);//SNESBankAndData._writeRegister(IODIRB,0xFF) # Set MCP bank B to inputs (SNES Data 0-7)

	writeByte (0, _IOControls, IODIRA, 0xEF);//IOControls._writeRegister(IODIRA,0xEF) # Set MCP bank A to inputs; WITH EXCEPTION TO MOSFET

	setIOControl(0); //writeByte (0, _IOControls, GPIOA, 0x10);//IOControls._writeRegister(GPIOA,0x10) #Turn off MOSFET	
	
}




int main(void){
	
	printf("START\n");
	
	int readCart = 1;
	uint8_t ROMmakeup;	
	uint8_t ROMspeed;
	uint8_t bankSize;
	uint8_t ROMtype;
	uint8_t ROMsize;
	uint8_t SRAMsize;
	uint8_t country;
	uint8_t license;
	uint8_t version;
	uint32_t VBLvector;
	uint32_t resetVector;
	uint32_t inverseChecksum;
	
	int i = 0;
	int x = 0;
	// ------------- Set Registers -----------------------------------------------------


	wiringPiSetup () ;
	mcp23s17Setup (BASE, 0, _IOControls) ;
	mcp23s17Setup (BASE+100, 0,_SNESAddressPins) ;
	mcp23s17Setup (BASE+200, 0, _SNESBankAndData) ;

	initInterface();


//----------------------------------------------------------------------------------------------------
/*
# GPA0: /RD
# GPA1: /RESET
# GPA2: /WR
# GPA3: /CS
# GPA4: CART MOSFET
# GPA7: /IRQ 
*/	

setIOControl(_RESET + _WR + _POWER);   //writeByte (0, _IOControls, GPIOA, 0x06);//IOControls._writeRegister(GPIOA,0x06)#reset
//time.sleep(.25)

//-----------------------------------------------------

char cartname[21] = "";

uint32_t headerAddr =32704;
int isLowROM = 1;
int isValid = 0;

if (compareROMchecksums(32704,1) == 1){
	printf("Checksums matched\n");
	ROMmakeup =  readOffset(headerAddr + 21,isLowROM);
	ROMspeed = getUpNibble(ROMmakeup);
	bankSize = getLowNibble(ROMmakeup);

	if (bankSize == 0){
		printf("ROM Makeup match for LoROM. Assuming this is the case!\n");
		isLowROM = 1;
		isValid = 1;
	}
	
	else if (bankSize == 1){
		printf("ROM Makeup match for HiROM. Assuming this is the case!\n");
		headerAddr = 65472;
		isLowROM = 0;
		isValid = 1;
	}
	
	else
		printf("Bank Configuration Read Error\n");
}
else
 printf("Checksums did not match. Either no cart, or cart read error\n");

//#--- Debug. Manually set bank size ----------
//#isLowROM = 1
//#-------------------------------------------


uint32_t currentAddr = headerAddr;
gotoOffset(headerAddr, isLowROM);

for (i = headerAddr; i <(headerAddr + 20); i++ )
 cartname[x++] = readOffset(i,isLowROM);
//cartname = cartname.rstrip() 

ROMmakeup =  readAddr(headerAddr + 21,isLowROM);
ROMspeed = getUpNibble(ROMmakeup);
bankSize = getLowNibble(ROMmakeup);
ROMtype   =  readAddr(headerAddr + 22,isLowROM);
ROMsize   =  getROMsize(headerAddr + 23, isLowROM);
SRAMsize  =  readAddr(headerAddr + 24,isLowROM);
country   =  readAddr(headerAddr + 25,isLowROM);
license   =  readAddr(headerAddr + 26,isLowROM);
version   =  readAddr(headerAddr + 27,isLowROM);

currentAddr = headerAddr + 28;
inverseChecksum  = readAddr(currentAddr,isLowROM);
inverseChecksum += readAddr(currentAddr+1,isLowROM) * 256;

currentAddr = headerAddr + 30;
ROMchecksum  = readAddr(currentAddr,isLowROM);
ROMchecksum += readAddr(currentAddr+1,isLowROM) * 256;

currentAddr = headerAddr + 32;
VBLvector = readAddr(currentAddr,isLowROM);
VBLvector += readAddr(currentAddr+1,isLowROM) * 256;

currentAddr = headerAddr + 34;
resetVector = readAddr(currentAddr,isLowROM);
resetVector += readAddr(currentAddr+1,isLowROM) * 256;



int16_t numberOfPages = getNumberOfPages(ROMsize,isLowROM);


printf("Game Title:         %s\n", cartname);
printf("ROM Makeup:         %d\n", ROMmakeup);
printf(" - ROM Speed:       %d\n", ROMspeed);
printf(" - Bank Size:       %d\n", bankSize);
printf("ROM Type:           %d\n", ROMtype);

if (ROMtype == 243){
	printf("\nCapcom CX4 ROM Type detected!");
	CX4setROMsize(ROMsize);
	printf("\n");
}

printf("ROM Size:           %d  MBits\n", ROMsize);

int convertedSRAMsize = 0;
printf("SRAM Size:          Value: %d",SRAMsize);
if (convertedSRAMsize == 0)
 if (SRAMsize <= 12 && SRAMsize > 0)
  convertedSRAMsize  =  1<<(SRAMsize +3);
printf(" | %d KBits\n", convertedSRAMsize);

printf("Country:            %d\n", country);
printf( "License:            %d\n", license);
printf( "Version:            1.%d\n",version);
printf( "Inverse Checksum:   %x\n", inverseChecksum);
printf( "ROM Checksum:       %x\n", + ROMchecksum);
printf( " - Checksums xOr'ed:   %x\n", (inverseChecksum | ROMchecksum) );
printf( "\n");
printf( "VBL Vector:         %d\n", VBLvector);
printf( "Reset Vector:       %d\n", resetVector);
printf( "\n");
printf( "Number of pages:    %d\n", numberOfPages );
printf( "\n");

uint8_t *dump;
char fileName[30];
//dump = returnNULLheader()
int y = 0;
uint32_t sizeOfCartInBytes = 0;
uint32_t pageChecksum = 0;
//uint32_t totalChecksum = 0;
uint32_t currentByte = 0;
uint32_t numberOfRemainPages = 0;
uint32_t firstNumberOfPages = 0;
time_t timeStart = 0;
time_t timeEnd = 0;

/*
if directory != "" :
 if directory[len(directory)-1] != "/":
  directory += "/"

g = open("/tmp/insertedCart",'w')*/
if (isValid == 1){
 //g.write(cartname)


 if (readCart == 1){
  //if os.path.exists(directory + cartname + '.smc'){
  // printf("Cart has already been ripped, not ripping again!");
  // readCart = 0;
  
  }
 
 
 else if (readCart == 0)  
  printf("Will not rip cart due to OPTs");
   
 if (readCart == 1){  
 
 
  numberOfRemainPages = 0;
  firstNumberOfPages = numberOfPages; 
  timeStart = time(NULL);
  
  //f = open(directory + cartname + '.smc','w')
  FILE *romfile;
  stpcpy(fileName, cartname);
  strcat(fileName, ".smc");
  romfile = fopen( fileName, "wb");
 
 
  if (isLowROM == 1){

   sizeOfCartInBytes = numberOfPages * 32768;  
   dump = calloc(sizeOfCartInBytes, sizeof(uint8_t) );
   printf("Reading %d Low ROM pages.\n", numberOfPages);

   /*dump =*/ ripROM(0x00, isLowROM, firstNumberOfPages, dump);
  }
  
  else{
	  sizeOfCartInBytes = numberOfPages * 65536;
	  dump = calloc(sizeOfCartInBytes, sizeof(uint8_t) );
   if (numberOfPages > 64){
    numberOfRemainPages = ( numberOfPages - 64 ); //# number of pages over 64
    printf("Reading first 64 of %d Hi ROM pages.\n",  numberOfPages);
    firstNumberOfPages = 64;
   }
   else
    printf("Reading %d Hi ROM pages.\n", numberOfPages);

   /*dump =*/ ripROM(0xC0, isLowROM, firstNumberOfPages, dump); 

   if (numberOfRemainPages > 0){
    printf("Reading last %d of High ROM pages.\n", numberOfRemainPages);
    /*dump +=*/ ripROM(0x40, isLowROM, numberOfRemainPages, dump);
   }
  }

  printf("\n");
  printf("Entire Checksum:             %x\n", totalChecksum);
  printf("\n");
  printf("Header Checksum:             %x\n", ROMchecksum );

  totalChecksum = ( totalChecksum & 0xFFFF );

  printf("16-bit Generated Checksum:   %x\n", totalChecksum);

  if (totalChecksum == ROMchecksum)
   printf("--------------------------   CHECKSUMS MATCH!\n");
  else
   printf("----------WARNING: CHECKSUMS DO NOT MATCH: %x != %x\n", totalChecksum, ROMchecksum);
    
    
  timeEnd = time(NULL);
  //print ""
  printf("Address Writes - LowByte: %d HighByte: %d | Bank Writes: %d | Data Reads: %d\n", LowByteWrites, HighByteWrites, BankWrites, DataReads);
  printf("\nIt took %d seconds to read cart\n", timeEnd - timeStart);
  printf("Size of Cart in Bytes: %d\n", sizeOfCartInBytes);

  fwrite(dump, sizeof(uint8_t), sizeOfCartInBytes, romfile); 
  fclose(romfile);
  free(dump);
 }
 /*if (readSRAM == 1){
  f = open(directory + cartname + '.srm','w')

  timeStart = time.time()
  dump = ripSRAM(convertedSRAMsize,ROMsize,isLowROM)
  timeEnd = time.time()

  print ""
  print "It took " + str(timeEnd - timeStart) + "seconds to read SRAM Data"
  f.write(dump)
  f.close
 }*/
}
else{
 //g.write("NULL")
 //g.close
}


//#--- Clean Up & End Script ------------------------------------------------------

shutdownInterface();


}