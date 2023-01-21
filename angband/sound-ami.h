/*
 * sound-ami.h
 *
 *  Created on: Jul 29, 2015
 *      Author: alex
 */

#ifndef SOUND_AMI_H_
#define SOUND_AMI_H_


/* EasySound.h        */
/*                    */
/* V2.00 1990-0-23    */
/*                    */
/* AMIGA C CLUB (ACC) */
/* Anders Bjerin      */
/* Tulevagen 22       */
/* 181 41  LIDINGO    */
/* SWEDEN             */

#include <exec/types.h>

/* Sound channels: */
#define LEFT0         0
#define RIGHT0        1
#define RIGHT1        2
#define LEFT1         3

#define NONSTOP       0
#define ONCE          1
#define MAXVOLUME    64
#define MINVOLUME     0
#define NORMALRATE    0
struct SoundInfo
{
    BYTE *SoundBuffer;   /* WaveForm Buffers */
    UWORD RecordRate;    /* Record Rate */
    ULONG FileLength;    /* WaveForm Lengths */
};

extern CPTR PrepareSound(char *file );
extern BOOL PlaySound( struct SoundInfo *info, UWORD volume, UBYTE channel, WORD delta_rate, UWORD repeat );
extern void StopSound( UBYTE channel );
extern void RemoveSound( struct SoundInfo *info );


#endif /* SOUND_AMI_H_ */
