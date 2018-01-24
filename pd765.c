/*
    ZEsarUX  ZX Second-Emulator And Released for UniX 
    Copyright (C) 2013 Cesar Hernandez Bano

    This file is part of ZEsarUX.

    ZEsarUX is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>


#include "pd765.h"
#include "cpu.h"
#include "debug.h"
#include "utils.h"
#include "menu.h"
#include "screen.h"
#include "mem128.h"


z80_bit pd765_enabled={0};

z80_bit pd765_motor={0};

z80_byte pd765_index_write_command=0;
z80_byte pd765_index_read_command=0;

//En operacion de read data, dice que byte estamos leyendo
int pd765_read_byte_index=0;

z80_byte pd765_last_command_write=0;

//#define TEMP_VALUE_BUSY 5
//z80_byte pd765_disk_busy=0;

z80_byte pd765_srt=0;
z80_byte pd765_hut=0;
z80_byte pd765_hlt=0;
z80_byte pd765_nd=0;
z80_byte pd765_hd=0;
z80_byte pd765_us0=0;
z80_byte pd765_us1=0;

z80_byte pd765_st0=0;
z80_byte pd765_st1=0;
z80_byte pd765_st2=0;
z80_byte pd765_st3=0;


z80_byte pd765_pcn=0;

z80_byte pd765_ncn=0;

z80_byte pd765_sector=0;
z80_byte pd765_bytes_sector=2;

z80_byte pd765_eot=0;
z80_byte pd765_gpl=0;
z80_byte pd765_dtl=0;


z80_byte buffer_disco[200000];

z80_byte pdc_buffer_retorno[20000];
int pdc_buffer_retorno_len=0;
int pdc_buffer_retorno_index=0;

#define kBusy 0x10
#define kExec 0x20
#define kIO 0x40
#define kRM 0x80

#define kInvalid 0x1f

#define kRvmUPD765StateIdle    0
#define kRvmUPD765StateRead    1
#define kRvmUPD765StateWrite   2
#define kRvmUPD765StateFormat  3
#define kRvmUPD765StateSeek    4
//#define kRvmUPD765StateResult  5
#define kRvmUPD765StateCommand 7

#define kRvmUPD765Seeking      8

#define kRvmUPD765Result 0x80

#define kRvmUPD765Halt 0xff

#define kDriveReady 0x20


#define kReadingData 0x11
#define kReadingDataEnd 0x12
#define kWritingData 0x13
#define kWritingDataEnd 0x14

#define kReadingTrackData 0x15

#define kScanningData 0x16
#define kScanningDataEnd 0x17

int dwstate=0,drstate=0;



int pd765_get_index_memory(int indice)
{
	//ncn pista
	//sector


	//4864 bytes cada salto
	//Inicio 256
	int offset;

	offset=pd765_ncn*4864+pd765_sector*512+indice;
	offset +=256; //del principio de disco
	offset +=256; //que nos situa despues de la cabecera de la pista

	return offset;
}


void pd765_next_sector(void)
{
        pd765_sector++;

        //TODO Esto es totalmente arbitrario, para pistas de 9 sectores
        if (pd765_sector==9) {
                pd765_sector=0;
                pd765_ncn++;
        }
}


void pd765_read_sector(int indice_destino)
{
        //ncn pista
        //sector

	printf ("Reading full sector at track %d sector %d\n",pd765_ncn,pd765_sector);

	int offset=pd765_get_index_memory(0);
	pd765_next_sector();

	//copiamos 512 bytes

//z80_byte buffer_disco[200000];

//z80_byte pdc_buffer_retorno[20000];

	int i;
	z80_byte byte_leido;
	for (i=0;i<512;i++) {
		byte_leido=buffer_disco[offset++];
		pdc_buffer_retorno[indice_destino++]=byte_leido;
	}


}



int contador_recallibrate_temp=0;

//Se deberia inicializar a 128 al hacer reset
z80_byte pd765_status_register=128;

void pd765_enable(void)
{
	debug_printf (VERBOSE_INFO,"Enabling PD765");
	pd765_enabled.v=1;

	//Leer disco de prueba


        FILE *ptr_configfile;
        ptr_configfile=fopen("disco.dsk","rb");

        if (!ptr_configfile) {
                debug_printf(VERBOSE_ERR,"Unable to open disco de prueba");
                return;
        }

        //int leidos=fread(buffer_disco,1,200000,ptr_configfile);
        fread(buffer_disco,1,200000,ptr_configfile);


        fclose(ptr_configfile);
}

void pd765_disable(void)
{
        debug_printf (VERBOSE_INFO,"Disabling PD765");
        pd765_enabled.v=0;
}


void pd765_motor_on(void)
{
	if (pd765_motor.v==0) {
		menu_putstring_footer(WINDOW_FOOTER_ELEMENT_X_DISK,1,"DISK",WINDOW_FOOTER_PAPER,WINDOW_FOOTER_INK);
		pd765_motor.v=1;
	}
}

void pd765_motor_off(void)
{
	if (pd765_motor.v) {
		menu_putstring_footer(WINDOW_FOOTER_ELEMENT_X_DISK,1,"    ",WINDOW_FOOTER_INK,WINDOW_FOOTER_PAPER);
		pd765_motor.v=0;
	}
}

int temp_operacion_pendiente=0;

void pd765_write_command(z80_byte value)
{
	if (pd765_index_write_command==0) printf ("------------------------\nSending PD765 command: 0x%02X PC=0x%04X\n------------------------\n",value,reg_pc);
	else printf ("Sending PD765 command data 0x%02X index write: %d for previous command (0x%02X) PC=0x%04X\n",value,pd765_index_write_command-1,pd765_last_command_write,reg_pc);


	//Envio comando inicial
	if (pd765_index_write_command==0) {
		if (value==0x03) {
			printf ("PD765 command: specify\n");
			pd765_last_command_write=3;
			pd765_index_write_command=1;
			pd765_status_register= (pd765_status_register & 0xF) | kRM ; 
		}

		else if (value==0x04) {
                        printf ("PD765 command: sense drive status\n");
			pd765_last_command_write=4;
                        pd765_index_write_command=1;
			pd765_status_register= (pd765_status_register & 0xF) | kRM ; 
			//sleep(1);
                }

		else if (value==7) {
			printf ("PD765 command: recalibrate\n");
			pd765_last_command_write=7;
                        pd765_index_write_command=1;
			pd765_status_register= (pd765_status_register & 0xF) | kRM ; 
			temp_operacion_pendiente=1;
                }

		else if (value==8) {
                        printf ("PD765 command: sense interrupt status\n");
                        pd765_last_command_write=8;
                        pd765_index_write_command=0; //no necesita parametros
			pd765_index_read_command=1;

			pd765_st0=(pd765_hd&1)<<2 | (pd765_us1&1) << 1 | (pd765_us0&1) | 32;
			printf ("us0: %d us1: %d\n",pd765_us0,pd765_us1);
			//sleep(3);

			pd765_status_register= (pd765_status_register & 0xF) | kRM | kIO; 

			if (temp_operacion_pendiente==0) {
				//Decir comando incorrecto, ya que no habia ninguna operacion pendiente
				pd765_st0 |=128;
				printf ("No habia operacion pendiente. Indicar comando incorrecto en st0\n");
				//sleep(3);
			}

			if (temp_operacion_pendiente==1) {
				temp_operacion_pendiente=0;
			}



			                  pdc_buffer_retorno_len=2;
                                        pdc_buffer_retorno_index=0;
                                        pdc_buffer_retorno[0]=pd765_st0;
                                        pdc_buffer_retorno[1]=pd765_pcn;
                                        pd765_status_register=(pd765_status_register & 0xf) |  kIO  | kRM; //???
                                        drstate=kRvmUPD765Result;
                                        dwstate=kRvmUPD765StateIdle;


                }

		else if ((value&15)==10) {
			printf ("PD765 command: read id\n");
			//sleep(1);
			if (value&64) printf ("TODO multitrack\n");

			pd765_st0=(pd765_hd&1)<<2 | (pd765_us1&1) << 1 | (pd765_us0&1) | 32    ; //Indicar 32 de seek end
			//Cuando finaliza read id, bits de st0: 7 a 0, 6 a 1
			pd765_st0 |=64;

			pd765_st1=0;
			pd765_st2=0;
			pd765_st3=32; //de momento solo ready

			pd765_last_command_write=10;
			pd765_index_write_command=1;
			pd765_status_register=(pd765_status_register & 0xf) | kRM;
			temp_operacion_pendiente=1;
                }

		else if ((value&15)==6) {
			printf ("PD765 command: read data\n");
			//sleep(1);
			if (value&128) printf ("TODO MT\n");
			if (value&64) printf ("TODO MF\n");
			if (value&32) printf ("TODO SK\n");

			pd765_last_command_write=6;
			pd765_index_write_command=1;
			pd765_status_register=(pd765_status_register & 0xf) | kRM;
			temp_operacion_pendiente=1;
		}

		else if (value==15) {
			printf ("PD765 command: seek\n");
                        pd765_last_command_write=15;
			pd765_index_write_command=1;
			temp_operacion_pendiente=1;
                }



		else {
			printf ("\n\nUnknown command\n");
			pd765_index_write_command=0; //Reseteamos a comando inicial
			sleep(5);
		}
	}

	//Envio datos asociados a comando
	else {
		switch (pd765_last_command_write) {
			case 3:
				//Specify
				if (pd765_index_write_command==1) {
					//SRT/HUD
					pd765_srt=(value>>4)&15;
					pd765_hut=value&15;
					printf ("Setting SRT: %d HUT: %d\n",pd765_srt,pd765_hut);
				}

				if (pd765_index_write_command==2) {
					//HLT/ND
					pd765_hlt=(value>>4)&15;
                                        pd765_nd=value&15;
					printf ("Setting HLT: %d ND: %d\n",pd765_hlt,pd765_nd);
				}
				
				pd765_index_write_command++;
				if (pd765_index_write_command==3) {
					pd765_index_write_command=0;
			                pd765_status_register=(pd765_status_register & 0xf) | kRM;
			                dwstate=kRvmUPD765StateIdle;
				}
			break;


			case 4:
				//Sense drive status
				if (pd765_index_write_command==1) {
					//HD US1 US0
					pd765_hd=(value>>2)&1;
					pd765_us1=(value>>1)&1;
					pd765_us0=value&1;
					printf ("Setting HD: %d US1: %d US0: %d\n",pd765_hd,pd765_us1,pd765_us0);
				}

				pd765_index_write_command++;

                                if (pd765_index_write_command==2) {
					pd765_index_write_command=0;
					//Y meter valor de retorno en lectura
					pd765_st3=32; //de momento solo ready
					if (pd765_us0 || pd765_us1) {
						//Decir no ready b:
						pd765_st3 &=(255-32);
					}
					pdc_buffer_retorno_len=1;
					pdc_buffer_retorno_index=0;
					pdc_buffer_retorno[0]=pd765_st3;
					pd765_status_register=(pd765_status_register & 0xf) | kRM | kIO | kBusy;
			                drstate=kRvmUPD765Result;
				        dwstate=kRvmUPD765StateIdle;
				}
					
			break;

			case 6:
				//Read data
				if (pd765_index_write_command==1) {
                                        //HD US1 US0
                                        pd765_hd=(value>>2)&1;
                                        pd765_us1=(value>>1)&1;
                                        pd765_us0=value&1;
                                        printf ("Setting HD: %d US1: %d US0: %d\n",pd765_hd,pd765_us1,pd765_us0);
                                }

				if (pd765_index_write_command==2) {
					pd765_ncn=value;

				}
				
				if (pd765_index_write_command==3) {
					pd765_hd=value;
				}
				
				if (pd765_index_write_command==4) {
					pd765_sector=value;
					//Parece que cuando dice "sector 1" quiere decir el 0
					//pd765_sector--;
				}
				
				if (pd765_index_write_command==5) {
					pd765_bytes_sector=value;
				}
				

				if (pd765_index_write_command==6) {
                                        pd765_eot=value;
				}

				if (pd765_index_write_command==7) {
                                        pd765_gpl=value;
				}

				if (pd765_index_write_command==8) {
                                        pd765_dtl=value;
				}


	
                                pd765_index_write_command++;
				if (pd765_index_write_command==9) {
					pd765_index_read_command=1;
					pd765_index_write_command=0;

		                        pd765_st0=(pd765_hd&1)<<2 | (pd765_us1&1) << 1 | (pd765_us0&1) | 32    ; //Indicar 32 de seek end
                		        pd765_st1=0;
		                        pd765_st2=0;

	                                pdc_buffer_retorno_len=7;

					//Longitud de los datos de retorno es la cabecera (7) + la longitud del sector,
					//de momento forzado a 512. TODO
					pdc_buffer_retorno_len +=512;

					pdc_buffer_retorno_index=0;
                                        pdc_buffer_retorno[0]=pd765_st0;
                                        pdc_buffer_retorno[1]=pd765_st1;
                                        pdc_buffer_retorno[2]=pd765_st2;
                                        pdc_buffer_retorno[3]=pd765_ncn;
                                        pdc_buffer_retorno[4]=pd765_hd;
                                        pdc_buffer_retorno[5]=pd765_sector;
                                        pdc_buffer_retorno[6]=pd765_bytes_sector;

					pd765_read_sector(7);

                                        pd765_status_register=(pd765_status_register & 0xf) | kRM | kIO | kBusy;
                                        drstate=kRvmUPD765Result;
                                        dwstate=kRvmUPD765StateIdle;

					//prueba
					//pd765_disk_busy=TEMP_VALUE_BUSY;

                                }

			break;

			case 7:
				//Recalibrate
				if (pd765_index_write_command==1) {
					//US1 US0
					//printf ("Temporal no cambiamos us0 o us1\n");
					pd765_us1=(value>>1)&1;
                                        pd765_us0=value&1;
                                        printf ("Setting US1: %d US0: %d\n",pd765_us1,pd765_us0);
					pd765_ncn=0;
					pd765_sector=0;
					//sleep(2);
                                }

				pd765_index_write_command++;
                                if (pd765_index_write_command==2) {
                                        pd765_index_write_command=0;
					//E irse al track 0
					pd765_pcn=0;
					pd765_st0=(pd765_hd&1)<<2 | (pd765_us1&1) << 1 | (pd765_us0&1) | 32    ; //Indicar 32 de seek end   
					contador_recallibrate_temp=0;
				}

			break;


			case 8:
				//No necesita parametros
				/*
                                      pdc_buffer_retorno_len=2;
					pdc_buffer_retorno_index=0;
                                        pdc_buffer_retorno[0]=pd765_st0;
                                        pdc_buffer_retorno[1]=pd765_pcn;
                                        pd765_status_register=(pd765_status_register & 0xf) |  kIO | kBusy; //???
                                        drstate=kRvmUPD765Result;
                                        dwstate=kRvmUPD765StateIdle;
				*/

			break;

			case 10:

				 //Read id
				if (pd765_index_write_command==1) {
                                        //HD US1 US0
                                        pd765_hd=(value>>2)&1;
                                        pd765_us1=(value>>1)&1;
                                        pd765_us0=value&1;
                                        printf ("Setting HD: %d US1: %d US0: %d\n",pd765_hd,pd765_us1,pd765_us0);
                                }

                                pd765_index_write_command++;
                                if (pd765_index_write_command==2) {
                                        pd765_index_write_command=0;
                                        pd765_st0=(pd765_hd&1)<<2 | (pd765_us1&1) << 1 | (pd765_us0&1)    ; //Indicar 32 de seek end
                                        pd765_st1=0;
                                        pd765_st2=0;

                                        pdc_buffer_retorno_len=7;
					pdc_buffer_retorno_index=0;
                                        pdc_buffer_retorno[0]=pd765_st0;
                                        pdc_buffer_retorno[1]=pd765_st1;
                                        pdc_buffer_retorno[2]=pd765_st2;
                                        pdc_buffer_retorno[3]=pd765_ncn;
                                        pdc_buffer_retorno[4]=pd765_hd;
                                        pdc_buffer_retorno[5]=pd765_sector;
                                        pdc_buffer_retorno[6]=pd765_bytes_sector;

                                        pd765_status_register=(pd765_status_register & 0xf) | kRM | kIO | kBusy;
                                        drstate=kRvmUPD765Result;
                                        dwstate=kRvmUPD765StateIdle;
				}

			break;

			case 15:
				//Seek
				if (pd765_index_write_command==1) {
                                        //HD US1 US0
                                        pd765_hd=(value>>2)&1;
                                        pd765_us1=(value>>1)&1;
                                        pd765_us0=value&1;
                                        printf ("Setting HD: %d US1: %d US0: %d\n",pd765_hd,pd765_us1,pd765_us0);
                                }

                                if (pd765_index_write_command==2) {
                                        //NCN
                                        pd765_ncn=value;
					pd765_sector=0;
                                        printf ("Setting NCN: %d\n",pd765_ncn);

					//Indicar que el disco esta en seek mode
					//temp pd765_status_register=1;

					//Y luego indicar que el present cylinder es ese mismo (nos posicionamos "instantaneamente");
					pd765_pcn=pd765_ncn;
                                }

                                pd765_index_write_command++;
                                if (pd765_index_write_command==3) {
					pd765_index_write_command=0;
					contador_recallibrate_temp=0;
					pd765_st0=(pd765_hd&1)<<2 | (pd765_us1&1) << 1 | (pd765_us0&1) | 32    ; //Indicar 32 de seek end

                                        //prueba
                                        //pd765_disk_busy=TEMP_VALUE_BUSY;


				}
			break;


			default:
				printf ("\n\nSending data from unknown command: 0x%02X\n",pd765_last_command_write);
				sleep(5);
			break;
		}
	}

}

z80_byte temp_random_pd;

z80_byte temp_leer_dato;

z80_byte pd765_read_command(void)
{
	printf ("\nRead data after command index:  %d PC=0x%04X\n",pdc_buffer_retorno_index,reg_pc);
        z80_byte value;

			/*
                        if (pd765_index_read_command>=8) {
					//Retornar datos hasta que haya final. 512 bytes (sector size=2)
					int final_datos=512;
					if (pd765_read_byte_index>=final_datos) {
						pd765_status_register &=(255-64); //Final
						printf ("Fin leer datos\n");
						//Avanzamos numero de sector + cilindro
						pd765_next_sector();
					}
					else {
						printf ("Devolviendo dato indice: %d\n",pd765_read_byte_index);
						//Devolver datos de disco
						//value=temp_leer_dato++;
						value=pd765_get_disk_value(pd765_read_byte_index);
						//return pd765_buffer_disco[pd765_read_byte_index];
						pd765_read_byte_index++;
						
					}
			}

			if (pd765_index_read_command<=7) pd765_index_read_command++;

			//Ya no hay mas valores de retorno
			if (pd765_index_read_command==8 && pd765_last_command_write!=6) {
				//pd765_status_register &=(255-64); 
				//Quitar busy
				//pd765_status_register &=(255-16);
			}
			*/


    switch(drstate)
    {
      case kRvmUPD765Result:
	pdc_buffer_retorno_len--;
	if (!pdc_buffer_retorno_len) {
          pd765_status_register=(pd765_status_register & 0xf) | kRM;
          drstate=kRvmUPD765StateIdle;
	  printf ("fin datos retorno\n");
        }
        value=pdc_buffer_retorno[pdc_buffer_retorno_index++];
	break;
  
      case kReadingDataEnd:
      case kReadingData:
      case kReadingTrackData:
	pd765_status_register &=0x7f;
	//Retornando datos
	value=0xee; //TODO
	break;
        
      default:
        value=0xff;
      break;
    }

        printf ("Reading PD765 command result: return value: 0x%02X char: %c\n",value,
		(value>=32 && value<=127 ? value : '.') );

	return value;
}


z80_byte pd765_read_status_register(void)
{
	z80_byte value;

	value=pd765_status_register;

	//Si esta ocupado haciendo lecturas, devolver busy
	//if (pd765_disk_busy) {	
	//	value=31;
	//	pd765_disk_busy--;
	//	printf ("\nReading PD765 status register disk busy\n");
	//}


	printf ("\nReading PD765 status register: return value 0x%02X PC=0x%04X\n",value,reg_pc);
	//sleep(1);


	return value;
}

/*

Sentencia que ha hecho saltar el bucle y llegar a un comando 4A (read id)
-----------------
PD765 command: sense interrupt status

Reading PD765 status register: return value 0xC0 PC=0x209E

Read data after command PC=0x20A8
Read command after Sense interrupt status
Returning ST0. Reading PD765 command: 0xBF

Reading PD765 status register: return value 0xC0 PC=0x209E

Read data after command PC=0x20A8
Read command after Sense interrupt status
Returning PCN. Reading PD765 command: 0x00

Reading PD765 status register: return value 0x80 PC=0x209E

Reading PD765 status register: return value 0x80 PC=0x211C
------------------------
Sending PD765 command: 0x4A PC=0x2126
------------------------


Unknown command

Reading PD765 status register: return value 0x80 PC=0x211C


Descomentar linea
value=temp_random_pd--;
Para poder llegar aqui

Que sentido tiene que ST0 valga BF???
O es por culpa del PCN que vale 0???
*/

void traps_plus3dos_return(void)
{
	reg_pc=pop_valor();
}

void traps_plus3dos_return_ok(void)
{

	Z80_FLAGS |=FLAG_C;
	traps_plus3dos_return();
}

void traps_plus3dos_return_error(void)
{

	Z80_FLAGS=(Z80_FLAGS & (255-FLAG_C));
	traps_plus3dos_return();
}

/*
archivo de pruebas en cinta 
pruebaplustres.tap

0   (de program)
 1d 00 00 80 1d 00  (longitud, par1, par2)



datos:
00 01 0e 00 ea 68 6f 6c 
61 20 71 75 65 20 74 61  6c 0d 00 02 07 00 ea 61  
 64 69 6f 73 0d

*/

z80_byte p3dos_prueba_header[]={0,0x1d, 0x00, 00, 0x80, 0x1d ,00,0};
z80_byte p3dos_prueba_datos[]={00 ,0x01 ,0x0e ,0x00 ,0xea ,0x68 ,0x6f ,0x6c 
,0x61 ,0x20 ,0x71 ,0x75 ,0x65 ,0x20 ,0x74 ,0x61  ,0x6c ,0x0d ,0x00 ,0x02 ,0x07 ,0x00 ,0xea ,0x61  
 ,0x64 ,0x69 ,0x6f ,0x73 ,0x0d};


void traps_plus3dos_handle_ref_head(void)
{
	reg_ix=49152;
	int i;

			z80_byte *p;
		p=ram_mem_table[7];

	for (i=0;i<8;i++) {

		p[i]=p3dos_prueba_header[i];
	}
}

void traps_plus3dos_handle_dos_read(void)
{

	/*
Read bytes from a file into memory.

Advance the file pointer.

The destination buffer is in the following memory configuration:

	C000h...FFFFh (49152...65535)	- Page specified in C
	8000h...BFFFh (32768...49151)	- Page 2
	4000h...7FFFh (16384...32767)	- Page 5
	0000h...3FFFh (0...16383)	- DOS ROM

The routine does not consider soft-EOF.

Reading EOF will produce an error.

ENTRY CONDITIONS
	B = File number
	C = Page for C000h (49152)...FFFFh (65535)
	DE = Number of bytes to read (0 means 64K)
	HL = Address for bytes to be read

EXIT CONDITIONS
	If OK:
		Carry true
		A DE corrupt
	Otherwise:
		Carry false
		A = Error code
		DE = Number of bytes remaining unread
	Always:
		BC HL IX corrupt
		All other registers preserved
	*/


	reg_ix=49152;
	int i;

			z80_byte *p;
		p=ram_mem_table[reg_c];

		//temp
		p=ram_mem_table[5];

	for (i=0;i<reg_de;i++) {
		//poke_byte_no_time(reg_hl+i,p3dos_prueba_datos[i]);
		p[(reg_hl&16383)+i]=p3dos_prueba_datos[i];
	}
}

                        
void traps_plus3dos_dd_l_dpb(void)
{
/*
DD L DPB                Initialise a DPB from a disk specification
DD L DPB
018Ah (394)

Initialise a DPB for a given format.

This routine does not affect or consider the freeze flag.

ENTRY CONDITIONS
        IX = Address of destination DPB
        HL = Address of source disk specification

EXIT CONDITIONS
        If OK:
                Carry true
                A = Disk type recorded on disk
                DE = Size of allocation vector
                HL = Size of hash table
        If bad format:
                Carry false
                A = Error code
                DE HL corrupt
        Always:
                BC IX corrupt
                All other registers preserved
*/
		//De momento no tengo claro que retornar en DE, HL
		DE=16;
		HL=16;

		traps_plus3dos_return_ok();

}

void traps_plus3dos_dd_l_seek(void)
{
/*
DD L SEEK
018Dh (397)

Seek to required track.

Retry if fails.

ENTRY CONDITIONS
        C = Unit/head
                bits 0...1 = unit
                bit 2 = head
                bits 3...7 = 0
        D = Track
        IX = Address of XDPB

EXIT CONDITIONS
        If OK:
                Carry true
                A corrupt
        Otherwise:
                Carry false
                A = Error report
        Always:
                BC DE HL IX corrupt
                All other registers preserved
*/


	traps_plus3dos_return_ok();
}

//ROM 2 4.0 direct entry points
int traps_plus3dos_directentry(void)
{
	switch(reg_pc) {
                 case 0x019f:
			printf ("DOS_INITIALISE\n");
		break;
	
                 case 0x01cd:
                 case 0x062d:
                 case 0x0740:
                 case 0x0761:
                 case 0x08b1:
                 case 0x10ea:
                 case 0x11fe:
                 case 0x11a8:
                 case 0x1298:
                 case 0x0a19:
                 case 0x08f2:
                 case 0x0924:
                 case 0x096f:
                 case 0x1ace:
                 case 0x090f:
                 case 0x08fc:
                 case 0x1070:
                 case 0x108c:
                 case 0x1079:
                 case 0x01d8:
                 case 0x01de:
                 case 0x05c2:
                 case 0x08c3:
                 case 0x0959:
                 case 0x0706:
                 case 0x02e8:
			printf ("DOS_SET_MESSAGE\n");
		break;
                 case 0x1847:
                 case 0x1943:
                 case 0x1f27:
			printf ("DD_INTERFACE\n");
		break;
                 case 0x1f32:
                 case 0x1f47:
                 case 0x1e7c:
                 case 0x1bff:
                 case 0x1c0d:
                 case 0x1c16:
                 case 0x1c24:
                 case 0x1c36:
                 case 0x1e65:
                 case 0x1c80:
                 case 0x1cdb:
                 case 0x1edd:
                 case 0x1ee9:
                 case 0x1e75:
                 case 0x1bda:
                 case 0x1cee:
                 case 0x1d30:
			printf ("DD_L_DPB\n");
		break;

                 case 0x1f76:
			printf ("DD_L_SEEK\n");
		break;

                 case 0x20c3:
                 case 0x20cc:
                 case 0x212b:
			printf ("DD_L_ON_MOTOR\n");
		break;

                 case 0x2150:
                 case 0x2164:
		 break;

		default:
			return 0;
		break;
	}

	return 1;
}

void traps_plus3dos(void)
{
	if (MACHINE_IS_SPECTRUM_P2A) {
		z80_byte rom_entra=((puerto_32765>>4)&1) + ((puerto_8189>>1)&2);

		int direct_entry=0;

		if (rom_entra==2 && traps_plus3dos_directentry() ) {
			printf ("Direct entry point. reg_pc=%d %04xH\n\n",reg_pc,reg_pc);	
			direct_entry=1;
			sleep(1);
		}

		if (rom_entra==2 && 
			( (reg_pc>=256 && reg_pc<=412) || direct_entry) 


			) {
			//Mostrar llamadas a PLUS3DOS


			switch (reg_pc) {

				case 256:
					printf ("-----DOS INITIALISE\n\n");
					//traps_plus3dos_return_ok();
				break;

				case 262:
					printf ("-----DOS OPEN\n\n");
					/*
						If file newly created:
		Carry true
		Zero true
		A corrupt
	If existing file opened:
		Carry true
		Zero false
		A corrupt
					*/

					//Z80_FLAGS=(Z80_FLAGS & (255-FLAG_Z));
					//traps_plus3dos_return_ok();

				break;

				case 265:
					printf ("-----DOS CLOSE\n\n");
					//traps_plus3dos_return_ok();
				break;

				case 268:
					printf ("-----DOS ABANDON\n\n");
					//traps_plus3dos_return_ok();
				break;

				case 271:
					printf ("-----DOS REF HEAD\n\n");
/*
EXIT CONDITIONS
	If OK, but file doesn't have a header:
		Carry true
		Zero true
		A corrupt
		IX = Address of header data in page 7
	If OK, file has a header:
		Carry true
		Zero false
		A corrupt
		IX = Address of header data in page 7
		*/

					//Problema: Como asigno IX dentro de pagina 7? A saber
					//traps_plus3dos_handle_ref_head();
					


					//Z80_FLAGS=(Z80_FLAGS & (255-FLAG_Z));
					//traps_plus3dos_return_ok();
					
				break;

				case 274:
					printf ("-----DOS READ. Address: %d Lenght: %d\n\n",reg_hl,reg_de);
					
					//traps_plus3dos_handle_dos_read();
					//traps_plus3dos_return_ok();
				break;

				case 286:
					printf ("-----DOS CATALOG\n\n");
				break;

				case 334:
					printf ("-----DOS SET MESSAGE\n\n");
				break;

				case 340:
					printf ("-----DOS MAP B\n\n");
				break;

				case 355:
				case 0x1bff:
					printf ("-----DD READ SECTOR track %d sector %d buffer %d xdpb: %d\n\n",
					reg_d,reg_e,reg_hl,reg_ix);	
/*
ENTRY CONDITIONS
	B = Page for C000h (49152)...FFFFh (65535)
	C = Unit (0/1)
	D = Logical track, 0 base
	E = Logical sector, 0 base
	HL = Address of buffer
	IX = Address of XDPB
	*/		
					printf ("PLUS3DOS routine reg_pc=%d\n",reg_pc);
					sleep(1);			
				break;
			

				case 349:
					printf ("-----DD SETUP\n\n");
				break;

				case 346:
					printf ("-----DD INIT\n\n");
					//traps_plus3dos_return_ok();
				break;
			

				case 343:
					printf ("-----DD INTERFACE\n\n");
					//traps_plus3dos_return_ok();
				break;
			
				case 367:
					printf ("-----DD READ ID\n\n");
				break;

				case 379:
					printf ("-----DD ASK 1\n\n");
					//traps_plus3dos_return_error();
				break;

				case 394:
				case 0x1d30:
					printf ("-----DD_L_DPB\n\n");
					traps_plus3dos_dd_l_dpb();
				break;


				case 397:
				case 0x1f76:
					printf ("-----DD_L_SEEK\n\n");
					traps_plus3dos_dd_l_seek();
				break;
			}

			printf ("PLUS3DOS routine reg_pc=%d\n\n",reg_pc);
			sleep(1);
		}
	}
}
