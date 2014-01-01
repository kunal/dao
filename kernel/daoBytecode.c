/*
// Dao Virtual Machine
// http://www.daovm.net
//
// Copyright (c) 2006-2013, Limin Fu
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED  BY THE COPYRIGHT HOLDERS AND  CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED  WARRANTIES,  INCLUDING,  BUT NOT LIMITED TO,  THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL  THE COPYRIGHT HOLDER OR CONTRIBUTORS  BE LIABLE FOR ANY DIRECT,
// INDIRECT,  INCIDENTAL, SPECIAL,  EXEMPLARY,  OR CONSEQUENTIAL  DAMAGES (INCLUDING,
// BUT NOT LIMITED TO,  PROCUREMENT OF  SUBSTITUTE  GOODS OR  SERVICES;  LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION)  HOWEVER CAUSED  AND ON ANY THEORY OF
// LIABILITY,  WHETHER IN CONTRACT,  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <math.h>
#include <string.h>

#include "daoBytecode.h"
#include "daoOptimizer.h"
#include "daoNamespace.h"
#include "daoVmspace.h"
#include "daoValue.h"
#include "daoGC.h"


static const char* const dao_asm_names[] =
{
	"ASM_NONE"      ,
	"ASM_TYPEOF"    ,
	"ASM_TYPEDEF"   ,
	"ASM_ROUTINE"   ,
	"ASM_CLASS"     ,
	"ASM_INTERFACE" ,
	"ASM_ENUM"      ,
	"ASM_TYPE"      ,
	"ASM_VALUE"     ,
	"ASM_EVAL"      ,
	"ASM_CONSTS"    ,
	"ASM_TYPES"     ,
	"ASM_CODE"      ,
	"ASM_END"       ,
	"ASM_LOAD"      ,
	"ASM_USE"       ,
	"ASM_BASES"     ,
	"ASM_CONST"     ,
	"ASM_STATIC"    ,
	"ASM_GLOBAL"    ,
	"ASM_VAR"       ,
	"ASM_DECOPAT"   ,
	"ASM_DATA"      ,
	"ASM_DATA2"     ,
	"ASM_SEEK"
};


DaoByteBlock* DaoByteBlock_New( DaoByteCoder *coder )
{
	DaoByteBlock *self = (DaoByteBlock*) dao_calloc(1,sizeof(DaoByteBlock));
	self->coder = coder;
	return self;
}
void DaoByteBlock_Delete( DaoByteBlock *self )
{
	if( self->wordToBlocks ) DMap_Delete( self->wordToBlocks );
	if( self->valueToBlocks ) DMap_Delete( self->valueToBlocks );
	dao_free( self );
}


DaoByteCoder* DaoByteCoder_New( DaoVmSpace *vms )
{
	DaoByteCoder *self = (DaoByteCoder*) dao_calloc(1,sizeof(DaoByteCoder));
	self->valueToBlocks = DHash_New(D_VALUE,0);
	self->caches = DArray_New(0);
	self->stack = DArray_New(0);
	self->lines = DArray_New(0);
	self->ivalues = DArray_New(0);
	self->iblocks = DArray_New(0);
	self->indices = DArray_New(0);
	self->path = DString_New(1);
	self->intSize = sizeof(daoint);
	self->vmspace = vms;
	return self;
}
void DaoByteCoder_Remove( DaoByteCoder *self, DaoByteBlock *block, DaoByteBlock *parent )
{
	DaoByteBlock *it = block->first;
	while( it ){
		DaoByteBlock *b = it;
		it = it->next;
		DaoByteCoder_Remove( self, b, block );
	}
	if( block->prev ) block->prev->next = block->next;
	if( block->next ) block->next->prev = block->prev;
	if( parent && parent->first == block ) parent->first = block->next;
	if( parent && parent->last == block ) parent->last = block->prev;
	block->parent = NULL;
	block->first = block->last = NULL;
	block->prev = block->next = NULL;
	DArray_Append( self->caches, block );
}
void DaoByteCoder_Reset( DaoByteCoder *self )
{
	if( self->top ) DaoByteCoder_Remove( self, self->top, NULL );
	self->top = NULL;
	DArray_Clear( self->stack );
	DMap_Reset( self->valueToBlocks );
}
void DaoByteCoder_Delete( DaoByteCoder *self )
{
	int i, n;
	DaoByteCoder_Reset( self );
	for(i=0,n=self->caches->size; i<n; ++i){
		DaoByteBlock *block = (DaoByteBlock*) self->caches->items.pVoid[i];
		DaoByteBlock_Delete( block );
	}
	DArray_Delete( self->caches );
	DArray_Delete( self->stack );
	DArray_Delete( self->lines );
	DArray_Delete( self->ivalues );
	DArray_Delete( self->iblocks );
	DArray_Delete( self->indices );
	DString_Delete( self->path );
	DMap_Delete( self->valueToBlocks );
	dao_free( self );
}

DaoByteBlock* DaoByteCoder_Init( DaoByteCoder *self )
{
	DaoByteCoder_Reset( self );
	self->top = DaoByteCoder_NewBlock( self, DAO_ASM_ROUTINE );
	return self->top;
}

DaoByteBlock* DaoByteCoder_NewBlock( DaoByteCoder *self, int type )
{
	DaoByteBlock *block = (DaoByteBlock*) DArray_PopBack( self->caches );
	if( block == NULL ) block = DaoByteBlock_New( self );
	if( block->wordToBlocks ) DMap_Reset( block->wordToBlocks );
	block->type = type;
	memset( block->begin, 0, 8 );
	memset( block->end, 0, 8 );
	return block;
}

DaoByteBlock* DaoByteBlock_NewBlock( DaoByteBlock *self, int type )
{
	DaoByteBlock *block = DaoByteCoder_NewBlock( self->coder, type );
	block->parent = self;
	if( self->last == NULL ){
		self->last = self->first = block;
	}else{
		block->prev = self->last;
		self->last->next = block;
		self->last = block;
	}
	return block;
}

DaoByteBlock* DaoByteBlock_FindBlock( DaoByteBlock *self, DaoValue *value )
{
	DNode *it;
	if( value == NULL ) return NULL;
	if( self->valueToBlocks ){
		it = DMap_Find( self->valueToBlocks, value );
		if( it ) return (DaoByteBlock*) it->value.pVoid;
	}
	it = DMap_Find( self->coder->valueToBlocks, value );
	if( it ) return (DaoByteBlock*) it->value.pVoid;
	return NULL;
}
DaoByteBlock* DaoByteBlock_AddBlock( DaoByteBlock *self, DaoValue *value, int type )
{
	DaoByteBlock *block = DaoByteBlock_NewBlock( self, type );
	if( self->valueToBlocks == NULL ) self->valueToBlocks = DHash_New(D_VALUE,0);
	DaoValue_Copy( value, & block->value );
	DMap_Insert( self->coder->valueToBlocks, value, block );
	DMap_Insert( self->valueToBlocks, value, block );
	return block;
}



void DaoByteCoder_EncodeUInt16( uchar_t *data, uint_t value )
{
	data[0] = (value >> 8) & 0xFF;
	data[1] = value & 0xFF;
}
void DaoByteCoder_EncodeUInt32( uchar_t *data, uint_t value )
{
	data[0] = (value >> 24) & 0xFF;
	data[1] = (value >> 16) & 0xFF;
	data[2] = (value >>  8) & 0xFF;
	data[3] = value & 0xFF;
}
void DaoByteCoder_EncodeDaoInt( uchar_t *data, daoint value )
{
	uchar_t i, m = sizeof(daoint);
	for(i=0; i<m; ++i) data[i] = (value >> 8*(m-1-i)) & 0xFF;
}
/*
// IEEE 754 double-precision binary floating-point format:
//   sign(1)--exponent(11)------------fraction(52)---------------------
//   S EEEEEEEEEEE FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
//   63         52                                                    0
//
//   value = (-1)^S  *  ( 1 + \sigma_0^51 (b_i * 2^{-(52-i)}) )  *  2^{E-1023}
//
// Exponents 0x000 is used to represent zero (if F=0) and subnormals (if F!=0);
// Exponents 0x7FF is used to represent inf (if F=0) and NaNs (if F!=0);
// Where F is the fraction mantissa.
*/
static void DaoByteCoder_EncodeNAN( uchar_t *data )
{
	DaoByteCoder_EncodeUInt32( data, 0x7FF << 20 );
	DaoByteCoder_EncodeUInt32( data + 4, 1 );
}
static void DaoByteCoder_EncodeInf( uchar_t *data )
{
	DaoByteCoder_EncodeUInt32( data, 0x7FF << 20 );
	DaoByteCoder_EncodeUInt32( data + 4, 0 );
}
static void DaoByteCoder_EncodeDouble( uchar_t *data, double value )
{
	uint_t i = 20, m1 = 0, m2 = 0;
	int first = 1;
	int neg = value < 0.0;
	int expon = 0;
	double frac;

	if( value == 0.0 ){
		DaoByteCoder_EncodeUInt32( data, 0 );
		DaoByteCoder_EncodeUInt32( data + 4, 0 );
		return;
	}else if( isnan( value ) ){
		DaoByteCoder_EncodeNAN( data );
		return;
	}else if( isinf( value ) ){
		DaoByteCoder_EncodeInf( data );
		return;
	}

	frac = frexp( fabs( value ), & expon );
	frac = 2.0 * frac - 1.0;
	expon -= 1;
	while(1){
		double prod = frac * 2.0;
		uint_t bit = (uint_t) prod;
		frac = prod - bit;
		i -= 1;
		if( first ){
			m1 |= bit << i;
			if( i == 0 ){
				first = 0;
				i = 32;
			}
		}else{
			m2 |= bit << i;
			if( i == 0 ) break;
		}
		if( frac <= 0.0 ) break;
	}
	m1 |= (expon+1023) << 20;
	if( neg ) m1 |= 1 << 31;
	DaoByteCoder_EncodeUInt32( data, m1 );
	DaoByteCoder_EncodeUInt32( data + 4, m2 );
}
uint_t DaoByteCoder_DecodeUInt8( uchar_t *data )
{
	return data[0];
}
uint_t DaoByteCoder_DecodeUInt16( uchar_t *data )
{
	int value = (data[0]<<8) + data[1];
	data += 2;
	return value;
}
uint_t DaoByteCoder_DecodeUInt32( uchar_t *data )
{
	uint_t value = data[0] << 24;
	value += data[1] << 16;
	value += data[2] << 8;
	value += data[3];
	data += 4;
	return value;
}
daoint DaoByteCoder_DecodeDaoInt( DaoByteCoder *self, uchar_t *data )
{
	DaoStream *stream = self->vmspace->errorStream;
	uchar_t i, m = self->intSize;
	daoint value = 0;

	if( self->intSize > sizeof(daoint) ){ /* self->intSize=8, sizeof(daoint)=4 */
		daoint B1 = data[0], B2 = data[1], B3 = data[2], B4 = data[3];
		daoint B5 = data[4], B6 = data[5], B7 = data[6], B8 = data[7];

		if( (B1 == 0x7F || B1 == 0xFF) && B2 == 0xFF && B3 == 0xFF && B4 == 0xFF ){
			if( B5 & 0x80 ) goto TooBigInteger;
			if( B1 == 0xFF ) B5 |= 0x80;
		}else if( B1 || B2 || B3 || B4 ){
			goto TooBigInteger;
		}
		return (B5<<24)|(B6<<16)|(B7<<8)|B8;
	}else if( self->intSize < sizeof(daoint) ){ /* self->intSize=4, sizeof(daoint)=8 */
		daoint B1 = data[0], B2 = data[1], B3 = data[2], B4 = data[3];

		if( B1 & 0x80 ){
			daoint leading = (0xFF<<24)|(0xFF<<16)|(0xFF<<8)|0xFF;
			daoint shift = 32; /* just to avoid a warning on 32 bit systems; */
			return (leading<<shift)|(0xFF<<24)|((B1&0x7F)<<24)|(B2<<16)|(B3<<8)|B4;
		}
		return (B1<<24)|(B2<<16)|(B3<<8)|B4;
	}

	for(i=0; i<m; ++i) value |= ((daoint)data[i]) << 8*(m-1-i);
	return value;
TooBigInteger:
	DaoStream_WriteMBS( stream, "Error: too big integer value for the platform!" );
	self->error = 1;
	return 0;
}
/*
// IEEE 754 double-precision binary floating-point format:
//   sign(1)--exponent(11)------------fraction(52)---------------------
//   S EEEEEEEEEEE FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
//   63         52                                                    0
//
//   value = (-1)^S  *  ( 1 + \sigma_0^51 (b_i * 2^{-(52-i)}) )  *  2^{E-1023}
*/
double DaoByteCoder_DecodeDouble( DaoByteCoder *self, uchar_t *data )
{
	double value = 1.0;
	uint_t first = DaoByteCoder_DecodeUInt32( data );
	uint_t second = DaoByteCoder_DecodeUInt32( data+4 );
	uint_t negative = first & (1<<31);
	int i, expon;

	if( first == 0 && second == 0 ) return 0;
	if( first == (0x7FF<<20) && second == 0 ) return INFINITY;
	if( first == (0x7FF<<20) && second == 1 ) return NAN;

	first = (first<<1)>>1;
	expon = (first>>20) - 1023;
	for(i=0; i<32; ++i){
		if( (second>>i)&0x1 ){
			int e = -(52-i);
			value += e >= 0 ? pow( 2, e ) : 1.0 / pow( 2, -e );
		}
	}
	for(i=0; i<20; ++i){
		if( (first>>i) & 0x1 ){
			int e = -(20-i);
			value += e >= 0 ? pow( 2, e ) : 1.0 / pow( 2, -e );
		}
	}
	if( expon >= 0 ){
		value *= pow( 2, expon );
	}else{
		value /= pow( 2, -expon );
	}
	if( negative ) value = -value;
	return value;
}
void DaoByteCoder_DecodeChunk2222( uchar_t *data, uint_t *A, uint_t *B, uint_t *C, uint_t *D )
{
	*A = DaoByteCoder_DecodeUInt16( data + 0 );
	*B = DaoByteCoder_DecodeUInt16( data + 2 );
	*C = DaoByteCoder_DecodeUInt16( data + 4 );
	*D = DaoByteCoder_DecodeUInt16( data + 6 );
}
void DaoByteCoder_DecodeChunk2114( uchar_t *data, uint_t *A, uint_t *B, uint_t *C, uint_t *D )
{
	*A = DaoByteCoder_DecodeUInt16( data + 0 );
	*B = DaoByteCoder_DecodeUInt8( data + 2 );
	*C = DaoByteCoder_DecodeUInt8( data + 3 );
	*D = DaoByteCoder_DecodeUInt32( data + 4 );
}
void DaoByteCoder_DecodeChunk224( uchar_t *data, uint_t *A, uint_t *B, uint_t *C )
{
	*A = DaoByteCoder_DecodeUInt16( data + 0 );
	*B = DaoByteCoder_DecodeUInt16( data + 2 );
	*C = DaoByteCoder_DecodeUInt32( data + 4 );
}
void DaoByteCoder_DecodeSubChunk222( uchar_t *data, uint_t *A, uint_t *B, uint_t *C )
{
	*A = DaoByteCoder_DecodeUInt16( data + 0 );
	*B = DaoByteCoder_DecodeUInt16( data + 2 );
	*C = DaoByteCoder_DecodeUInt16( data + 4 );
}
void DaoByteCoder_DecodeSubChunk114( uchar_t *data, uint_t *A, uint_t *B, uint_t *C )
{
	*A = DaoByteCoder_DecodeUInt8( data + 0 );
	*B = DaoByteCoder_DecodeUInt8( data + 1 );
	*C = DaoByteCoder_DecodeUInt32( data + 2 );
}
void DaoByteCoder_DecodeSubChunk24( uchar_t *data, uint_t *A, uint_t *B )
{
	*A = DaoByteCoder_DecodeUInt16( data + 0 );
	*B = DaoByteCoder_DecodeUInt32( data + 2 );
}


DaoByteBlock* DaoByteBlock_EncodeInteger( DaoByteBlock *self, daoint val )
{
	DaoByteBlock *block;
	DaoInteger tmp = {DAO_INTEGER,0,0,0,0,0.0};
	DaoValue *value = (DaoValue*) & tmp;

	value->xInteger.value = val;
	block = DaoByteBlock_FindBlock( self, value );
	if( block ) return block;
	block = DaoByteBlock_AddBlock( self, value, DAO_ASM_VALUE );
	DaoByteCoder_EncodeUInt16( block->begin, DAO_INTEGER );
	DaoByteCoder_EncodeDaoInt( block->end, val );
	return block;
}
DaoByteBlock* DaoByteBlock_EncodeFloat( DaoByteBlock *self, float val )
{
	DaoByteBlock *block;
	DaoFloat tmp = {DAO_FLOAT,0,0,0,0,0.0};
	DaoValue *value = (DaoValue*) & tmp;

	value->xFloat.value = val;
	block = DaoByteBlock_FindBlock( self, value );
	if( block ) return block;
	block = DaoByteBlock_AddBlock( self, value, DAO_ASM_VALUE );
	DaoByteCoder_EncodeUInt16( block->begin, DAO_FLOAT );
	DaoByteCoder_EncodeDouble( block->end, val );
	return block;
}
DaoByteBlock* DaoByteBlock_EncodeDouble( DaoByteBlock *self, double val )
{
	DaoByteBlock *block;
	DaoDouble tmp = {DAO_DOUBLE,0,0,0,0,0.0};
	DaoValue *value = (DaoValue*) & tmp;

	value->xDouble.value = val;
	block = DaoByteBlock_FindBlock( self, value );
	if( block ) return block;
	block = DaoByteBlock_AddBlock( self, value, DAO_ASM_VALUE );
	DaoByteCoder_EncodeUInt16( block->begin, DAO_DOUBLE );
	DaoByteCoder_EncodeDouble( block->end, val );
	return block;
}
DaoByteBlock* DaoByteBlock_EncodeComplex( DaoByteBlock *self, DaoComplex *value )
{
	DaoByteBlock *block2, *block = DaoByteBlock_FindBlock( self, (DaoValue*) value );
	if( block ) return block;
	block = DaoByteBlock_AddBlock( self, (DaoValue*) value, DAO_ASM_VALUE );
	block2 = DaoByteBlock_NewBlock( block, DAO_ASM_DATA );
	DaoByteCoder_EncodeUInt16( block->begin, DAO_COMPLEX );
	DaoByteCoder_EncodeDouble( block2->begin, value->value.real );
	DaoByteCoder_EncodeDouble( block->end, value->value.imag );
	return block;
}
DaoByteBlock* DaoByteBlock_EncodeLong( DaoByteBlock *self, DaoLong *value )
{
	int i;
	DaoByteBlock *block = DaoByteBlock_FindBlock( self, (DaoValue*) value );
	if( block ) return block;
	block = DaoByteBlock_AddBlock( self, (DaoValue*) value, DAO_ASM_VALUE );
	block->begin[2] = value->value->base;
	block->begin[3] = value->value->sign;
	DaoByteCoder_EncodeUInt16( block->begin, DAO_LONG );
	DaoByteCoder_EncodeUInt32( block->begin+4, value->value->size );
	for(i=0; (i+8)<value->value->size; i+=8){
		DaoByteBlock *dataBlock = DaoByteBlock_NewBlock( block, DAO_ASM_DATA );
		memcpy( dataBlock->begin, value->value->data+i, 8*sizeof(char) );
	}
	memcpy( block->end, value->value->data+i, (value->value->size-i)*sizeof(char) );
	return block;
}
DaoByteBlock* DaoByteBlock_EncodeString( DaoByteBlock *self, DString *string )
{
	DaoByteBlock *block;
	DaoString daostring = {DAO_STRING,0,0,0,1,NULL};
	DaoValue *value = (DaoValue*) & daostring;
	int i, j, size = string->size;

	daostring.data = string;
	block = DaoByteBlock_FindBlock( self, value );
	if( block ) return block;
	block = DaoByteBlock_AddBlock( self, value, DAO_ASM_VALUE );
	DaoByteCoder_EncodeUInt16( block->begin, DAO_STRING );
	DaoByteCoder_EncodeUInt16( block->begin+2, string->mbs == NULL );
	DaoByteCoder_EncodeUInt32( block->begin+4, string->size );
	if( string->mbs ){
		for(i=0; (i+8)<size; i+=8){
			DaoByteBlock *dataBlock = DaoByteBlock_NewBlock( block, DAO_ASM_DATA );
			memcpy( dataBlock->begin, string->mbs+i, 8*sizeof(char) );
		}
		memcpy( block->end, string->mbs+i, (size-i)*sizeof(char) );
	}else{
		for(i=0; (i+2)<size; i+=2){
			DaoByteBlock *dataBlock = DaoByteBlock_NewBlock( block, DAO_ASM_DATA );
			DaoByteCoder_EncodeUInt32( dataBlock->begin, string->wcs[i] );
			DaoByteCoder_EncodeUInt32( dataBlock->begin+4, string->wcs[i+1] );
		}
		if( i < size ) DaoByteCoder_EncodeUInt32( block->end, string->wcs[i] );
		if( (i+1) < size ) DaoByteCoder_EncodeUInt32( block->end+4, string->wcs[i] );
	}
	return block;
}



static void DaoByteCoder_PrintBlock( DaoByteCoder *self, DaoByteBlock *block, int spaces );

static void DaoByteBlock_CopyToEndFromBegin( DaoByteBlock *self, DaoByteBlock *other )
{
	int i;
	memcpy( self->end, other->begin, 8 );
	if( other->wordToBlocks == NULL ) return;
	if( other->wordToBlocks && self->wordToBlocks == NULL ) self->wordToBlocks = DMap_New(0,0);
	for(i=0; i<8; i+=2){
		DNode *it = DMap_Find( other->wordToBlocks, (other->begin + i) );
		if( it ) DMap_Insert( self->wordToBlocks, (self->end + i), it->value.pVoid );
	}
}
void DaoByteBlock_EncodeValues( DaoByteBlock *self, DaoValue **values, int count )
{
	int i;
	for(i=0; i<count; ++i){
		DaoByteBlock *block = DaoByteBlock_EncodeValue( self, values[i] );
		DArray_Append( self->coder->stack, block );
	}
}
int DaoByteBlock_EncodeValues2( DaoByteBlock *self, DArray *values )
{
	DaoByteBlock_EncodeValues( self, values->items.pValue, values->size );
	return values->size;
}
void DaoByteBlock_InsertBlockIndex( DaoByteBlock *self, uchar_t *code, DaoByteBlock *block )
{
	if( self->wordToBlocks == NULL ) self->wordToBlocks = DMap_New(0,0);
	DMap_Insert( self->wordToBlocks, code, block );
}
void DaoByteBlock_AddBlockIndexData( DaoByteBlock *self, int head, int size )
{
	DaoByteBlock *dataBlock = self;
	uchar_t *data = dataBlock->begin + 2*(4-head);
	int i, j, offset;
	if( size > self->coder->stack->size ) size = self->coder->stack->size;
	offset = self->coder->stack->size - size;
	for(i=0; i<size; ++i, data+=2){
		DaoByteBlock *block = (DaoByteBlock*) self->coder->stack->items.pVoid[i+offset];
		if( data >= dataBlock->begin + 8 ){
			dataBlock = DaoByteBlock_NewBlock( self, DAO_ASM_DATA );
			data = dataBlock->begin;
		}
		DaoByteBlock_InsertBlockIndex( dataBlock, data, block );
	}
	if( dataBlock != self ){
		DaoByteBlock_CopyToEndFromBegin( self, dataBlock );
		DaoByteCoder_Remove( self->coder, dataBlock, self );
	}
	DArray_Erase( self->coder->stack, offset, size );
}



DaoByteBlock* DaoByteBlock_EncodeEnum( DaoByteBlock *self, DaoEnum *value )
{
	DaoByteBlock *typeBlock;
	DaoByteBlock *newBlock = DaoByteBlock_FindBlock( self, (DaoValue*) value );
	
	if( newBlock ) return newBlock;

	typeBlock = DaoByteBlock_EncodeType( self, value->etype );
	newBlock = DaoByteBlock_AddBlock( self, (DaoValue*) value, DAO_ASM_VALUE );

	DaoByteCoder_EncodeUInt16( newBlock->begin, DAO_ENUM );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+2, typeBlock );
	DaoByteCoder_EncodeUInt32( newBlock->end, value->value );
	return newBlock;
}
DaoByteBlock* DaoByteBlock_EncodeArray( DaoByteBlock *self, DaoArray *value )
{
	int i;
	DaoByteBlock *databk;
	DaoByteBlock *block = DaoByteBlock_FindBlock( self, (DaoValue*) value );
	if( block ) return block;
	block = DaoByteBlock_AddBlock( self, (DaoValue*) value, DAO_ASM_VALUE );
	DaoByteCoder_EncodeUInt16( block->begin, DAO_ARRAY );
	block->begin[2] = value->etype;
	block->begin[3] = value->ndim;
	DaoByteCoder_EncodeUInt32( block->begin+4, value->size );
	for(i=0; (i+2)<value->ndim; i+=2){
		databk = DaoByteBlock_NewBlock( block, DAO_ASM_DATA );
		DaoByteCoder_EncodeUInt32( databk->begin, value->dims[i] );
		DaoByteCoder_EncodeUInt32( databk->begin+4, value->dims[i+1] );
	}
	databk = DaoByteBlock_NewBlock( block, DAO_ASM_DATA );
	DaoByteCoder_EncodeUInt32( databk->begin, value->dims[i] );
	if( (i+1)<value->ndim ) DaoByteCoder_EncodeUInt32( databk->begin+4, value->dims[i+1] );
	if( value->etype == DAO_INTEGER && sizeof(daoint) == 8 ){
		for(i=0; (i+1)<value->size; i+=1){
			databk = DaoByteBlock_NewBlock( block, DAO_ASM_DATA );
			DaoByteCoder_EncodeDaoInt( databk->begin, value->data.i[i] );
		}
		DaoByteCoder_EncodeDaoInt( databk->end, value->data.i[i] );
	}else if( value->etype == DAO_INTEGER ){
		for(i=0; (i+2)<value->size; i+=2){
			databk = DaoByteBlock_NewBlock( block, DAO_ASM_DATA );
			DaoByteCoder_EncodeDaoInt( databk->begin, value->data.i[i] );
			DaoByteCoder_EncodeDaoInt( databk->begin+4, value->data.i[i+1] );
		}
		DaoByteCoder_EncodeDaoInt( databk->end, value->data.i[i] );
		if( (i+1)<value->size ) DaoByteCoder_EncodeDaoInt( databk->end+4, value->data.i[i+1] );
	}else if( value->etype == DAO_FLOAT ){
		for(i=0; (i+1)<value->size; i+=1){
			databk = DaoByteBlock_NewBlock( block, DAO_ASM_DATA );
			DaoByteCoder_EncodeDouble( databk->begin, value->data.f[i] );
		}
		DaoByteCoder_EncodeDouble( databk->end, value->data.f[i] );
	}else if( value->etype == DAO_DOUBLE ){
		for(i=0; (i+1)<value->size; i+=1){
			databk = DaoByteBlock_NewBlock( block, DAO_ASM_DATA );
			DaoByteCoder_EncodeDouble( databk->begin, value->data.d[i] );
		}
		DaoByteCoder_EncodeDouble( databk->end, value->data.d[i] );
	}else if( value->etype == DAO_COMPLEX ){
		for(i=0; (i+1)<value->size; i+=1){
			DaoByteBlock *databk1 = DaoByteBlock_NewBlock( block, DAO_ASM_DATA );
			DaoByteBlock *databk2 = DaoByteBlock_NewBlock( block, DAO_ASM_DATA );
			DaoByteCoder_EncodeDouble( databk1->begin, value->data.c[i].real );
			DaoByteCoder_EncodeDouble( databk2->begin, value->data.c[i].imag );
		}
		databk = DaoByteBlock_NewBlock( block, DAO_ASM_DATA );
		DaoByteCoder_EncodeDouble( databk->begin, value->data.c[i].real );
		DaoByteCoder_EncodeDouble( databk->end, value->data.c[i].imag );
	}
	return block;
}
DaoByteBlock* DaoByteBlock_EncodeList( DaoByteBlock *self, DaoList *value )
{
	DaoByteBlock *typeBlock;
	DaoByteBlock *newBlock = DaoByteBlock_FindBlock( self, (DaoValue*) value );
	
	if( newBlock ) return newBlock;

	DaoByteBlock_EncodeValues2( self, & value->items );
	typeBlock = DaoByteBlock_EncodeType( self, value->ctype );
	newBlock = DaoByteBlock_AddBlock( self, (DaoValue*) value, DAO_ASM_VALUE );

	DaoByteCoder_EncodeUInt16( newBlock->begin, DAO_LIST );
	DaoByteCoder_EncodeUInt32( newBlock->begin+4, value->items.size );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+2, typeBlock );
	DaoByteBlock_AddBlockIndexData( newBlock, 0, value->items.size );
	return newBlock;
}
DaoByteBlock* DaoByteBlock_EncodeMap( DaoByteBlock *self, DaoMap *value )
{
	DNode *it;
	DaoByteBlock *typeBlock;
	DaoByteBlock *newBlock = DaoByteBlock_FindBlock( self, (DaoValue*) value );
	
	if( newBlock ) return newBlock;

	for(it=DaoMap_First(value); it; it=DaoMap_Next(value,it)){
		DaoByteBlock *key = DaoByteBlock_EncodeValue( self, it->key.pValue );
		DaoByteBlock *value = DaoByteBlock_EncodeValue( self, it->value.pValue );
		DArray_Append( self->coder->stack, key );
		DArray_Append( self->coder->stack, value );
	}
	typeBlock = DaoByteBlock_EncodeType( self, value->ctype );
	newBlock = DaoByteBlock_AddBlock( self, (DaoValue*) value, DAO_ASM_VALUE );

	DaoByteCoder_EncodeUInt16( newBlock->begin, DAO_MAP );
	DaoByteCoder_EncodeUInt32( newBlock->begin+4, value->items->hashing );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+2, typeBlock );
	DaoByteBlock_AddBlockIndexData( newBlock, 0, 2*value->items->size );
	return newBlock;
}
DaoByteBlock* DaoByteBlock_EncodeTuple( DaoByteBlock *self, DaoTuple *value )
{
	DaoByteBlock *typeBlock;
	DaoByteBlock *newBlock = DaoByteBlock_FindBlock( self, (DaoValue*) value );
	
	if( newBlock ) return newBlock;

	DaoByteBlock_EncodeValues( self, value->items, value->size );
	typeBlock = DaoByteBlock_EncodeType( self, value->ctype );
	newBlock = DaoByteBlock_AddBlock( self, (DaoValue*) value, DAO_ASM_VALUE );

	DaoByteCoder_EncodeUInt16( newBlock->begin, DAO_TUPLE );
	DaoByteCoder_EncodeUInt16( newBlock->begin+4, value->size );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+2, typeBlock );
	DaoByteBlock_AddBlockIndexData( newBlock, 1, value->size );
	return newBlock;
}
DaoByteBlock* DaoByteBlock_EncodeNameValue( DaoByteBlock *self, DaoNameValue* value )
{
	DaoByteBlock *typeBlock, *valueBlock, *nameBlock;
	DaoByteBlock *newBlock = DaoByteBlock_FindBlock( self, (DaoValue*) value );
	
	if( newBlock ) return newBlock;

	typeBlock = DaoByteBlock_EncodeType( self, value->ctype );
	valueBlock = DaoByteBlock_EncodeValue( self, value->value );
	nameBlock = DaoByteBlock_EncodeString( self, value->name );
	newBlock = DaoByteBlock_AddBlock( self, (DaoValue*) value, DAO_ASM_VALUE );

	DaoByteCoder_EncodeUInt16( newBlock->begin, DAO_PAR_NAMED );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+2, nameBlock );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+4, valueBlock );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+6, typeBlock );
	return newBlock;
}
DaoByteBlock* DaoByteBlock_EncodeValue( DaoByteBlock *self, DaoValue *value )
{
	DaoByteBlock *newBlock;
	if( value == NULL ) return NULL;
	newBlock = DaoByteBlock_FindBlock( self, value );
	if( newBlock ) return newBlock;
	switch( value->type ){
	case DAO_NONE : return DaoByteBlock_AddBlock( self, value, DAO_ASM_VALUE );
	case DAO_INTEGER : return DaoByteBlock_EncodeInteger( self, value->xInteger.value );
	case DAO_FLOAT   : return DaoByteBlock_EncodeFloat( self, value->xFloat.value );
	case DAO_DOUBLE  : return DaoByteBlock_EncodeDouble( self, value->xDouble.value );
	case DAO_COMPLEX : return DaoByteBlock_EncodeComplex( self, (DaoComplex*) value );
	case DAO_LONG   : return DaoByteBlock_EncodeLong( self, (DaoLong*) value );
	case DAO_STRING : return DaoByteBlock_EncodeString( self, value->xString.data );
	case DAO_ENUM : return DaoByteBlock_EncodeEnum( self, (DaoEnum*) value );
	case DAO_ARRAY  : return DaoByteBlock_EncodeArray( self, (DaoArray*) value );
	case DAO_LIST  : return DaoByteBlock_EncodeList( self, (DaoList*) value );
	case DAO_MAP   : return DaoByteBlock_EncodeMap( self, (DaoMap*) value );
	case DAO_TUPLE : return DaoByteBlock_EncodeTuple( self, (DaoTuple*) value );
	case DAO_TYPE  : return DaoByteBlock_EncodeType( self, (DaoType*) value );
	case DAO_PAR_NAMED : return DaoByteBlock_EncodeNameValue( self, (DaoNameValue*) value );
	case DAO_CTYPE : printf( "Unencoded value type: %s!\n", value->xCtype.ctype->name->mbs ); break;
	case DAO_CDATA : printf( "Unencoded value type: %s!\n", value->xCtype.ctype->name->mbs ); break;
	default: printf( "Unencoded value type: %i!\n", value->type ); break;
	}
	return NULL;
}
DaoByteBlock* DaoByteBlock_EncodeEnumType( DaoByteBlock *self, DaoType *type )
{
	DaoByteBlock *newBlock = DaoByteBlock_FindBlock( self, (DaoValue*) type );
	DaoByteBlock *nameBlock;
	DNode *it;

	if( newBlock ) return newBlock;
	for(it=DMap_First(type->mapNames); it; it=DMap_Next(type->mapNames,it)){
		DaoByteBlock_EncodeString( self, it->key.pString );
	}
	nameBlock = DaoByteBlock_EncodeString( self, type->name );
	newBlock = DaoByteBlock_AddBlock( self, (DaoValue*) type, DAO_ASM_ENUM );

	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin, nameBlock );
	DaoByteCoder_EncodeUInt16( newBlock->begin+2, type->flagtype );
	DaoByteCoder_EncodeUInt32( newBlock->begin+4, type->mapNames->size );
	if( type->mapNames->size == 0 ) return newBlock;
	for(it=DMap_First(type->mapNames); it; it=DMap_Next(type->mapNames,it)){
		DaoByteBlock *data = DaoByteBlock_NewBlock( newBlock, DAO_ASM_DATA );
		DaoByteBlock *namebk = DaoByteBlock_EncodeString( self, it->key.pString );
		DaoByteBlock_InsertBlockIndex( data, data->begin, namebk );
		DaoByteCoder_EncodeUInt32( data->begin+2, it->value.pInt );
	}
	DaoByteBlock_CopyToEndFromBegin( newBlock, newBlock->last );
	DaoByteCoder_Remove( self->coder, newBlock->last, newBlock );
	return newBlock;
}
DaoByteBlock* DaoByteBlock_EncodeType( DaoByteBlock *self, DaoType *type )
{
	DaoByteBlock *newBlock = DaoByteBlock_FindBlock( self, (DaoValue*) type );
	DaoByteBlock *nameBlock, *auxBlock = NULL;
	int size = 0;

	if( type == NULL ) return NULL;
	if( newBlock ) return newBlock;
	if( type->tid == DAO_ENUM ) return DaoByteBlock_EncodeEnumType( self, type );
	if( type->nested ) size = DaoByteBlock_EncodeValues2( self, type->nested );
	if( type->aux ) auxBlock = DaoByteBlock_EncodeValue( self, type->aux );
	nameBlock = DaoByteBlock_EncodeString( self, type->name );
	newBlock = DaoByteBlock_AddBlock( self, (DaoValue*) type, DAO_ASM_TYPE );

	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin, nameBlock );
	if( auxBlock ) DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+4, auxBlock );
	DaoByteCoder_EncodeUInt16( newBlock->begin+2, type->tid );
	DaoByteBlock_AddBlockIndexData( newBlock, 0, size );
	return newBlock;
}
DaoByteBlock* DaoByteBlock_EncodeTypeAlias( DaoByteBlock *self, DaoType *type, DaoType *aliased, DString *alias )
{
	DaoByteBlock *newBlock = NULL;
	DaoByteBlock *nameBlock = DaoByteBlock_EncodeString( self, alias );
	DaoByteBlock *typeBlock = DaoByteBlock_EncodeType( self, type );
	if( aliased == type ){
		newBlock = DaoByteBlock_AddBlock( self, (DaoValue*) aliased, DAO_ASM_TYPEDEF );
	}else{
		newBlock = DaoByteBlock_FindBlock( self, (DaoValue*) aliased );
		if( newBlock ) return newBlock;

		newBlock = DaoByteBlock_AddBlock( self, (DaoValue*) aliased, DAO_ASM_TYPEDEF );
	}
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+0, nameBlock );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+2, typeBlock );
	return newBlock;
}
DaoByteBlock* DaoByteBlock_EncodeTypeOf( DaoByteBlock *self, DaoType *type, DaoValue *value )
{
	DaoByteBlock *newBlock = DaoByteBlock_FindBlock( self, (DaoValue*) type );
	DaoByteBlock *valBlock = DaoByteBlock_FindBlock( self, value );
	if( newBlock ) return newBlock;
	if( valBlock == NULL ) return NULL;

	newBlock = DaoByteBlock_AddBlock( self, (DaoValue*) type, DAO_ASM_TYPEOF );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin, valBlock );
	return newBlock;
}
DaoByteBlock* DaoByteBlock_EncodeCtype( DaoByteBlock *self, DaoCtype *ctype, DaoCtype *generic, DaoType **types, int n )
{
	DaoByteBlock *newBlock = DaoByteBlock_FindBlock( self, (DaoValue*) ctype );
	DaoByteBlock *genBlock = DaoByteBlock_FindBlock( self, (DaoValue*) generic );

	if( newBlock ) return newBlock;
	DaoByteBlock_EncodeValues( self, (DaoValue**) types, n );
	newBlock = DaoByteBlock_AddBlock( self, (DaoValue*) ctype, DAO_ASM_VALUE );
	DaoByteCoder_EncodeUInt16( newBlock->begin, DAO_CTYPE );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+2, genBlock );
	DaoByteBlock_AddBlockIndexData( newBlock, 2, n );
	return newBlock;
}



DaoByteBlock* DaoByteBlock_AddRoutineBlock( DaoByteBlock *self, DaoRoutine *routine, int perm )
{
	DaoByteBlock *decl = DaoByteBlock_FindBlock( self, (DaoValue*) routine );
	DaoByteBlock *type = DaoByteBlock_EncodeType( self, routine->routType );
	DaoByteBlock *host = DaoByteBlock_EncodeType( self, routine->routHost );
	DaoByteBlock *name = DaoByteBlock_EncodeString( self, routine->routName );
	DaoByteBlock *rout = DaoByteBlock_AddBlock( self, (DaoValue*) routine, DAO_ASM_ROUTINE );
	if( decl ){
		DaoByteBlock_InsertBlockIndex( rout, rout->begin, decl );
	}else{
		DaoByteBlock_InsertBlockIndex( rout, rout->begin, name );
	}
	DaoByteBlock_InsertBlockIndex( rout, rout->begin+2, type );
	if( host ) DaoByteBlock_InsertBlockIndex( rout, rout->begin+4, host );
	DaoByteCoder_EncodeUInt16( rout->begin+6, routine->attribs );
	if( routine->routHost && routine->routHost->tid == DAO_CLASS ){
		rout->end[6] = routine == routine->routHost->aux->xClass.classRoutine;
	}
	rout->end[7] = perm;
	return rout;
}
DaoByteBlock* DaoByteBlock_AddClassBlock( DaoByteBlock *self, DaoClass *klass, int perm )
{
	daoint i, j;
	DaoByteBlock *decl = DaoByteBlock_FindBlock( self, (DaoValue*) klass );
	DaoByteBlock *name = DaoByteBlock_EncodeString( self, klass->className );
	DaoByteBlock *parent = DaoByteBlock_EncodeValue( self, klass->parent );
	DaoByteBlock *block = DaoByteBlock_AddBlock( self, (DaoValue*) klass, DAO_ASM_CLASS );
	if( decl ){
		DaoByteBlock_InsertBlockIndex( block, block->begin, decl );
	}else{
		DaoByteBlock_InsertBlockIndex( block, block->begin, name );
	}
	DaoByteBlock_InsertBlockIndex( block, block->begin+2, parent );
	DaoByteCoder_EncodeUInt32( block->begin+4, klass->attribs );
	block->end[7] = perm;
	for(i=0; i<klass->mixinBases->size; i+=4){
		DaoByteBlock *data = DaoByteBlock_NewBlock( block, DAO_ASM_BASES );
		for(j=0; j<4; ++j){
			if( i+j >= klass->mixinBases->size ) break;
			decl = DaoByteBlock_FindBlock( self, klass->mixinBases->items.pValue[i+j] );
			DaoByteBlock_InsertBlockIndex( data, data->begin + 2*j, decl );
		}
	}
	return block;
}
DaoByteBlock* DaoByteBlock_AddInterfaceBlock( DaoByteBlock *self, DaoInterface *inter, int pm )
{
	daoint i, j;
	DaoByteBlock *decl = DaoByteBlock_FindBlock( self, (DaoValue*) inter );
	DaoByteBlock *name = DaoByteBlock_EncodeString( self, inter->abtype->name );
	DaoByteBlock *block = DaoByteBlock_AddBlock( self, (DaoValue*) inter, DAO_ASM_INTERFACE );
	if( decl ){
		DaoByteBlock_InsertBlockIndex( block, block->begin, decl );
	}else{
		DaoByteBlock_InsertBlockIndex( block, block->begin, name );
	}
	DaoByteCoder_EncodeUInt16( block->begin+2, inter->supers->size );
	block->end[7] = pm;
	for(i=0; i<inter->supers->size; i+=4){
		DaoByteBlock *data = DaoByteBlock_NewBlock( block, DAO_ASM_BASES );
		for(j=0; j<4; ++j){
			if( i+j >= inter->supers->size ) break;
			decl = DaoByteBlock_FindBlock( self, inter->supers->items.pValue[i+j] );
			DaoByteBlock_InsertBlockIndex( data, data->begin + 2*j, decl );
		}
	}
	return block;
}

DaoByteBlock* DaoByteBlock_EncodeLoadStmt( DaoByteBlock *self, DString *mod, DString *ns )
{
	DaoByteBlock *fileBlock = DaoByteBlock_EncodeString( self, mod );
	DaoByteBlock *nameBlock = ns ? DaoByteBlock_EncodeString( self, ns ) : NULL;
	DaoByteBlock *newBlock = DaoByteBlock_NewBlock( self, DAO_ASM_LOAD );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin, fileBlock );
	if( ns ) DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+2, nameBlock );
	return newBlock;
}
DaoByteBlock* DaoByteBlock_EncodeUseStmt( DaoByteBlock *self, DaoValue *value, int tag )
{
	DaoByteBlock *valueBlock = DaoByteBlock_FindBlock( self, value );
	DaoByteBlock *newBlock = DaoByteBlock_NewBlock( self, DAO_ASM_USE );
	DaoByteCoder_EncodeUInt16( newBlock->begin, tag );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin + 2, valueBlock );
	return newBlock;
}
DaoByteBlock* DaoByteBlock_AddEvalBlock( DaoByteBlock *self, DaoValue *value, int code, int opb, DaoType *type )
{
	DaoByteBlock *tblock = DaoByteBlock_EncodeType( self, type );
	DaoByteBlock *block = DaoByteBlock_AddBlock( self, value, DAO_ASM_EVAL );
	DaoByteCoder_EncodeUInt16( block->begin, code );
	DaoByteCoder_EncodeUInt16( block->begin+2, opb );
	DaoByteBlock_InsertBlockIndex( block, block->begin+4, tblock );
	return block;
}

DaoByteBlock* DaoByteBlock_EncodeDeclConst( DaoByteBlock *self, DString *name, DaoValue *value, int perm )
{
	DaoByteBlock *nameBlock = DaoByteBlock_EncodeString( self, name );
	DaoByteBlock *valueBlock = DaoByteBlock_EncodeValue( self, value );
	DaoByteBlock *newBlock = DaoByteBlock_NewBlock( self, DAO_ASM_CONST );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin, nameBlock );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+2, valueBlock );
	DaoByteCoder_EncodeUInt16( newBlock->begin+6, perm );
	return newBlock;
}
DaoByteBlock* DaoByteBlock_EncodeDeclaration( DaoByteBlock *self, int tag, DString *name, DaoValue *value, DaoType *type, int perm )
{
	DaoByteBlock *nameBlock = name ? DaoByteBlock_EncodeString( self, name ) : NULL;
	DaoByteBlock *valueBlock = DaoByteBlock_EncodeValue( self, value );
	DaoByteBlock *typeBlock = DaoByteBlock_EncodeType( self, type );
	DaoByteBlock *newBlock = DaoByteBlock_NewBlock( self, tag );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin, nameBlock );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+2, valueBlock );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin+4, typeBlock );
	DaoByteCoder_EncodeUInt16( newBlock->begin+6, perm );
	return newBlock;
}
DaoByteBlock* DaoByteBlock_EncodeDeclVar( DaoByteBlock *self, DString *name, DaoValue *value, DaoType *type, int perm )
{
	return DaoByteBlock_EncodeDeclaration( self, DAO_ASM_VAR, name, value, type, perm );
}
DaoByteBlock* DaoByteBlock_EncodeDeclStatic( DaoByteBlock *self, DString *name, DaoValue *value, DaoType *type, int perm )
{
	return DaoByteBlock_EncodeDeclaration( self, DAO_ASM_STATIC, name, value, type, perm );
}
DaoByteBlock* DaoByteBlock_EncodeDeclGlobal( DaoByteBlock *self, DString *name, DaoValue *value, DaoType *type, int perm )
{
	return DaoByteBlock_EncodeDeclaration( self, DAO_ASM_GLOBAL, name, value, type, perm );
}
DaoByteBlock* DaoByteBlock_EncodeSeekStmt( DaoByteBlock *self, DaoByteBlock *target )
{
	DaoByteBlock *newBlock = DaoByteBlock_NewBlock( self, DAO_ASM_SEEK );
	DaoByteBlock_InsertBlockIndex( newBlock, newBlock->begin, target );
	return newBlock;
}



static int DaoByteCoder_UpdateIndex( DaoByteCoder *self, DaoByteBlock *block )
{
	int inserted = 0;
	DaoByteBlock *pb = block->first;
	DNode *it;

	self->index += block->type > 0 && block->type <= DAO_ASM_EVAL;
	block->index = self->index;
	if( block->wordToBlocks ){
		for(it=DMap_First(block->wordToBlocks); it; it=DMap_Next(block->wordToBlocks,it)){
			DaoByteBlock *pb = (DaoByteBlock*) it->value.pVoid;
			if( pb != NULL && block->index > (pb->index + 0x7fff) ){
				it->value.pVoid = DaoByteBlock_EncodeSeekStmt( block, pb );
				inserted += 1;
			}
		}
	}
	while( pb ){
		inserted += DaoByteCoder_UpdateIndex( self, pb );
		pb = pb->next;
	}
	return inserted;
}
static void DaoByteCoder_FinalizeBlock( DaoByteCoder *self, DaoByteBlock *block )
{
	DaoNamespace *nspace;
	DaoByteBlock *cur, *defblock;
	DaoByteBlock *consts, *types, *code;
	DaoByteBlock *pb = block->first;
	DaoRoutine *routine;
	DMap *glbnames;
	DMap *varblocks;
	DMap *vartypes;
	DNode *it;
	uchar_t *data;
	int i, N;

	while( pb ){
		DaoByteCoder_FinalizeBlock( self, pb );
		pb = pb->next;
	}
	defblock = DaoByteBlock_FindBlock( block, block->value );
	if( defblock && defblock != block ) return; /* Just declaration; */
	if( block->type == DAO_ASM_CLASS ){
		DaoClass *klass = (DaoClass*) block->value;
		DaoByteCoder_EncodeUInt32( block->begin+4, klass->attribs );
	}

	if( block->type != DAO_ASM_ROUTINE ) return;

	routine = (DaoRoutine*) block->value;
	if( routine->body == NULL ) return;

	nspace = routine->nameSpace;
	DaoByteCoder_EncodeUInt16( block->begin+6, routine->attribs );
	DaoByteCoder_EncodeUInt16( block->end, routine->body->regCount );

	/* local constants: */
	N = DaoByteBlock_EncodeValues2( block, & routine->routConsts->items );
	consts = DaoByteBlock_NewBlock( block, DAO_ASM_CONSTS );
	DaoByteCoder_EncodeUInt16( consts->begin, N );
	DaoByteBlock_AddBlockIndexData( consts, 3, N );

	/* explicit types: */
	varblocks = DMap_New(0,0);
	vartypes = routine->body->localVarType;
	for(it=DMap_First(vartypes); it; it=DMap_Next(vartypes,it)){
		DaoByteBlock *pb = DaoByteBlock_EncodeType( block, it->value.pType );
		DMap_Insert( varblocks, it->key.pVoid, pb );
	}
	types = DaoByteBlock_NewBlock( block, DAO_ASM_TYPES );
	DaoByteCoder_EncodeUInt16( types->begin, vartypes->size );
	cur = types;
	data = types->begin + 4;
	for(it=DMap_First(varblocks); it; it=DMap_Next(varblocks,it), data+=4){
		DaoByteBlock *pb = (DaoByteBlock*) it->value.pVoid;
		if( data >= cur->begin + 8 ){
			cur = DaoByteBlock_NewBlock( types, DAO_ASM_DATA );
			data = cur->begin;
		}
		DaoByteCoder_EncodeUInt16( data, it->key.pInt );
		DaoByteBlock_InsertBlockIndex( cur, data+2, pb );
	}
	if( cur != types ){
		DaoByteBlock_CopyToEndFromBegin( types, cur );
		DaoByteCoder_Remove( types->coder, cur, types );
	}
	DMap_Delete( varblocks );

	glbnames = DHash_New(0,0);
	for(it=DMap_First(nspace->lookupTable); it; it=DMap_Next(nspace->lookupTable,it)){
		DMap_Insert( glbnames, IntToPointer( LOOKUP_ID(it->value.pInt) ), it->key.pVoid );
	}

	/* code: */
	code = DaoByteBlock_NewBlock( block, DAO_ASM_CODE );
	self->lines->size = 0;
	for(i=0; i<routine->body->annotCodes->size; ++i){
		DaoVmCodeX *vmc = routine->body->annotCodes->items.pVmc[i];
		int count = self->lines->size;
		int lastline = count ? self->lines->items.pInt[count-2] : -1;
		int lastcount = count ? self->lines->items.pInt[count-1] : -1;
		if( vmc->line != lastline ){
			DArray_PushBack( self->lines, IntToPointer( vmc->line ) );
			DArray_PushBack( self->lines, IntToPointer( 1 ) );
		}else{
			self->lines->items.pInt[count-1] += 1;
		}
	}
	DaoByteCoder_EncodeUInt16( code->begin, routine->body->annotCodes->size );
	DaoByteCoder_EncodeUInt16( code->begin + 2, self->lines->size/2 );
	cur = code;
	data = cur->begin + 4;
	for(i=0; i<self->lines->size; i+=2, data+=4){
		int last = i ? self->lines->items.pInt[i-2] : 0;
		int line = self->lines->items.pInt[i];
		int count = self->lines->items.pInt[i+1];
		if( data >= cur->begin + 8 ){
			cur = DaoByteBlock_NewBlock( code, DAO_ASM_DATA );
			data = cur->begin;
		}
		DaoByteCoder_EncodeUInt16( data, line - last );
		DaoByteCoder_EncodeUInt16( data+2, count );
	}

	for(i=0; i<routine->body->annotCodes->size; ++i){
		DMap *id2names = NULL;
		DaoVmCodeX *vmc = routine->body->annotCodes->items.pVmc[i];
		switch( vmc->code ){
		case DVM_GETCK : case DVM_GETVK : case DVM_SETVK :
			break;
		case DVM_GETVO : case DVM_SETVO :
			break;
		case DVM_GETCG : case DVM_GETVG : case DVM_SETVG :
			id2names = glbnames;
			break;
		}
		cur = DaoByteBlock_NewBlock( code, DAO_ASM_DATA );
		DaoByteCoder_EncodeUInt16( cur->begin + 0, vmc->code );
		DaoByteCoder_EncodeUInt16( cur->begin + 2, vmc->a );
		DaoByteCoder_EncodeUInt16( cur->begin + 4, vmc->b );
		DaoByteCoder_EncodeUInt16( cur->begin + 6, vmc->c );
		if( id2names ){
			it = DMap_Find( id2names, IntToPointer(vmc->b) );
			if( it == NULL ){
				//XXX
				continue;
			}
			pb = DaoByteBlock_EncodeString( block, it->value.pString );
			DaoByteBlock_InsertBlockIndex( cur, cur->begin+4, pb );
		}
	}
	if( cur != code ){
		DaoByteBlock_CopyToEndFromBegin( code, cur );
		DaoByteCoder_Remove( code->coder, cur, code );
	}
	if( code != block->last ){ /* encoded new names */
		code->prev->next = code->next;
		code->next->prev = code->prev;
		code->prev = block->last;
		code->next = NULL;
		block->last->next = code;
		block->last = code;
	}
	DMap_Delete( glbnames );
}
static void DaoByteCoder_FinalizeEncoding( DaoByteCoder *self, DaoByteBlock *block )
{
	DaoByteBlock *pb = block->first;
	DaoRoutine *routine;
	DNode *it;
	int i, N;

	if( block->wordToBlocks ){
		for(it=DMap_First(block->wordToBlocks); it; it=DMap_Next(block->wordToBlocks,it)){
			DaoByteBlock *pb = (DaoByteBlock*) it->value.pVoid;
			int diff = 0;
			if( pb != NULL ){
				DaoByteBlock *base = block;
				if( base->type == DAO_ASM_DATA || base->type == DAO_ASM_DATA2 )
					base = base->parent;
				diff = base->index - pb->index;
				/*
				// For a non-value block, its index is the same as the last
				// value block. So one should be added to the difference.
				*/
				diff += (base->type == 0 || base->type > DAO_ASM_EVAL);
#if 0
				if( diff < 0 && pb->value ){
					printf( "==================== %i %i\n", diff, pb->value->type );
					if( pb->value->type == DAO_ROUTINE )
						printf( "%s\n", pb->value->xRoutine.routName->mbs );
				}
#endif
			}
			DaoByteCoder_EncodeUInt16( (uchar_t*) it->key.pVoid, diff );
		}
	}
	while( pb ){
		DaoByteCoder_FinalizeEncoding( self, pb );
		pb = pb->next;
	}
}
static void DaoByteCoder_MergeData( DaoByteCoder *self, DaoByteBlock *block )
{
	uint_t A, B, C, D, E, F, G, H;
	DaoByteBlock *pb = block->first;
	while( pb ){
		if( pb->type == DAO_ASM_DATA ){
			DaoByteBlock *pb2 = pb->next;
			DaoByteCoder_DecodeChunk2222( pb->begin, & A, & B,  & C, & D );
			if( A <= 0xff && B <= 0xff && C <= 0xff && D <= 0xff && pb2 != NULL ){
				DaoByteCoder_DecodeChunk2222( pb2->begin, & E, & F,  & G, & H );
				if( E <= 0xff && F <= 0xff && G <= 0xff && H <= 0xff ){
					pb->begin[0] = A; pb->begin[1] = B; pb->begin[2] = C; pb->begin[3] = D;
					pb->begin[4] = E; pb->begin[5] = F; pb->begin[6] = G; pb->begin[7] = H;
					pb->type = DAO_ASM_DATA2;
					pb2->type = DAO_ASM_NONE;
					pb = pb->next;
				}
			}
		}else{
			DaoByteCoder_MergeData( self, pb );
		}
		pb = pb->next;
	}
}
static void DaoByteCoder_SplitData( DaoByteCoder *self, DaoByteBlock *block )
{
	int i;
	DaoByteBlock *pb = block->first;
	while( pb ){
		if( pb->type == DAO_ASM_DATA2 ){
			DaoByteBlock *pb2 = pb->next;
			if( pb2 == NULL || pb2->type != DAO_ASM_NONE ){
				pb2 = DaoByteCoder_NewBlock( self, DAO_ASM_DATA );
				pb2->parent = block;
				pb2->next = pb->next;
				pb2->prev = pb;
				if( pb->next ) pb->next->prev = pb2;
				pb->next = pb2;
			}
			pb->type = DAO_ASM_DATA;
			pb2->type = DAO_ASM_DATA;
			for(i=0; i<4; ++i)  DaoByteCoder_EncodeUInt16( pb2->begin + 2*i, pb->begin[4+i] );
			for(i=3; i>=0; --i) DaoByteCoder_EncodeUInt16( pb->begin + 2*i, pb->begin[i] );
			pb = pb->next;
		}else{
			DaoByteCoder_SplitData( self, pb );
		}
		pb = pb->next;
	}
}
void DaoByteCoder_Finalize( DaoByteCoder *self )
{
	if( self->top == NULL ) return;

	DaoByteCoder_FinalizeBlock( self, self->top );

	self->index = 0;
	while( DaoByteCoder_UpdateIndex( self, self->top ) ) self->index = 0;
	DaoByteCoder_FinalizeEncoding( self, self->top );
}

void DaoByteBlock_EncodeToString( DaoByteBlock *block, DString *output )
{
	DaoByteBlock *pb = block->first;

	if( block->type == DAO_ASM_NONE ) return;

	DString_AppendChar( output, block->type );
	DString_AppendDataMBS( output, (char*) block->begin, 8 );
	while( pb ){
		DaoByteBlock_EncodeToString( pb, output );
		pb = pb->next;
	}
	if( block->type < DAO_ASM_ROUTINE || block->type > DAO_ASM_CODE ) return;
	DString_AppendChar( output, DAO_ASM_END );
	DString_AppendDataMBS( output, (char*) block->end, 8 );
}
void DaoByteCoder_EncodeToString( DaoByteCoder *self, DString *output )
{
	if( self->top == NULL ) return;
	DaoByteCoder_Finalize( self );
	DaoByteCoder_MergeData( self, self->top );
	DaoByteBlock_EncodeToString( self->top, output );
}

void DaoByteCoder_EncodeHeader( DaoByteCoder *self, const char *fname, DString *output )
{
	DString *path = DString_New(1);
	char *daodir = getenv( "DAO_DIR" );
	char *home = getenv( "HOME" );
	uchar_t bytes[2];

	DString_SetMBS( path, fname );
	if( DString_Find( path, self->vmspace->startPath, 0 ) == 0 ){
		DString_ReplaceMBS( path, "$(CMD_DIR)/", 0, self->vmspace->startPath->size );
	}else if( DString_Find( path, self->vmspace->daoBinPath, 0 ) == 0 ){
		DString_ReplaceMBS( path, "$(EXE_DIR)/", 0, self->vmspace->daoBinPath->size );
	}else if( daodir && DString_FindMBS( path, daodir, 0 ) == 0 ){
		DString_ReplaceMBS( path, "$(DAO_DIR)", 0, strlen(daodir) );
	}else if( home && DString_FindMBS( path, home, 0 ) == 0 ){
		DString_ReplaceMBS( path, "$(HOME)", 0, strlen(home) );
	}

	DaoByteCoder_EncodeUInt16( bytes, path->size );

	DString_AppendDataMBS( output, DAO_BC_SIGNATURE "\0", 9 );
	DString_AppendChar( output, sizeof(daoint) == 4 ? '\4' : '\x8' );
	DString_AppendDataMBS( output, "\0\0\0\0\0", 5 );
	DString_AppendDataMBS( output, "\0\0\0\0\0", 5 );
	DString_AppendDataMBS( output, "\0\0\0\0\0", 5 );
	DString_AppendDataMBS( output, "\0\0\0\0\0\r\n", 7 );

	DString_AppendDataMBS( output, (char*) bytes, 2 );
	DString_AppendDataMBS( output, path->mbs, path->size );
	DString_AppendDataMBS( output, "\0\r\n", 3 );
	DString_Delete( path );
}





int DaoByteCoder_Decode( DaoByteCoder *self, DString *input )
{
	daoint i;
	DString header = *input;
	DString signature = DString_WrapBytes( DAO_BC_SIGNATURE, 8 );

	if( input->mbs == NULL ) return 0;

	if( header.size > 8 ) header.size = 8;
	if( DString_EQ( & header, & signature ) == 0 ) return 0;
	if( input->mbs[8] != 0 ) return 0; /* Not official format; */

	DaoByteCoder_Init( self );

	self->intSize = input->mbs[9];
	self->codes = (uchar_t*) input->mbs + 32;
	self->end = (uchar_t*) input->mbs + input->size;
	self->error = self->end + 1;
	if( self->intSize != 4 && self->intSize != 8 ) self->codes = self->error;

	i = DaoByteCoder_DecodeUInt16( self->codes );
	DString_Reset( self->path, i );
	memcpy( self->path->mbs, self->codes + 2, i*sizeof(char) );

	DArray_Append( self->stack, self->top );

	self->codes += i + 5;
	self->codes += 9;  /* Skip: ASM_ROUTINE: 0, 0, 0, 0; */
	for(; self->codes < self->end; self->codes += 9){
		DaoByteBlock *current = (DaoByteBlock*) DArray_Back( self->stack );
		int type = self->codes[0];
		if( type == DAO_ASM_END ){
			memcpy( current->end, self->codes+1, 8*sizeof(char) );
			DArray_PopBack( self->stack );
		}else{
			DaoByteBlock *sublock = DaoByteBlock_NewBlock( current, type );
			memcpy( sublock->begin, self->codes+1, 8*sizeof(char) );
			if( type >= DAO_ASM_ROUTINE && type <= DAO_ASM_CODE ){
				DArray_PushBack( self->stack, sublock );
			}
		}
	}

	return 1;
}


static int DaoByteCoder_DecodeBlock( DaoByteCoder *self, DaoByteBlock *block );

static DaoByteBlock* DaoByteCoder_LookupBlock( DaoByteCoder *self, DaoByteBlock *block, int index )
{
	/*
	// For a value block, it has been already pushed to the stack, so the
	// base index 1 refers to itself. Adding one to refer to the correct one.
	*/
	if( index == 0 ) return NULL;
	if( block->type == DAO_ASM_DATA || block->type == DAO_ASM_DATA2 ) block = block->parent;
	index += block->type && block->type <= DAO_ASM_EVAL;
	return (DaoByteBlock*) self->stack->items.pVoid[ self->stack->size - index ];
}

static void DaoByteCoder_GetBlocks( DaoByteCoder *self, DaoByteBlock *block, uchar_t *data, uchar_t *end, int stride, int nullterm )
{
	for(; data < end; data+=stride){
		uint_t idx = DaoByteCoder_DecodeUInt16( data );
		DaoByteBlock *pb = DaoByteCoder_LookupBlock( self, block, idx );;
		if( idx == 0 && nullterm ) break;
		DArray_Append( self->iblocks, idx ? pb : NULL );
	}
}
static void DaoByteCoder_GetIntegers( DaoByteCoder *self, DArray *ints, uchar_t *data, uchar_t *end, int stride )
{
	for(; data < end; data+=stride){
		uint_t idx = DaoByteCoder_DecodeUInt16( data );
		DArray_Append( ints, IntToPointer( idx ) );
	}
}
void DaoByteBlock_GetAllBlocks( DaoByteCoder *self, DaoByteBlock *block, int head, int size, int nullterm )
{
	DaoByteBlock *pb, *dataBlock = block;
	uchar_t *data = dataBlock->begin + 2*(4-head);
	uchar_t *end = dataBlock->begin + 8;
	uint_t idx;
	for(; ; data+=2){
		if( data >= end ){
			if( end == block->end + 8 ) break;
			if( dataBlock == block ){
				dataBlock = block->first;
			}else{
				dataBlock = dataBlock->next;
			}
			if( dataBlock ){
				data = dataBlock->begin;
			}else{
				dataBlock = block;
				data = dataBlock->end;
			}
			end = data + 8;
		}
		idx = DaoByteCoder_DecodeUInt16( data );
		pb = DaoByteCoder_LookupBlock( self, dataBlock, idx );;
		if( idx == 0 && nullterm ) break;
		DArray_Append( self->iblocks, pb );
	}
}
void DaoByteBlock_GetAllValues( DaoByteCoder *self, DaoByteBlock *block, int head, int size, int nullterm )
{
	uint_t i, offset = self->iblocks->size;
	DaoByteBlock_GetAllBlocks( self, block, head, size, nullterm );
	for(i=offset; i<self->iblocks->size; ++i){
		DaoByteBlock *pb = (DaoByteBlock*) self->iblocks->items.pVoid[i];
		DArray_Append( self->ivalues, pb->value );
	}
	DArray_Erase( self->iblocks, offset, -1 );
}

static int DaoByteCoder_DecodeValue( DaoByteCoder *self, DaoByteBlock *block )
{
	DLong *dlong;
	DaoMap *map;
	DaoList *list;
	DaoType *type;
	DaoType **itypes;
	DaoTuple *tuple;
	DaoArray *array;
	DaoValue *value = NULL;
	DaoNameValue *namevalue;
	DaoByteBlock *pb = block->first;
	DaoByteBlock *pb2, *pb3, *namebk, *valuebk, *typebk = NULL;
	uint_t tid = DaoByteCoder_DecodeUInt16( block->begin );
	uint_t i, A, B, C, D, ids[4];

	switch( tid ){
	case DAO_NONE :
		value = dao_none_value;
		break;
	case DAO_INTEGER :
		value = (DaoValue*) DaoInteger_New(0);
		value->xInteger.value = DaoByteCoder_DecodeDaoInt( self, block->end );
		break;
	case DAO_FLOAT :
		value = (DaoValue*) DaoFloat_New(0.0);
		value->xFloat.value = DaoByteCoder_DecodeDouble( self, block->end );
		break;
	case DAO_DOUBLE :
		value = (DaoValue*) DaoDouble_New(0.0);
		value->xDouble.value = DaoByteCoder_DecodeDouble( self, block->end );
		break;
	case DAO_COMPLEX :
		value = (DaoValue*) DaoComplex_New2(0.0,0.0);
		value->xComplex.value.real = DaoByteCoder_DecodeDouble( self, block->first->begin );
		value->xComplex.value.imag = DaoByteCoder_DecodeDouble( self, block->end );
		pb = pb->next;
		break;
	case DAO_LONG :
		value = (DaoValue*) DaoLong_New();
		dlong = value->xLong.value;
		dlong->base = block->begin[2];
		dlong->sign = block->begin[3];
		DLong_Resize( dlong, DaoByteCoder_DecodeUInt32( block->begin+4 ) );
		for(i=0; pb; pb=pb->next, i+=8){
			memcpy( dlong->data + i, pb->begin, 8*sizeof(char) );
		}
		memcpy( dlong->data + i, pb->end, (dlong->size - i)*sizeof(char) );
		break;
	case DAO_STRING :
		A = DaoByteCoder_DecodeUInt16( block->begin+2 );
		B = DaoByteCoder_DecodeUInt32( block->begin+4 );
		value = (DaoValue*) DaoString_New( A == 0 );
		DString_Reserve( value->xString.data, B );
		if( A ){
			while( pb ){
				C = DaoByteCoder_DecodeUInt32( pb->begin );
				D = DaoByteCoder_DecodeUInt32( pb->begin + 4 );
				DString_AppendWChar( value->xString.data, C );
				DString_AppendWChar( value->xString.data, D );
				pb = pb->next;
			}
			C = DaoByteCoder_DecodeUInt32( block->end );
			DString_AppendWChar( value->xString.data, C );
			if( B%2 == 0 ){
				D = DaoByteCoder_DecodeUInt32( block->end + 4 );
				DString_AppendWChar( value->xString.data, D );
			}
		}else{
			while( pb ){
				DString_AppendDataMBS( value->xString.data, (char*) pb->begin, 8 );
				pb = pb->next;
			}
			DString_AppendDataMBS( value->xString.data, (char*)block->end, B%8 ? B%8 : 8 );
			//printf( ">>>>>>>>>>>>>> %p %s\n", block, value->xString.data->mbs );
		}
		break;
	case DAO_ENUM :
		A = DaoByteCoder_DecodeUInt16( block->begin+2 );
		B = DaoByteCoder_DecodeUInt32( block->end );
		typebk = DaoByteCoder_LookupBlock( self, block, A );
		value = (DaoValue*) DaoEnum_New( (DaoType*) typebk->value, B );
		break;
	case DAO_ARRAY :
		A = block->begin[2];
		B = D = block->begin[3];
		C = DaoByteCoder_DecodeUInt32( block->begin+4 );
		i = 0;
		array = DaoArray_New( A );
		value = (DaoValue*) array;
		DaoArray_SetDimCount( array, D );
		while( B > 0 && pb != NULL ){
			if( i < D ) array->dims[i++] = DaoByteCoder_DecodeUInt32( pb->begin );
			if( i < D ) array->dims[i++] = DaoByteCoder_DecodeUInt32( pb->begin+4 );
			pb = pb->next;
			B -= 2;
		}
		DaoArray_ResizeArray( array, array->dims, array->ndim );
		if( array->size != C ) return 0;

		if( array->etype == DAO_INTEGER && sizeof(daoint) == 8 ){
			for(i=0; (i+1)<C && pb != NULL; i+=1, pb=pb->next){
				array->data.i[i] = DaoByteCoder_DecodeDaoInt( self, pb->begin );
			}
			array->data.i[i] = DaoByteCoder_DecodeDaoInt( self, block->end );
		}else if( array->etype == DAO_INTEGER ){
			for(i=0; (i+2)<C && pb != NULL; i+=2, pb=pb->next){
				array->data.i[i] = DaoByteCoder_DecodeDaoInt( self, pb->begin );
				array->data.i[i+1] = DaoByteCoder_DecodeDaoInt( self, pb->begin+4 );
			}
			array->data.i[i] = DaoByteCoder_DecodeDaoInt( self, block->end );
			if( !(C%2) ) array->data.i[i+1] = DaoByteCoder_DecodeDaoInt( self, block->end+4 );
		}else if( array->etype == DAO_FLOAT ){
			for(i=0; (i+1)<C && pb != NULL; i+=1, pb=pb->next){
				array->data.f[i] = DaoByteCoder_DecodeDouble( self, pb->begin );
			}
			array->data.f[i] = DaoByteCoder_DecodeDouble( self, block->end );
		}else if( array->etype == DAO_DOUBLE ){
			for(i=0; (i+1)<C && pb != NULL; i+=1, pb=pb->next){
				array->data.d[i] = DaoByteCoder_DecodeDouble( self, pb->begin );
			}
			array->data.d[i] = DaoByteCoder_DecodeDouble( self, block->end );
		}else if( array->etype == DAO_COMPLEX ){
			for(i=0; (i+1)<C && pb != NULL && pb->next != NULL; i+=1, pb=pb->next->next){
				array->data.c[i].real = DaoByteCoder_DecodeDouble( self, pb->begin );
				array->data.c[i].imag = DaoByteCoder_DecodeDouble( self, pb->next->begin );
			}
			array->data.c[i].real = DaoByteCoder_DecodeDouble( self, pb->begin );
			array->data.c[i].imag = DaoByteCoder_DecodeDouble( self, block->end );
		}
		break;
	case DAO_LIST :
		A = DaoByteCoder_DecodeUInt16( block->begin+2 );
		B = DaoByteCoder_DecodeUInt32( block->begin+4 );
		list = DaoList_New();
		value = (DaoValue*) list;
		pb2 = DaoByteCoder_LookupBlock( self, block, A );
		GC_ShiftRC( pb2->value, list->ctype );
		list->ctype = (DaoType*) pb2->value;
		D = self->iblocks->size;
		DaoByteBlock_GetAllBlocks( self, block, 0, B, 1 );
		for(i=D; i<self->iblocks->size; ++i){
			pb2 = (DaoByteBlock*) self->iblocks->items.pVoid[i];
			DaoList_Append( list, pb2->value );
		}
		DArray_Erase( self->iblocks, D, -1 );
		break;
	case DAO_MAP :
		A = DaoByteCoder_DecodeUInt16( block->begin+2 );
		B = DaoByteCoder_DecodeUInt32( block->begin+4 );
		map = DaoMap_New( B );
		value = (DaoValue*) map;
		pb2 = DaoByteCoder_LookupBlock( self, block, A );
		GC_ShiftRC( pb2->value, map->ctype );
		map->ctype = (DaoType*) pb2->value;
		D = self->iblocks->size;
		DaoByteBlock_GetAllBlocks( self, block, 0, B, 1 );
		for(i=D; i<self->iblocks->size; i+=2){
			pb2 = (DaoByteBlock*) self->iblocks->items.pVoid[i];
			pb3 = (DaoByteBlock*) self->iblocks->items.pVoid[i+1];
			DaoMap_Insert( map, pb2->value, pb3->value );
		}
		DArray_Erase( self->iblocks, D, -1 );
		break;
	case DAO_TUPLE :
		A = DaoByteCoder_DecodeUInt16( block->begin+2 );
		B = DaoByteCoder_DecodeUInt16( block->begin+4 );
		pb2 = DaoByteCoder_LookupBlock( self, block, A );
		tuple = DaoTuple_Create( (DaoType*) pb2->value, B, 0 );
		value = (DaoValue*) tuple;
		DaoByteBlock_GetAllBlocks( self, block, 1, B, 1 );
		for(i=D; i<self->iblocks->size; ++i){
			pb2 = (DaoByteBlock*) self->iblocks->items.pVoid[i];
			DaoTuple_SetItem( tuple, pb2->value, i-D );
		}
		DArray_Erase( self->iblocks, D, -1 );
		break;
	case DAO_PAR_NAMED :
		B = DaoByteCoder_DecodeUInt16( block->begin+2 );
		C = DaoByteCoder_DecodeUInt16( block->begin+4 );
		D = DaoByteCoder_DecodeUInt16( block->begin+6 );
		namebk = DaoByteCoder_LookupBlock( self, block, B );
		valuebk = DaoByteCoder_LookupBlock( self, block, C );
		typebk = DaoByteCoder_LookupBlock( self, block, D );
		namevalue = DaoNameValue_New( namebk->value->xString.data, valuebk->value );
		GC_IncRC( typebk->value );
		namevalue->ctype = (DaoType*) typebk->value;
		value = (DaoValue*) namevalue;
		break;
	case DAO_CTYPE :
		C = self->ivalues->size;
		A = DaoByteCoder_DecodeUInt16( block->begin+2 );
		pb2 = DaoByteCoder_LookupBlock( self, block, A );
		DaoByteBlock_GetAllValues( self, block, 2, B, 1 );
		itypes = self->ivalues->items.pType + C;
		D = self->ivalues->size - C;
		type = DaoType_Specialize( pb2->value->xCtype.cdtype, itypes, D );
		//printf( ">>>>>>>>> %p %s %s\n", type, type->name->mbs, type->typer->name );
		//printf( "%p %p\n", type->aux, type->aux->xCtype.ctype );
		value = type->aux;
		DArray_Erase( self->ivalues, C, -1 );
		break;
	default :
		printf( "Value decoding not supported for type: %i!\n", tid );
	}
	GC_ShiftRC( value, block->value );
	block->value = value;
	//printf( "%p %p\n", block, value );
	return 1;
}
static int DaoByteCoder_DecodeType( DaoByteCoder *self, DaoByteBlock *block )
{
	DString *sname = DString_New(1);
	DaoType *type, **itypes;
	DaoByteBlock *name, *aux, *cbtype;
	DaoByteBlock *pb2, *pb = block->first;
	uint_t offset = self->ivalues->size;
	uint_t A, B, C, D, ids[4];
	int i, count;
	daoint pos;

	DaoByteCoder_DecodeChunk2222( block->begin, & A, & B, & C, & D );
	name = DaoByteCoder_LookupBlock( self, block, A );
	aux = DaoByteCoder_LookupBlock( self, block, C );
	if( B == 0 ){ /* Type aliasing; */
		DaoType *type = (DaoType*) aux->value;
		type = DaoType_Copy( type );
		DString_Assign( type->name, name->value->xString.data );
		DaoNamespace_AddType( self->nspace, type->name, type );
		DaoNamespace_AddTypeConstant( self->nspace, type->name, type );
		return 1;
	}
	cbtype = DaoByteCoder_LookupBlock( self, block, D );
	DaoByteBlock_GetAllValues( self, block, 0, -1, 1 );
	DString_Assign( sname, name->value->xString.data );
	pos = DString_FindChar( sname, '<', 0 );
	if( pos >= 0 ) DString_Erase( sname, pos, -1 );
	if( B == DAO_PAR_NAMED ){
		daoint pos = DString_FindChar( sname, ':', 0 );
		if( pos >= 0 ) DString_Erase( sname, pos, -1 );
	}else if( B == DAO_PAR_DEFAULT ){
		daoint pos = DString_FindChar( sname, '=', 0 );
		if( pos >= 0 ) DString_Erase( sname, pos, -1 );
	}
	//DaoByteCoder_PrintBlock( self, block, 0 );
	itypes = self->ivalues->items.pType + offset;
	count = self->ivalues->size - offset;
	printf( "DaoByteCoder_DecodeType1: %s %p\n", sname->mbs, aux );
	type = DaoNamespace_MakeType( self->nspace, sname->mbs, B, aux?aux->value:NULL, itypes, count );
	GC_ShiftRC( type, block->value );
	block->value = (DaoValue*) type;
	DString_Delete( sname );
	printf( "DaoByteCoder_DecodeType2: %i %p\n", B, type );
	printf( "DaoByteCoder_DecodeType2: %s %i %p\n", type->name->mbs, B, type );
	DArray_Erase( self->ivalues, offset, -1 );
	return 1;
}
static int DaoByteCoder_DecodeTypeAlias( DaoByteCoder *self, DaoByteBlock *block )
{
	uint_t A = DaoByteCoder_DecodeUInt16( block->begin );
	uint_t B = DaoByteCoder_DecodeUInt16( block->begin+2 );
	DaoByteBlock *namebk = DaoByteCoder_LookupBlock( self, block, A );
	DaoByteBlock *typebk = DaoByteCoder_LookupBlock( self, block, B );
	DString *name = namebk->value->xString.data;
	DaoType *type = (DaoType*) typebk->value;

	type = DaoType_Copy( type );
	DString_Assign( type->name, name );
	DaoNamespace_AddType( self->nspace, type->name, type );
	DaoNamespace_AddTypeConstant( self->nspace, type->name, type );

	GC_ShiftRC( type, block->value );
	block->value = (DaoValue*) type;
	return 1;
}
static int DaoByteCoder_DecodeTypeOf( DaoByteCoder *self, DaoByteBlock *block )
{
	uint_t A = DaoByteCoder_DecodeUInt16( block->begin );
	DaoByteBlock *valuebk = DaoByteCoder_LookupBlock( self, block, A );
	DaoType *type;

	type = DaoNamespace_GetType( self->nspace, valuebk->value );

	GC_ShiftRC( type, block->value );
	block->value = (DaoValue*) type;
	return 1;
}
static int DaoByteCoder_AddToScope( DaoByteCoder *self, DaoByteBlock *block, DString *name, DaoValue *value )
{
	int perm = block->end[7];
	if( block->parent == NULL ) return 0;

	if( block->parent == self->top ){
		DaoNamespace_AddConst( self->nspace, name, value, perm );
	}else if( block->parent->type == DAO_ASM_CLASS ){
		DaoClass *klass = DaoValue_CastClass( block->parent->value );
		DaoRoutine *rout = DaoValue_CastRoutine( value );
		if( rout && rout->routHost == klass->objType && (rout->attribs & DAO_ROUT_INITOR) ){
			DaoClass_AddConst( klass, klass->classRoutine->routName, value, perm );
		}else{
			DaoClass_AddConst( klass, name, value, perm );
		}
	}else if( block->parent->type == DAO_ASM_INTERFACE ){
		DaoInterface *inter = (DaoInterface*) block->parent->value;
		DaoRoutine *rout = DaoValue_CastRoutine( value );
		DaoMethods_Insert( inter->methods, rout, self->nspace, inter->abtype );
	}else{
		return 0;
	}
	return 1;
}
static int DaoByteCoder_DecodeEnum( DaoByteCoder *self, DaoByteBlock *block )
{
	uint_t A = DaoByteCoder_DecodeUInt16( block->begin );
	uint_t B = DaoByteCoder_DecodeUInt16( block->begin+2 );
	uint_t C = DaoByteCoder_DecodeUInt32( block->begin+4 );
	DaoByteBlock *pb, *namebk = DaoByteCoder_LookupBlock( self, block, A );
	DString *name = namebk->value->xString.data;
	DaoType *type = DaoNamespace_FindType( self->nspace, name );

	if( type ){
		GC_ShiftRC( type, block->value );
		block->value = (DaoValue*) type;
		return 1;
	}
	type = DaoType_New( name->mbs, DAO_ENUM, NULL, NULL );
	type->mapNames = DMap_New(D_STRING,0);
	type->flagtype = B;
	for(pb=block->first; pb; pb=pb->next){
		A = DaoByteCoder_DecodeUInt16( pb->begin+0 );
		B = DaoByteCoder_DecodeUInt32( pb->begin+2 );
		namebk = DaoByteCoder_LookupBlock( self, pb, A );
		DMap_Insert( type->mapNames, namebk->value->xString.data, IntToPointer(B) );
	}
	if( C ){
		A = DaoByteCoder_DecodeUInt16( block->end+0 );
		B = DaoByteCoder_DecodeUInt32( block->end+2 );
		namebk = DaoByteCoder_LookupBlock( self, block, A );
		DMap_Insert( type->mapNames, namebk->value->xString.data, IntToPointer(B) );
	}
	DaoType_CheckAttributes( type );
	DaoNamespace_AddType( self->nspace, type->name, type );
	GC_ShiftRC( type, block->value );
	block->value = (DaoValue*) type;
	return 1;
}
static int DaoByteCoder_DecodeBases( DaoByteCoder *self, DaoByteBlock *block )
{
	uint_t i, ids[4];

	DaoByteCoder_DecodeChunk2222( block->begin, ids+0, ids+1, ids+2, ids+3 );
	for(i=0; i<4; ++i){
		DaoByteBlock *pb = DaoByteCoder_LookupBlock( self, block, ids[i] );
		if( pb == NULL ) break;
		if( block->parent->type == DAO_ASM_CLASS ){
			DaoClass *klass = DaoValue_CastClass( block->parent->value );
			DaoClass *base = DaoValue_CastClass( pb->value );
			DaoClass_AddMixinClass( klass, base );
		}else if( block->parent->type == DAO_ASM_INTERFACE ){
			DaoInterface *inter = DaoValue_CastInterface( block->parent->value );
			DaoInterface *base = DaoValue_CastInterface( pb->value );
			DArray_Append( inter->supers, base );
		}
	}
	return 1;
}
static int DaoByteCoder_DecodeInterface( DaoByteCoder *self, DaoByteBlock *block )
{
	uint_t A = DaoByteCoder_DecodeUInt16( block->begin );
	uint_t B = DaoByteCoder_DecodeUInt16( block->begin+2 );
	DaoByteBlock *name = DaoByteCoder_LookupBlock( self, block, A );
	DaoByteBlock *pb = block->first;
	DaoInterface *inter = NULL;
	
	if( name->value->type == DAO_INTERFACE ){
		inter = (DaoInterface*) name->value;
	}else if( name->value->type == DAO_CLASS ){
		DaoClass *klass = (DaoClass*) name->value;
		DaoClass_MakeInterface( klass );
		inter = klass->inter;
	}else{
		inter = DaoInterface_New( self->nspace, name->value->xString.data->mbs );
		DaoByteCoder_AddToScope( self, block, inter->abtype->name, (DaoValue*) inter );
	}

	GC_ShiftRC( inter, block->value );
	block->value = (DaoValue*) inter;

	while( pb ){
		if( pb->type != DAO_ASM_BASES && inter->derived == 0 ){
			DaoInterface_DeriveMethods( inter );
		}
		DaoByteCoder_DecodeBlock( self, pb );
		pb = pb->next;
	}
	return 1;
}
static int DaoByteCoder_DecodeClass( DaoByteCoder *self, DaoByteBlock *block )
{
	uint_t A = DaoByteCoder_DecodeUInt16( block->begin );
	uint_t B = DaoByteCoder_DecodeUInt16( block->begin+2 );
	uint_t C = DaoByteCoder_DecodeUInt32( block->begin+4 );
	DaoByteBlock *name = DaoByteCoder_LookupBlock( self, block, A );
	DaoByteBlock *parent = DaoByteCoder_LookupBlock( self, block, B );
	DaoByteBlock *pb = block->first;
	DaoClass *klass = NULL;
	
	if( name->value->type == DAO_CLASS ){
		klass = (DaoClass*) name->value;
	}else{
		klass = DaoClass_New();
		DaoClass_SetName( klass, name->value->xString.data, self->nspace );
		DaoByteCoder_AddToScope( self, block, klass->className, (DaoValue*) klass );
	}

	GC_ShiftRC( klass, block->value );
	block->value = (DaoValue*) klass;

	klass->attribs = C;
	if( parent && parent->value ) DaoClass_AddSuperClass( klass, parent->value );

	while( pb ){
		if( pb->type != DAO_ASM_BASES && klass->derived == 0 ){
			DaoClass_DeriveClassData( klass );
			DaoClass_DeriveObjectData( klass );
		}
		DaoByteCoder_DecodeBlock( self, pb );
		pb = pb->next;
	}
	DaoClass_UpdateMixinConstructors( klass );
	return 1;
}
static int DaoByteCoder_DecodeRoutine( DaoByteCoder *self, DaoByteBlock *block )
{
	DaoRoutine *routine = NULL;
	DaoByteBlock *pb = block->first;
	DaoByteBlock *name, *type, *host;
	uint_t A, B, C, D;
	int i, count, add = 0;

	//DaoByteCoder_PrintBlock( self, block, 0 );

	DaoByteCoder_DecodeChunk2222( block->begin, & A, & B, & C, & D );
	if( block->end[6] ){  /* Default constructor; */
		host = DaoByteCoder_LookupBlock( self, block, C );
		routine = host->value->xType.aux->xClass.classRoutine;
		add = 1;
	}else if( A ){
		name = DaoByteCoder_LookupBlock( self, block, A );
		if( name->value->type == DAO_ROUTINE ){
			routine = (DaoRoutine*) name->value;
		}else{
			routine = DaoRoutine_New( self->nspace, NULL, 1);
			DString_Assign( routine->routName, name->value->xString.data );
			add = 1;
		}
	}else if( block == self->top ){
		routine = DaoRoutine_New( self->nspace, NULL, 1);
		self->nspace->mainRoutine = routine;
		DString_SetMBS( routine->routName, "__main__" );
		DArray_Append( self->nspace->mainRoutines, routine );
		DaoNamespace_SetConst( self->nspace, DVR_NSC_MAIN, (DaoValue*) routine );
	}
	if( routine == NULL ) return 0;
	GC_ShiftRC( routine, block->value );
	block->value = (DaoValue*) routine;
	routine->attribs = D;
	if( B ){
		type = DaoByteCoder_LookupBlock( self, block, B );
		GC_ShiftRC( type->value, routine->routType );
		routine->routType = (DaoType*) type->value;
		routine->parCount = routine->routType->nested->size;
		if( routine->routType->variadic ) routine->parCount = DAO_MAX_PARAM;
	}else{
		GC_ShiftRC( dao_type_routine, routine->routType );
		routine->routType = (DaoType*) dao_type_routine;
	}
	if( C ){
		host = DaoByteCoder_LookupBlock( self, block, C );
		GC_ShiftRC( host->value, routine->routHost );
		routine->routHost = (DaoType*) host->value;
	}
	if( add ) DaoByteCoder_AddToScope( self, block, routine->routName, (DaoValue*) routine );

	DaoByteCoder_DecodeChunk2222( block->end, & A, & B, & C, & D );
	routine->body->regCount = A;

	while( pb ){
		DaoByteCoder_DecodeBlock( self, pb );
		pb = pb->next;
	}
	return DaoRoutine_DoTypeInference( routine, 0 );
}
static int DaoByteCoder_DecodeRoutineConsts( DaoByteCoder *self, DaoByteBlock *block )
{
	DaoByteBlock *pb = block->first;
	DaoRoutine *routine = (DaoRoutine*) block->parent->value;
	int i, count = DaoByteCoder_DecodeUInt16( block->begin );
	int offset = self->iblocks->size;

	DaoByteBlock_GetAllBlocks( self, block, 3, count, 0 );
	//DaoByteCoder_PrintBlock( self, block, 0 );
	for(i=0; i<count; ++i){
		DaoByteBlock *block = (DaoByteBlock*) self->iblocks->items.pVoid[offset+i];
		DaoRoutine_AddConstant( routine, block ? block->value : NULL );
	}
	DArray_Erase( self->iblocks, offset, -1 );
	return 1;
}
static int DaoByteCoder_DecodeRoutineTypes( DaoByteCoder *self, DaoByteBlock *block )
{
	DaoByteBlock *pb = block->first;
	DaoRoutine *routine = (DaoRoutine*) block->parent->value;
	int i, max, count = DaoByteCoder_DecodeUInt16( block->begin );
	int offset1 = self->indices->size;
	int offset2 = self->iblocks->size;
	//DaoByteCoder_PrintBlock( self, block, 0 );
	if( count ){
		DaoByteCoder_GetIntegers( self, self->indices, block->begin + 4, block->begin + 8, 4 );
		DaoByteCoder_GetBlocks( self, block, block->begin + 6, block->begin + 8, 4, 0 );
		i = DaoByteCoder_DecodeUInt16( block->begin + 6 );
	}
	while( pb ){
		DaoByteCoder_GetIntegers( self, self->indices, pb->begin, pb->begin + 8, 4 );
		DaoByteCoder_GetBlocks( self, block, pb->begin + 2, pb->begin + 8, 4, 0 );
		pb = pb->next;
	}
	max = (count - 1) % 2;
	if( max == 0 ) max = 2;
	DaoByteCoder_GetIntegers( self, self->indices, block->end, block->end + 2*(max+1), 4 );
	DaoByteCoder_GetBlocks( self, block, block->end + 2, block->end + 2 + 2*(max+1), 4, 0 );

	for(i=0; i<count; ++i){
		DaoByteBlock *block = (DaoByteBlock*) self->iblocks->items.pVoid[offset2+i];
		daoint idx = self->indices->items.pInt[offset1+i];
		MAP_Insert( routine->body->localVarType, idx, (DaoType*) block->value );
	}
	DArray_Erase( self->indices, offset1, -1 );
	DArray_Erase( self->iblocks, offset2, -1 );
	return 1;
}
static int DaoByteCoder_DecodeRoutineCode( DaoByteCoder *self, DaoByteBlock *block )
{
	DaoByteBlock *pb = block->first;
	DaoRoutine *routine = (DaoRoutine*) block->parent->value;
	int numcode = DaoByteCoder_DecodeUInt16( block->begin );
	int numlines = DaoByteCoder_DecodeUInt16( block->begin+2 );
	int defline = DaoByteCoder_DecodeUInt16( block->end );
	int offset1 = self->lines->size;
	int offset2 = self->indices->size;
	int i, k, m;
	if( numlines ){
		DaoByteCoder_GetIntegers( self, self->lines, block->begin + 4, block->begin + 8, 2 );
		numlines -= 1;
	}
	while( pb ){
		if( numlines > 0 ){
			DaoByteCoder_GetIntegers( self, self->lines, pb->begin, pb->begin + 8, 2 );
		}else{
			DaoByteCoder_GetIntegers( self, self->indices, pb->begin, pb->begin + 8, 2 );
		}
		numlines -= 2;
		pb = pb->next;
	}
	DaoByteCoder_GetIntegers( self, self->indices, block->end, block->end + 8, 2 );

	numlines = DaoByteCoder_DecodeUInt16( block->begin+2 );
	for(i=offset1+2; i<self->lines->size; i+=2){
		short diff = (short) self->lines->items.pInt[i];
		self->lines->items.pInt[i] = self->lines->items.pInt[i-2] + diff;
	}

	//DaoByteCoder_PrintBlock( self, block, 0 );
	numcode = self->indices->size - offset2;
	m = self->lines->size > offset1+1 ? self->lines->items.pInt[offset1+1] : 0;
	for(i=0, k=1; i<numcode; i+=4){
		DMap *lookupTable = NULL;
		daoint *idx = self->indices->items.pInt + offset2 + i;
		DaoVmCodeX vmc = {0,0,0,0,0,0,0,0,0};
		vmc.code = idx[0];
		vmc.a = idx[1];
		vmc.b = idx[2];
		vmc.c = idx[3];
		if( i/4 >= m && k < numlines ){
			m += self->lines->items.pInt[offset1+2*k+1];
			k += 1;
		}
		vmc.line = k <= numlines ? self->lines->items.pInt[offset1+2*(k-1)] : defline;
		switch( vmc.code ){
		case DVM_GETCK : case DVM_GETVK : case DVM_SETVK :
			break;
		case DVM_GETVO : case DVM_SETVO :
			break;
		case DVM_GETCG : case DVM_GETVG : case DVM_SETVG :
			lookupTable = self->nspace->lookupTable;
			break;
		}
		if( lookupTable != NULL ){
			DaoByteBlock *pb = DaoByteCoder_LookupBlock( self, block, vmc.b );
			DString *name = pb->value->xString.data;
			DNode *it = DMap_Find( lookupTable, name );
			if( it ) vmc.b = LOOKUP_ID( it->value.pInt );
		}
		DArray_Append( routine->body->annotCodes, & vmc );
		DVector_PushCode( routine->body->vmCodes, * (DaoVmCode*) & vmc );
	}
	DArray_Erase( self->lines, offset1, -1 );
	DArray_Erase( self->indices, offset2, -1 );
	return 1;
}
DaoProcess* DaoNamespace_ReserveFoldingOperands( DaoNamespace *self, int N );
static int DaoByteCoder_EvaluateValue( DaoByteCoder *self, DaoByteBlock *block )
{
	DaoProcess *process;
	DaoType *retype = NULL;
	DaoValue *value = NULL;
	DaoVmCode vmcode = {0,1,2,0};
	DaoByteBlock *pb = block->first;
	uint_t idx, offset = self->iblocks->size;
	int i, count;

	vmcode.code = DaoByteCoder_DecodeUInt16( block->begin );
	vmcode.b = DaoByteCoder_DecodeUInt16( block->begin + 2 );
	idx = DaoByteCoder_DecodeUInt16( block->begin + 4 );
	if( idx ){
		DaoByteBlock *bk = DaoByteCoder_LookupBlock( self, block, idx );
		retype = (DaoType*) bk->value;
	}

	while( pb ){
		DaoByteCoder_GetBlocks( self, block, pb->begin, pb->begin + 8, 2, 0 );
		pb = pb->next;
	}
	DaoByteCoder_GetBlocks( self, block, block->end, block->end + 8, 2, 1 );

	if( vmcode.code == DVM_GETCG ){
		DaoByteBlock *bk = (DaoByteBlock*) self->iblocks->items.pVoid[offset];
		DString *name = bk->value->xString.data;
		int id = DaoNamespace_FindConst( self->nspace, name );
		if( id >= 0 ) value = DaoNamespace_GetConst( self->nspace, id );
		goto Done;
	}

	count = self->iblocks->size - offset;
	process = DaoNamespace_ReserveFoldingOperands( self->nspace, count + 1 );
	if( vmcode.code == DVM_GETF ){
		DaoByteBlock *block = (DaoByteBlock*) self->iblocks->items.pVoid[offset+1];
		vmcode.b = DaoRoutine_AddConstant( process->activeRoutine, block->value );
	}
	for(i=0; i<count; ++i){
		DaoByteBlock *block = (DaoByteBlock*) self->iblocks->items.pVoid[offset+i];
		DaoValue_Copy( block->value, & process->activeValues[i+1] );
	}
	GC_ShiftRC( retype, process->activeTypes[0] );
	process->activeTypes[0] = retype;
	process->activeCode = & vmcode;
#if 0
	if( self->hostType ){
		GC_ShiftRC( self->hostType, proc->activeRoutine->routHost );
		proc->activeRoutine->routHost = self->hostType;
	}
#endif
	value = DaoProcess_MakeConst( process );
	DaoProcess_PrintException( process, self->nspace->vmSpace->errorStream, 1 );
#if 0
	GC_DecRC( proc->activeRoutine->routHost );
	proc->activeRoutine->routHost = NULL;
#endif

Done:
	DaoByteCoder_PrintBlock( self, block, 0 );
	//printf( "here: %p\n", value );
	DArray_Erase( self->iblocks, offset, -1 );
	GC_ShiftRC( value, block->value );
	block->value = value;
	return 1;
}
static int DaoByteCoder_DecodeDeclaration( DaoByteCoder *self, DaoByteBlock *block )
{
	uint A = DaoByteCoder_DecodeUInt16( block->begin + 0 );
	uint B = DaoByteCoder_DecodeUInt16( block->begin + 2 );
	uint C = DaoByteCoder_DecodeUInt16( block->begin + 4 );
	uint D = DaoByteCoder_DecodeUInt16( block->begin + 6 );
	DaoByteBlock *name0  = DaoByteCoder_LookupBlock( self, block, A );
	DaoByteBlock *value0 = DaoByteCoder_LookupBlock( self, block, B );
	DaoByteBlock *type0  = DaoByteCoder_LookupBlock( self, block, C );
	DaoValue *value = value0 ? value0->value : NULL;
	DaoType *type = type0 ? (DaoType*) type0->value : NULL;
	DString *name = name0 ? name0->value->xString.data : NULL;

	printf( "DaoByteCoder_DecodeDeclaration\n" );
	//DaoByteCoder_PrintBlock( self, block, 0 );
	if( block->parent == self->top ){
		switch( block->type ){
		case DAO_ASM_CONST   :
			DaoNamespace_AddConst( self->nspace, name, value, D );
			break;
		case DAO_ASM_GLOBAL  :
			DaoNamespace_AddVariable( self->nspace, name, value, type, D );
			break;
		}
	}else if( block->parent->type == DAO_ASM_ROUTINE ){
		DaoRoutine *routine = (DaoRoutine*) block->parent->value;
		if( block->type == DAO_ASM_STATIC ){
			DArray_Append( routine->body->svariables, DaoVariable_New(NULL,NULL) );
		}
	}else if( block->parent->type == DAO_ASM_CLASS ){
		DaoClass *klass = (DaoClass*) block->parent->value;
		switch( block->type ){
		case DAO_ASM_CONST   : DaoClass_AddConst( klass, name, value, D ); break;
		case DAO_ASM_VAR     : DaoClass_AddObjectVar( klass, name, value, type, D ); break;
		case DAO_ASM_STATIC  : DaoClass_AddGlobalVar( klass, name, value, type, D ); break;
		}
	}
	return 1;
}
static int DaoByteCoder_LoadModule( DaoByteCoder *self, DaoByteBlock *block )
{
	uint_t A = DaoByteCoder_DecodeUInt16( block->begin );
	uint_t B = DaoByteCoder_DecodeUInt16( block->begin+2 );
	DaoByteBlock *path = DaoByteCoder_LookupBlock( self, block, A );
	DaoByteBlock *mod = DaoByteCoder_LookupBlock( self, block, B );
	DaoNamespace *ns = NULL;
	DString *spath;

	if( path == NULL || path->value == NULL || path->value->type != DAO_STRING ) return 0;
	if( B && (mod == NULL || mod->value == NULL || mod->value->type != DAO_STRING) ) return 0;
	spath = DString_Copy( path->value->xString.data );
	if( (ns = DaoNamespace_FindNamespace(self->nspace, spath)) == NULL ){
		ns = DaoVmSpace_LoadModule( self->nspace->vmSpace, spath );
	}
	DString_Delete( spath );

	if( mod == NULL ){
		return DaoNamespace_AddParent( self->nspace, ns );
	//}else if( hostClass && self->isClassBody ){
	//	DaoClass_AddConst( hostClass, modname, (DaoValue*) ns, perm );
	}else{
		DaoNamespace_AddConst( self->nspace, mod->value->xString.data, (DaoValue*) ns, 0 );
	}
	return 1;
}
int DaoByteBlock_DecodeUseStmt( DaoByteCoder *self, DaoByteBlock *block )
{
	uint_t A = DaoByteCoder_DecodeUInt16( block->begin );
	uint_t B = DaoByteCoder_DecodeUInt16( block->begin+2 );
	DaoByteBlock *value = DaoByteCoder_LookupBlock( self, block, B );

	if( value->value == NULL ) return 0;
	if( value->value->type == DAO_NAMESPACE ){
		DaoNamespace_AddParent( self->nspace, (DaoNamespace*) value->value );
	}else if( value->value->type == DAO_ROUTINE ){
		if( block->parent->type == DAO_ASM_CLASS ){
			DaoClass *host = DaoValue_CastClass( block->parent->value );
			DRoutines_Add( host->classRoutines->overloads, (DaoRoutine*) value->value );
		}
	}
	return 1;
}
static int DaoByteCoder_DecodeBlock( DaoByteCoder *self, DaoByteBlock *block )
{
	int ret = 0;

	if( (block->type && block->type <= DAO_ASM_EVAL) || block->type == DAO_ASM_SEEK ){
		DArray_PushBack( self->stack, block );
	}
	switch( block->type ){
	case DAO_ASM_CONST     :
	case DAO_ASM_VAR       :
	case DAO_ASM_STATIC    :
	case DAO_ASM_GLOBAL    : ret = DaoByteCoder_DecodeDeclaration( self, block ); break;
	case DAO_ASM_VALUE     : ret = DaoByteCoder_DecodeValue( self, block ); break;
	case DAO_ASM_TYPE      : ret = DaoByteCoder_DecodeType( self, block ); break;
	case DAO_ASM_ENUM      : ret = DaoByteCoder_DecodeEnum( self, block ); break;
	case DAO_ASM_INTERFACE : ret = DaoByteCoder_DecodeInterface( self, block ); break;
	case DAO_ASM_CLASS     : ret = DaoByteCoder_DecodeClass( self, block ); break;
	case DAO_ASM_ROUTINE   : ret = DaoByteCoder_DecodeRoutine( self, block ); break;
	case DAO_ASM_EVAL      : ret = DaoByteCoder_EvaluateValue( self, block ); break;
	case DAO_ASM_LOAD      : ret = DaoByteCoder_LoadModule( self, block ); break;
	case DAO_ASM_CONSTS    : ret = DaoByteCoder_DecodeRoutineConsts( self, block ); break;
	case DAO_ASM_TYPES     : ret = DaoByteCoder_DecodeRoutineTypes( self, block ); break;
	case DAO_ASM_CODE      : ret = DaoByteCoder_DecodeRoutineCode( self, block ); break;
	case DAO_ASM_USE       : ret = DaoByteBlock_DecodeUseStmt( self, block ); break;
	case DAO_ASM_TYPEDEF   : ret = DaoByteCoder_DecodeTypeAlias( self, block ); break;
	case DAO_ASM_TYPEOF    : ret = DaoByteCoder_DecodeTypeOf( self, block ); break;
	case DAO_ASM_BASES     : ret = DaoByteCoder_DecodeBases( self, block ); break;
		break;
	}
	return ret;
}
int DaoByteCoder_Build( DaoByteCoder *self, DaoNamespace *nspace )
{
	if( self->top == NULL ) return 0;
	DaoByteCoder_SplitData( self, self->top );

	printf( "DaoByteCoder_Build\n" );

	self->stack->size = 0;
	self->ivalues->size = 0;
	self->iblocks->size = 0;
	self->indices->size = 0;
	self->nspace = nspace;
	DaoByteCoder_DecodeBlock( self, self->top );
	return 1;
}





static void DaoByteCoder_PrintSubBlocks( DaoByteCoder *self, DaoByteBlock *block, int spaces )
{
	DaoByteBlock *pb = block->first;
	while( pb ){
		if( pb != block->first && pb->type != DAO_ASM_END && pb->type != DAO_ASM_DATA && pb->type != DAO_ASM_DATA2 ) printf( "\n" );
		DaoByteCoder_PrintBlock( self, pb, spaces );
		pb = pb->next;
	}
}
static void DaoByteBlock_PrintTag( int tag, int spaces )
{
	while( spaces -- ) printf( " " );
	printf( "%s: ", dao_asm_names[tag] );
}
static void DaoByteBlock_PrintChars( uchar_t *chars, int count, int wcs )
{
	int i;
	if( wcs ){
		wchar_t ch1 = DaoByteCoder_DecodeUInt32( chars );
		wchar_t ch2 = DaoByteCoder_DecodeUInt32( chars + 4 );
		printf( "\"" );
		if( count > 0 ) printf( iswprint( ch1 ) ? "%lc" : "\\%i", ch1 );
		if( count > 1 ) printf( iswprint( ch2 ) ? "%lc" : "\\%i", ch2 );
		printf( "\";\n" );
		return;
	}
	printf( "\'" );
	for(i=0; i<count; ++i){
		int ch = chars[i];
		if( isprint(ch) ){
			printf( "%c", ch );
		}else{
			printf( "\\%i", ch );
		}
	}
	printf( "\';\n" );
}
void DaoByteCoder_PrintBlock( DaoByteCoder *self, DaoByteBlock *block, int spaces )
{
	int lines;
	uint_t i, A, B, C, D;
	DaoByteBlock *pb = block->first;

	DaoByteBlock_PrintTag( block->type, spaces );
	switch( block->type ){
	case DAO_ASM_ROUTINE :
	case DAO_ASM_CLASS :
	case DAO_ASM_INTERFACE :
		DaoByteCoder_DecodeChunk2222( block->begin, & A, & B, & C, & D );
		printf( "%i, %i, %i, %i;\n", A, B, C, D );
		DaoByteCoder_PrintSubBlocks( self, block, spaces + 4 );
		DaoByteBlock_PrintTag( DAO_ASM_END, spaces );
		printf( " ;\n" );
		break;
	case DAO_ASM_VALUE :
		A = DaoByteCoder_DecodeUInt16( block->begin );
		switch( A ){
		case DAO_INTEGER :
			printf( "DAO_INTEGER;\n" );
			DaoByteBlock_PrintTag( DAO_ASM_END, spaces );
			printf( " %" DAO_INT_FORMAT " ;\n", DaoByteCoder_DecodeDaoInt( self, block->end ) );
			break;
		case DAO_FLOAT :
			printf( "DAO_FLOAT;\n" );
			DaoByteBlock_PrintTag( DAO_ASM_END, spaces );
			printf( " %g ;\n", DaoByteCoder_DecodeDouble( self, block->end ) );
			break;
		case DAO_DOUBLE :
			printf( "DAO_DOUBLE;\n" );
			DaoByteBlock_PrintTag( DAO_ASM_END, spaces );
			printf( " %g ;\n", DaoByteCoder_DecodeDouble( self, block->end ) );
			break;
		case DAO_STRING :
			DaoByteCoder_DecodeSubChunk24( block->begin + 2, & B, & C );
			printf( "DAO_STRING, %i, %i;\n", B, C );
			D = B ? 2 : 8;
			while( pb ){
				DaoByteBlock_PrintTag( pb->type, spaces + 4 );
				DaoByteBlock_PrintChars( pb->begin, D, B );
				pb = pb->next;
			}
			DaoByteBlock_PrintTag( DAO_ASM_END, spaces );
			DaoByteBlock_PrintChars( block->end, (C%D) || C == 0 ? (C%D) : D, B );
			break;
		case DAO_ENUM :
			B = DaoByteCoder_DecodeUInt16( block->begin+2 );
			C = DaoByteCoder_DecodeUInt32( block->end );
			printf( "DAO_ENUM, %i;\n", B );
			DaoByteBlock_PrintTag( DAO_ASM_END, spaces );
			printf( " %i;\n", C );
			break;
		case DAO_ARRAY :
			C = DaoByteCoder_DecodeUInt32( block->begin+4 );
			printf( "DAO_ARRAY, %i %i %i;\n", block->begin[2], block->begin[3], C );
			DaoByteBlock_PrintTag( DAO_ASM_END, spaces );
			printf( " %i;\n", C );
			break;
		case DAO_LIST :
		case DAO_MAP :
			B = DaoByteCoder_DecodeUInt16( block->begin+2 );
			C = DaoByteCoder_DecodeUInt32( block->begin+4 );
			printf( "%s, %i, %i;\n", A == DAO_LIST ? "DAO_LIST" : "DAO_MAP", B, C );
			while( pb ){
				DaoByteBlock_PrintTag( pb->type, spaces + 4 );
				DaoByteCoder_DecodeChunk2222( pb->begin, & A, & B, & C, & D );
				printf( "%i, %i, %i, %i;\n", A, B, C, D );
				pb = pb->next;
			}
			DaoByteCoder_DecodeChunk2222( block->end, & A, & B, & C, & D );
			DaoByteBlock_PrintTag( DAO_ASM_END, spaces );
			printf( "%i, %i, %i, %i;\n", A, B, C, D );
			break;
		default :
			DaoByteCoder_DecodeChunk2222( block->begin, & A, & B, & C, & D );
			printf( "%i, %i, %i, %i;\n", A, B, C, D );
			DaoByteCoder_PrintSubBlocks( self, block, spaces + 4 );
			DaoByteBlock_PrintTag( DAO_ASM_END, spaces );
			printf( " ;\n" );
			break;
		}
		break;
	case DAO_ASM_ENUM :
		DaoByteCoder_DecodeChunk224( block->begin, & A, & B, & C );
		printf( "%i, %i, %i;\n", A, B, C );
		while( pb ){
			DaoByteCoder_DecodeSubChunk24( pb->begin, & A, & B );
			DaoByteBlock_PrintTag( pb->type, spaces + 4 );
			printf( "%i, %i;\n", A, B );
			pb = pb->next;
		}
		DaoByteCoder_DecodeSubChunk24( block->end, & A, & B );
		DaoByteBlock_PrintTag( DAO_ASM_END, spaces );
		printf( "%i, %i;\n", A, B );
		break;
	case DAO_ASM_EVAL :
		DaoByteCoder_DecodeChunk2222( block->begin, & A, & B, & C, & D );
		printf( "%s, %i, %i, %i;\n", DaoVmCode_GetOpcodeName( A ), B, C, D );
		while( pb ){
			DaoByteCoder_DecodeChunk2222( pb->begin, & A, & B, & C, & D );
			DaoByteBlock_PrintTag( pb->type, spaces + 4 );
			printf( "%i, %i, %i, %i;\n", A, B, C, D );
			pb = pb->next;
		}
		DaoByteCoder_DecodeChunk2222( block->end, & A, & B, & C, & D );
		DaoByteBlock_PrintTag( DAO_ASM_END, spaces );
		printf( "%i, %i, %i, %i;\n", A, B, C, D );
		break;
	case DAO_ASM_TYPE :
	case DAO_ASM_CONSTS :
	case DAO_ASM_TYPES :
		DaoByteCoder_DecodeChunk2222( block->begin, & A, & B, & C, & D );
		printf( "%5i, %5i, %5i, %5i;\n", A, B, C, D );
		while( pb ){
			DaoByteCoder_DecodeChunk2222( pb->begin, & A, & B, & C, & D );
			DaoByteBlock_PrintTag( pb->type, spaces + 4 );
			printf( "%5i, %5i, %5i, %5i;\n", A, B, C, D );
			pb = pb->next;
		}
		DaoByteCoder_DecodeChunk2222( block->end, & A, & B, & C, & D );
		DaoByteBlock_PrintTag( DAO_ASM_END, spaces );
		printf( "%5i, %5i, %5i, %5i;\n", A, B, C, D );
		break;
	case DAO_ASM_CODE :
		DaoByteCoder_DecodeChunk2222( block->begin, & A, & B, & C, & D );
		printf( "%5i, %5i, %5i, %5i;\n", A, B, C, D );
		lines = B - 1;
		while( pb ){
			DaoByteCoder_DecodeChunk2222( pb->begin, & A, & B, & C, & D );
			DaoByteBlock_PrintTag( pb->type, spaces + 4 );
			if( lines > 0 ){
				printf( "%5i, %5i, %5i, %5i;\n", (short)A, B, (short)C, D );
			}else{
				printf( "   %-11s, %5i, %5i, %5i;\n", DaoVmCode_GetOpcodeName( A ), B, C, D );
			}
			pb = pb->next;
			lines -= 2;
		}
		DaoByteCoder_DecodeChunk2222( block->end, & A, & B, & C, & D );
		DaoByteBlock_PrintTag( DAO_ASM_END, spaces );
		printf( "        %-11s, %5i, %5i, %5i;\n", DaoVmCode_GetOpcodeName( A ), B, C, D );
		break;
	case DAO_ASM_NONE :
	case DAO_ASM_END :
		printf( ";\n" );
		break;
	default:
		DaoByteCoder_DecodeChunk2222( block->begin, & A, & B, & C, & D );
		printf( "%5i, %5i, %5i, %5i;\n", A, B, C, D );
		if( block->type < DAO_ASM_END ){
			DaoByteBlock_PrintTag( DAO_ASM_END, spaces );
			printf( ";\n" );
		}
		break;
	}
}
void DaoByteCoder_Disassemble( DaoByteCoder *self )
{
	if( self->top == NULL ) return;
	DaoByteCoder_SplitData( self, self->top );
	DaoByteCoder_PrintBlock( self, self->top, 0 );
}

