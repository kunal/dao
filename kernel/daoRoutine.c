/*
// Dao Virtual Machine
// http://www.daovm.net
//
// Copyright (c) 2006-2014, Limin Fu
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

#include<string.h>
#include<assert.h>

#include"daoConst.h"
#include"daoRoutine.h"
#include"daoGC.h"
#include"daoClass.h"
#include"daoObject.h"
#include"daoStream.h"
#include"daoParser.h"
#include"daoProcess.h"
#include"daoVmspace.h"
#include"daoRegex.h"
#include"daoNumtype.h"
#include"daoNamespace.h"
#include"daoValue.h"

DMutex mutex_routines_update;
DMutex mutex_routine_specialize;
DMutex mutex_routine_specialize2;

DaoRoutine* DaoRoutine_New( DaoNamespace *nspace, DaoType *host, int body )
{
	DaoRoutine *self = (DaoRoutine*) dao_calloc( 1, sizeof(DaoRoutine) );
	DaoValue_Init( self, DAO_ROUTINE );
	self->trait |= DAO_VALUE_DELAYGC;
	self->subtype = body ? DAO_ROUTINE : DAO_CFUNCTION;
	self->routName = DString_New();
	self->routConsts = DaoList_New();
	self->nameSpace = nspace;
	self->routHost = host;
	GC_IncRC( self->nameSpace );
	GC_IncRC( self->routHost );
	GC_IncRC( self->routConsts );
	if( body ){
		self->body = DaoRoutineBody_New();
		GC_IncRC( self->body );
	}
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}
DaoRoutine* DaoRoutines_New( DaoNamespace *nspace, DaoType *host, DaoRoutine *init )
{
	DaoRoutine *self = DaoRoutine_New( nspace, host, 0 );
	self->subtype = DAO_ROUTINES;
	self->overloads = DRoutines_New();
	self->routType = DaoType_New( "routine", DAO_ROUTINE, (DaoValue*)self, NULL );
	self->routType->overloads = 1;
	GC_IncRC( self->routType );
	if( init == NULL ) return self;

	DString_Assign( self->routName, init->routName );
	if( self->nameSpace == NULL ){
		self->nameSpace = init->nameSpace;
		GC_IncRC( self->nameSpace );
	}
	if( init->overloads ){
		DArray *routs = init->overloads->routines;
		int i, n = routs->size;
		for(i=0; i<n; i++){
			DaoRoutine *routine = routs->items.pRoutine[i];
			if( routine->attribs & DAO_ROUT_PRIVATE ){
				if( routine->routHost && routine->routHost != host ) continue;
				if( routine->routHost == NULL && routine->nameSpace != nspace ) continue;
			}
			DRoutines_Add( self->overloads, routine );
		}
	}else{
		DRoutines_Add( self->overloads, init );
	}
	return self;
}
void DaoRoutine_CopyFields( DaoRoutine *self, DaoRoutine *from, int cst, int cbody, int stat )
{
	int i;
	self->subtype = from->subtype;
	self->attribs = from->attribs;
	self->parCount = from->parCount;
	self->defLine = from->defLine;
	self->pFunc = from->pFunc;
	GC_ShiftRC( from->routHost, self->routHost );
	GC_ShiftRC( from->routType, self->routType );
	GC_ShiftRC( from->nameSpace, self->nameSpace );
	self->routHost = from->routHost;
	self->routType = from->routType;
	self->nameSpace = from->nameSpace;
	DString_Assign( self->routName, from->routName );
	if( cst ){
		DaoList *list = DaoList_New();
		GC_ShiftRC( list, self->routConsts );
		self->routConsts = list;
		DArray_Assign( self->routConsts->value, from->routConsts->value );
	}else{
		GC_ShiftRC( from->routConsts, self->routConsts );
		self->routConsts = from->routConsts;
	}
	if( from->body ){
		DaoRoutineBody *body = from->body;
		if( cbody ) body = DaoRoutineBody_Copy( body, stat );
		GC_ShiftRC( body, self->body );
		self->body = body;
	}
}
DaoRoutine* DaoRoutine_Copy( DaoRoutine *self, int cst, int body, int stat )
{
	DaoRoutine *copy = DaoRoutine_New( self->nameSpace, self->routHost, 0 );
	DaoRoutine_CopyFields( copy, self, cst, body, stat );
	return copy;
}
void DaoRoutine_Delete( DaoRoutine *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	GC_DecRC( self->routHost );
	GC_DecRC( self->routType );
	GC_DecRC( self->routConsts );
	GC_DecRC( self->nameSpace );
	DString_Delete( self->routName );
	if( self->overloads ) DRoutines_Delete( self->overloads );
	if( self->specialized ) DRoutines_Delete( self->specialized );
	if( self->original ) GC_DecRC( self->original );
	if( self->body ) GC_DecRC( self->body );
	dao_free( self );
}
int DaoRoutine_IsWrapper( DaoRoutine *self )
{
	return self->pFunc != NULL;
}
int DaoRoutine_AddConstant( DaoRoutine *self, DaoValue *value )
{
	DArray *consts = self->routConsts->value;
	DArray_Append( consts, value );
	DaoValue_MarkConst( consts->items.pValue[consts->size-1] );
	return consts->size-1;
}
static int DaoRoutine_Check( DaoRoutine *self, DaoValue *obj, DaoValue *p[], int n, int code )
{
	DNode *node;
	DaoValue **dpar = p;
	DMap *defs = NULL;
	DMap *mapNames = self->routType->mapNames;
	DaoType *abtp, **parType = self->routType->nested->items.pType;
	int need_self = self->routType->attrib & DAO_TYPE_SELF;
	int selfChecked = 0, selfMatch = 0;
	int ndef = self->parCount;
	int npar = n;
	int j, ifrom, ito;
	int parpass[DAO_MAX_PARAM];

	/* func();
	 * obj.func();
	 * obj::func();
	 */
	if( code == DVM_MCALL && ! (self->routType->attrib & DAO_TYPE_SELF) ){
		npar --;
		dpar ++;
	}else if( obj && need_self && code != DVM_MCALL ){
		/* class DaoClass : CppClass{ cppmethod(); }
		 * use io;
		 * print(..);
		 */
		abtp = & parType[0]->aux->xType;
		selfMatch = DaoType_MatchValue2( abtp, obj, defs );
		if( selfMatch ){
			parpass[0] = selfMatch;
			selfChecked = 1;
		}
	}
	/*
	   if( strcmp( rout->routName->bytes, "expand" ) ==0 )
	   printf( "%i, %p, parlist = %s; npar = %i; ndef = %i, %i\n", i, rout, rout->routType->name->bytes, npar, ndef, selfChecked );
	 */
	if( (npar | ndef) ==0 ) return 1;
	if( npar > ndef ) return 0;
	defs = DMap_New(0,0);
	for( j=selfChecked; j<ndef; j++) parpass[j] = 0;
	for(ifrom=0; ifrom<npar; ifrom++){
		DaoValue *val = dpar[ifrom];
		ito = ifrom + selfChecked;
		if( ito < ndef && parType[ito]->tid == DAO_PAR_VALIST ){
			DaoType *vltype = (DaoType*) parType[ito]->aux;
			for(; ifrom<npar; ifrom++){
				DaoValue *val = dpar[ifrom];
				if( vltype && DaoType_MatchValue2( vltype, val, defs ) == 0 ) goto NotMatched;
				parpass[ifrom+selfChecked] = 1;
			}
			break;
		}
		if( ito >= ndef ) goto NotMatched;
		abtp = parType[ito];
		if( val->type == DAO_PAR_NAMED ){
			if( DString_EQ( val->xNameValue.name, abtp->fname ) == 0 ) goto NotMatched;
			val = val->xNameValue.value;
		}
		parpass[ito] = DaoType_MatchValue2( (DaoType*) abtp->aux, val, defs );
		/*
		   printf( "%i:  %i  %s\n", parpass[ito], abtp->tid, abtp->name->bytes );
		 */
		if( parpass[ito] == 0 ) goto NotMatched;
	}
	for(ito=0; ito<ndef; ito++){
		int m = parType[ito]->tid;
		if( m == DAO_PAR_VALIST ) break;
		if( parpass[ito] ) continue;
		if( m != DAO_PAR_DEFAULT ) goto NotMatched;
		parpass[ito] = 1;
	}
	DMap_Delete( defs );
	return 1;
NotMatched:
	DMap_Delete( defs );
	return 0;
}

DaoTypeBase routTyper=
{
	"routine", & baseCore, NULL, NULL, {0}, {0},
	(FuncPtrDel) DaoRoutine_Delete, NULL
};

DaoRoutineBody* DaoRoutineBody_New()
{
	DaoRoutineBody *self = (DaoRoutineBody*) dao_calloc( 1, sizeof( DaoRoutineBody ) );
	DaoValue_Init( self, DAO_ROUTBODY );
	self->trait |= DAO_VALUE_DELAYGC;
	self->source = NULL;
	self->vmCodes = DVector_New( sizeof(DaoVmCode) );
	self->regType = DArray_New( DAO_DATA_VALUE );
	self->svariables = DArray_New( DAO_DATA_VALUE );
	self->defLocals = DArray_New( DAO_DATA_TOKEN );
	self->annotCodes = DArray_New( DAO_DATA_VMCODE );
	self->localVarType = DMap_New(0,0);
	self->abstypes = DMap_New( DAO_DATA_STRING, DAO_DATA_VALUE );
	self->simpleVariables = DArray_New(0);
	self->codeStart = self->codeEnd = 0;
	self->aux = DMap_New(0,0);
	self->jitData = NULL;
	self->specialized = 0;
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}
void DaoRoutineBody_Delete( DaoRoutineBody *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	DVector_Delete( self->vmCodes );
	DArray_Delete( self->simpleVariables );
	DArray_Delete( self->regType );
	DArray_Delete( self->svariables );
	DArray_Delete( self->defLocals );
	DArray_Delete( self->annotCodes );
	DMap_Delete( self->localVarType );
	DMap_Delete( self->abstypes );
	if( self->decoTargets ) DArray_Delete( self->decoTargets );
	if( self->revised ) GC_DecRC( self->revised );
	if( self->aux ) DaoAux_Delete( self->aux );
	if( dao_jit.Free && self->jitData ) dao_jit.Free( self->jitData );
	dao_free( self );
}
void DaoRoutineBody_CopyFields( DaoRoutineBody *self, DaoRoutineBody *other, int copy_stat )
{
	int i;
	DMap_Delete( self->localVarType );
	DArray_Delete( self->annotCodes );
	self->source = other->source;
	self->annotCodes = DArray_Copy( other->annotCodes );
	self->localVarType = DMap_Copy( other->localVarType );
	if( self->decoTargets ){
		DArray_Delete( self->decoTargets );
		self->decoTargets = NULL;
	}
	if( other->decoTargets ) self->decoTargets = DArray_Copy( other->decoTargets );
	DVector_Assign( self->vmCodes, other->vmCodes );
	DArray_Assign( self->regType, other->regType );
	DArray_Assign( self->simpleVariables, other->simpleVariables );
	self->regCount = other->regCount;
	self->codeStart = other->codeStart;
	self->codeEnd = other->codeEnd;
	DArray_Clear( self->svariables );
	for(i=0; i<other->svariables->size; ++i){
		DaoVariable *var = other->svariables->items.pVar[i];
		if( copy_stat ) var = DaoVariable_New( var->value, var->dtype );
		DArray_Append( self->svariables, var );
	}
}
DaoRoutineBody* DaoRoutineBody_Copy( DaoRoutineBody *self, int copy_stat )
{
	DaoRoutineBody *copy = DaoRoutineBody_New();
	DaoRoutineBody_CopyFields( copy, self, copy_stat );
	return copy;
}

extern void DaoRoutine_JitCompile( DaoRoutine *self );

int DaoRoutine_SetVmCodes( DaoRoutine *self, DArray *vmCodes )
{
	int i, n;
	DaoRoutineBody *body = self->body;
	if( body == NULL ) return 0;
	if( vmCodes == NULL || vmCodes->type != DAO_DATA_VMCODE ) return 0;
	DArray_Swap( body->annotCodes, vmCodes );
	vmCodes = body->annotCodes;
	DVector_Resize( body->vmCodes, vmCodes->size );
	for(i=0,n=vmCodes->size; i<n; i++){
		body->vmCodes->data.codes[i] = *(DaoVmCode*) vmCodes->items.pVmc[i];
	}
	return DaoRoutine_DoTypeInference( self, 0 );
}
int DaoRoutine_SetVmCodes2( DaoRoutine *self, DVector *vmCodes )
{
	DVector_Assign( self->body->vmCodes, vmCodes );
	return DaoRoutine_DoTypeInference( self, 0 );
}

void DaoRoutine_SetSource( DaoRoutine *self, DArray *tokens, DaoNamespace *ns )
{
	DArray_Append( ns->sources, tokens );
	self->body->source = (DArray*) DArray_Back( ns->sources );
}


void DaoRoutine_MapTypes( DaoRoutine *self, DMap *deftypes )
{
	DaoType *tp;
	DNode *it;
	int i, n;
#if 0
	printf( "DaoRoutine_MapTypes() %s\n", self->routName->bytes );
	for(it=DMap_First(deftypes); it; it=DMap_Next(deftypes,it) ){
		printf( "%16p -> %p\n", it->key.pType, it->value.pType );
		printf( "%16s -> %s\n", it->key.pType->name->bytes, it->value.pType->name->bytes );
	}
#endif
	for(it=DMap_First(self->body->localVarType); it; it=DMap_Next(self->body->localVarType,it) ){
		tp = DaoType_DefineTypes( it->value.pType, self->nameSpace, deftypes );
		it->value.pType = tp;
	}
	for(i=0,n=self->body->svariables->size; i<n; ++i){
		DaoVariable *var = self->body->svariables->items.pVar[i];
		DaoType *type = DaoType_DefineTypes( var->dtype, self->nameSpace, deftypes );
		GC_ShiftRC( type, var->dtype );
		var->dtype = type;
	}
}
int DaoRoutine_Finalize( DaoRoutine *self, DaoType *host, DMap *deftypes )
{
	DaoType *tp = DaoType_DefineTypes( self->routType, self->nameSpace, deftypes );
	if( tp == NULL ) return 0;
	GC_ShiftRC( tp, self->routType );
	self->routType = tp;
	if( host ){
		GC_ShiftRC( host, self->routHost );
		self->routHost = host;
	}
	if( self->body == NULL ) return 1;
	DaoRoutine_MapTypes( self, deftypes );
	return 1;
	/*
	 DaoRoutine_PrintCode( self, self->nameSpace->vmSpace->stdioStream );
	 */
}


static const char *const sep1 = "==========================================\n";
static const char *const sep2 =
"-------------------------------------------------------------------------\n";

DAO_DLL void DaoRoutine_FormatCode( DaoRoutine *self, int i, DaoVmCodeX vmc, DString *output )
{
	char buffer1[10];
	char buffer2[200];
	const char *fmt = daoRoutineCodeFormat;
	const char *name;

	DString_Clear( output );
	name = DaoVmCode_GetOpcodeName( vmc.code );
	sprintf( buffer1, "%5i :  ", i);
	if( self->body->source ) DaoLexer_AnnotateCode( self->body->source, vmc, output, 24 );
	sprintf( buffer2, fmt, name, vmc.a, vmc.b, vmc.c, vmc.line, output->bytes );
	DString_SetChars( output, buffer1 );
	DString_AppendChars( output, buffer2 );
}
void DaoRoutine_PrintCode( DaoRoutine *self, DaoStream *stream )
{
	DaoVmCodeX **vmCodes;
	DString *annot;
	int j, n;

	DaoStream_WriteChars( stream, sep1 );
	DaoStream_WriteChars( stream, "routine " );
	DaoStream_WriteString( stream, self->routName );
	DaoStream_WriteChars( stream, "():\n" );
	DaoStream_WriteChars( stream, "type: " );
	DaoStream_WriteString( stream, self->routType->name );
	if( self->body ){
		DaoStream_WriteChars( stream, "\nNumber of register:\n" );
		DaoStream_WriteInt( stream, self->body->regCount );
	}
	DaoStream_WriteChars( stream, "\n" );
	if( self->body == NULL ) return;

	DaoStream_WriteChars( stream, sep1 );
	DaoStream_WriteChars( stream, "Virtual Machine Code:\n\n" );
	DaoStream_WriteChars( stream, daoRoutineCodeHeader );

	DaoStream_WriteChars( stream, sep2 );
	annot = DString_New();
	vmCodes = self->body->annotCodes->items.pVmc;
	for(j=0,n=self->body->annotCodes->size; j<n; j++){
		DaoVmCode vmc = self->body->vmCodes->data.codes[j];
		if( vmc.code == DVM_JITC ){
			DaoVmCodeX vmcx = *vmCodes[j];
			memcpy( &vmcx, &vmc, sizeof(DaoVmCode) );
			DaoRoutine_FormatCode( self, j, vmcx, annot );
			DaoStream_WriteString( stream, annot );
		}
		DaoRoutine_FormatCode( self, j, *vmCodes[j], annot );
		DaoStream_WriteString( stream, annot );
	}
	DaoStream_WriteChars( stream, sep2 );
	DString_Delete( annot );
}



static DParamNode* DParamNode_New()
{
	DParamNode *self = (DParamNode*) dao_calloc( 1, sizeof(DParamNode) );
	return self;
}
static void DParamNode_Delete( DParamNode *self )
{
	while( self->first ){
		DParamNode *node = self->first;
		self->first = node->next;
		DParamNode_Delete( node );
	}
	dao_free( self );
}


DRoutines* DRoutines_New()
{
	DRoutines *self = (DRoutines*) dao_calloc( 1, sizeof(DRoutines) );
	self->tree = NULL;
	self->mtree = NULL;
	self->routines = DArray_New(0);
	self->array = DArray_New( DAO_DATA_VALUE );
	self->array2 = DArray_New(0);
	return self;
}
void DRoutines_Delete( DRoutines *self )
{
	if( self->tree ) DParamNode_Delete( self->tree );
	if( self->mtree ) DParamNode_Delete( self->mtree );
	DArray_Delete( self->routines );
	DArray_Delete( self->array );
	DArray_Delete( self->array2 );
	dao_free( self );
}

static DParamNode* DParamNode_Add( DParamNode *self, DaoRoutine *routine, int pid )
{
	DaoType *partype;
	DParamNode *param, *it;
	if( pid >= (int)routine->routType->nested->size ){
		/* If a routine with the same parameter signature is found, return it: */
		for(it=self->first; it; it=it->next){
			/*
			// Code section methods may be overloaded with normal methods with
			// exactly the same parameter signatures (map::keys()).
			// But they differ in attributes.
			*/
			if( it->routine && it->routine->attribs == routine->attribs ) return it;
		}
		param = DParamNode_New();
		param->routine = routine;
		/* Add as a leaf. */
		if( self->last ){
			self->last->next = param;
			self->last = param;
		}else{
			self->first = self->last = param;
		}
		return param;
	}
	partype = routine->routType->nested->items.pType[pid];
	for(it=self->first; it; it=it->next){
		if( DaoType_MatchTo( partype, it->type2, NULL ) >= DAO_MT_EQ ){
			return DParamNode_Add( it, routine, pid + 1 );
		}
	}
	/* Add a new internal node: */
	param = DParamNode_New();
	param->type = routine->routType->nested->items.pType[pid];
	if( param->type->tid >= DAO_PAR_NAMED && param->type->tid <= DAO_PAR_VALIST ){
		param->type2 = param->type;
		param->type = (DaoType*) param->type->aux;
	}
	it = DParamNode_Add( param, routine, pid+1 );
	/*
	// Add the node to the tree after all its child nodes have been created, to ensure
	// a reader will always lookup in a valid tree in multi-threaded applications:
	*/
	if( self->last ){
		self->last->next = param;
		self->last = param;
	}else{
		self->first = self->last = param;
	}
	return it;
}
static void DParamNode_ExportRoutine( DParamNode *self, DArray *routines )
{
	DParamNode *it;
	if( self->routine ) DArray_PushFront( routines, self->routine );
	for(it=self->first; it; it=it->next) DParamNode_ExportRoutine( it, routines );
}
DaoRoutine* DRoutines_Add( DRoutines *self, DaoRoutine *routine )
{
	int i, n, bl = 0;
	DParamNode *param = NULL;
	DArray *routs;

	if( routine->routType == NULL ) return NULL;
	/* If the name is not set yet, set it: */
	self->attribs |= DString_FindChar( routine->routType->name, '@', 0 ) != DAO_NULLPOS;
	DMutex_Lock( & mutex_routines_update );
	if( routine->routType->attrib & DAO_TYPE_SELF ){
		if( self->mtree == NULL ) self->mtree = DParamNode_New();
		param = DParamNode_Add( self->mtree, routine, 0 );
	}else{
		if( self->tree == NULL ) self->tree = DParamNode_New();
		param = DParamNode_Add( self->tree, routine, 0 );
	}
	/*
	// Always replace the previous routine with the current one.
	*/
	param->routine = routine;

	/*
	// Runtime routine specialization based on parameter types may create
	// two specializations with identical parameter signature, so one of
	// the specialized routine will not be successully added to the tree.
	// To avoid memory leaking, the one not added to the tree should also
	// be appended to "array", so that it can be properly garbage collected.
	*/
	DArray_Append( self->array, routine );

	self->array2->size = 0;
	if( self->mtree ) DParamNode_ExportRoutine( self->mtree, self->array2 );
	if( self->tree ) DParamNode_ExportRoutine( self->tree, self->array2 );
	/* to ensure safety for readers: */
	routs = self->routines;
	self->routines = self->array2;
	self->array2 = routs;
	DMutex_Unlock( & mutex_routines_update );
	return param->routine;
}
void DaoRoutines_Import( DaoRoutine *self, DRoutines *other )
{
	DaoType *host = self->routHost;
	DaoNamespace *nspace = self->nameSpace;
	int i, n = other->routines->size;
	if( self->overloads == NULL ) return;
	for(i=0; i<n; i++){
		DaoRoutine *routine = other->routines->items.pRoutine[i];
		if( routine->attribs & DAO_ROUT_PRIVATE ){
			if( routine->routHost && routine->routHost != host ) continue;
			if( routine->routHost == NULL && routine->nameSpace != nspace ) continue;
		}
		DRoutines_Add( self->overloads, routine );
	}
}
static DaoRoutine* DParamNode_GetLeaf( DParamNode *self, int *ms, int mode )
{
	int b1 = (mode & DAO_CALL_BLOCK) != 0;
	DParamNode *param;
	DaoRoutine *rout;
	DNode *it;

	*ms = 0;
	if( self->routine ){
		int b2 = (self->routine->attribs & DAO_ROUT_CODESECT) != 0;
		if( b1 == b2 ) return self->routine; /* a leaf */
		return NULL;
	}
	for(param=self->first; param; param=param->next){
		if( param->type == NULL ){
			int b2 = (param->routine->attribs & DAO_ROUT_CODESECT) != 0;
			if( b1 == b2 ) return param->routine; /* a leaf */
			continue;
			return NULL;
		}
		if( param->type->tid == DAO_PAR_VALIST ){
			rout = DParamNode_GetLeaf( param, ms, mode );
			if( rout == NULL ) continue;
			*ms += 1;
			return rout;
		}
	}
	/* check for routines with default parameters: */
	for(param=self->first; param; param=param->next){
		if( param->type2 == NULL ) continue;
		if( param->type2->tid != DAO_PAR_DEFAULT && param->type2->tid != DAO_PAR_VALIST ) continue;
		rout = DParamNode_GetLeaf( param, ms, mode );
		if( rout == NULL ) continue;
		*ms += 1;
		return rout;
	}
	return NULL;
}
static DaoRoutine* DParamNode_Lookup( DParamNode *self, DaoValue *p[], int n, int mode, int strict, int *ms, DMap *defs, int clear )
{
	int i, m, k = 0, max = 0;
	DaoRoutine *rout = NULL;
	DaoRoutine *best = NULL;
	DaoValue *value = NULL;
	DParamNode *param;

	*ms = 1;
	if( n == 0 ) return DParamNode_GetLeaf( self, ms, mode );

	if( self->type2 && self->type2->tid == DAO_PAR_VALIST ){
		*ms = DAO_MT_EQ;
		for(i=0; i<n; ++i){
			m = DaoType_MatchValue( self->type, p[i], defs );
			if( m == 0 ) return NULL;
			if( m < *ms ) *ms = m;
		}
		return DParamNode_GetLeaf( self, & k, mode );
	}

	value = p[0];
	for(param=self->first; param; param=param->next){
		int tid = param->type2 ? param->type2->tid : 0;
		DaoType *type = param->type;
		if( type == NULL ) continue;
		if( value->type == DAO_PAR_NAMED && (tid == DAO_PAR_NAMED || tid == DAO_PAR_DEFAULT) ){
			if( DString_EQ( value->xNameValue.name, param->type2->fname ) == 0 ) continue;
		}
		if( defs && clear ) DMap_Reset( defs );
		m = DaoType_MatchValue( type, value, defs );
		if( m == 0 ) continue;
		if( strict && m < DAO_MT_ANY ) continue;
		rout = DParamNode_Lookup( param, p+1, n-1, mode, strict, & k, defs, 0 );
		if( rout == NULL ) continue;
		m += k;
		if( m > max ){
			best = rout;
			max = m;
		}
	}
	*ms = max;
	return best;
}
static DaoRoutine* DParamNode_LookupByType( DParamNode *self, DaoType *types[], int n, int mode, int strict, int *ms, DMap *defs, int clear )
{
	int i, m, k = 0, max = 0;
	DaoRoutine *rout = NULL;
	DaoRoutine *best = NULL;
	DaoType *partype = NULL;
	DParamNode *param;

	*ms = 1;
	if( n == 0 ) return DParamNode_GetLeaf( self, ms, mode );
	
	if( self->type2 && self->type2->tid == DAO_PAR_VALIST ){
		*ms = DAO_MT_EQ;
		for(i=0; i<n; ++i){
			m = DaoType_MatchTo( types[i], self->type, defs );
			if( m == 0 ) return NULL;
			if( m < *ms ) *ms = m;
		}
		return DParamNode_GetLeaf( self, & k, mode );
	}

	partype = types[0];
	for(param=self->first; param; param=param->next){
		int tid = param->type2 ? param->type2->tid : 0;
		DaoType *type = param->type;
		if( type == NULL ) continue;
		if( partype->tid == DAO_PAR_NAMED && (tid == DAO_PAR_NAMED || tid == DAO_PAR_DEFAULT) ){
			if( DString_EQ( partype->fname, param->type2->fname ) == 0 ) continue;
		}
		if( param->type2->attrib & DAO_TYPE_SELFNAMED ){
			/*
			// self parameter will be passed by reference, const self argument
			// should not be passed to non-const self parameter;
			*/
			if( partype->constant && param->type->constant == 0 ) continue;
		}
		if( defs && clear ) DMap_Reset( defs );
		m = DaoType_MatchTo( partype, type, defs );
		if( m == 0 ) continue;
		if( strict && m < DAO_MT_ANY ) continue;
		rout = DParamNode_LookupByType( param, types+1, n-1, mode, strict, & k, defs, 0 );
		if( rout == NULL ) continue;
		m += k;
		if( m > max ){
			best = rout;
			max = m;
		}
	}
	*ms = max;
	return best;
}
static DaoRoutine* DRoutines_Lookup2( DRoutines *self, DaoValue *obj, DaoValue *p[], int n, int code, int mode, int strict )
{
	int i, k, m, score = 0;
	int mcall = code == DVM_MCALL;
	DParamNode *param = NULL;
	DaoRoutine *rout = NULL;
	DMap *defs = NULL;
	if( self->attribs ) defs = DHash_New(0,0);
	if( obj && obj->type && mcall ==0 ){
		/*
		// class Klass {
		//     routine Meth1() { }
		//     routine Meth2() { Meth1() }
		// }
		*/
		if( self->mtree ){
			DaoRoutine *rout2 = NULL;
			for(param=self->mtree->first; param; param=param->next){
				if( param->type == NULL ) continue;
				if( defs ) DMap_Reset( defs );
				m = DaoType_MatchValue( param->type, obj, defs );
				if( strict && m < DAO_MT_ANY ) continue;
				if( m == 0 ) continue;
				rout2 = DParamNode_Lookup( param, p, n, mode, strict, & k, defs, 0 );
				if( rout2 == NULL ) continue;
				m += k;
				if( m > score ){
					rout = rout2;
					score = m;
				}
			}
			if( rout ) goto Finalize;
		}
	}
	if( mcall && self->mtree ){
		/*
		// object = Klass()
		// object.Meth1()
		*/
		rout = DParamNode_Lookup( self->mtree, p, n, mode, strict, & score, defs, 1 );
		if( rout ) goto Finalize;
	}
	if( mcall == 0 && obj == NULL && self->mtree ){
		/*
		// routine test(self: array<int>){}
		// routine test(self: array<int>, x: int){}
		// test([1, 2, 3]) 
		*/
		rout = DParamNode_Lookup( self->mtree, p, n, mode, strict, & score, defs, 1 );
		if( rout ) goto Finalize;
	}
	if( self->tree ){
		if( mcall ){
			/* obj.function(), where function() is not method of obj: */
			p += 1;
			n -= 1;
		}
		rout = DParamNode_Lookup( self->tree, p, n, mode, strict, & score, defs, 1 );
	}
Finalize:
	if( defs ) DMap_Delete( defs );
	return rout;
}
static DaoRoutine* DRoutines_LookupByType2( DRoutines *self, DaoType *selftype, DaoType *types[], int n, int code, int mode, int strict )
{
	int i, k, m, score = 0;
	int mcall = code == DVM_MCALL;
	DParamNode *param = NULL;
	DaoRoutine *rout = NULL;
	DMap *defs = NULL;
	if( self->attribs ) defs = DHash_New(0,0);
	if( selftype && mcall ==0 ){
		if( self->mtree ){
			DaoRoutine *rout2 = NULL;
			for(param=self->mtree->first; param; param=param->next){
				if( param->type == NULL ) continue;
				if( selftype->constant && param->type->constant == 0 ) continue;
				if( defs ) DMap_Reset( defs );
				m = DaoType_MatchTo( selftype, param->type, defs );
				if( strict && m < DAO_MT_ANY ) continue;
				if( m == 0 ) continue;
				rout2 = DParamNode_LookupByType( param, types, n, mode, strict, & k, defs, 0 );
				if( rout2 == NULL ) continue;
				m += k;
				if( m > score ){
					rout = rout2;
					score = m;
				}
			}
			if( rout ) goto Finalize;
		}
	}
	if( mcall && self->mtree ){
		rout = DParamNode_LookupByType( self->mtree, types, n, mode, strict, & score, defs, 1 );
		if( rout ) goto Finalize;
	}
	if( mcall == 0 && selftype == NULL && self->mtree ){
		rout = DParamNode_LookupByType( self->mtree, types, n, mode, strict, & score, defs, 1 );
		if( rout ) goto Finalize;
	}
	if( self->tree ){
		if( mcall ){
			types += 1;
			n -= 1;
		}
		rout = DParamNode_LookupByType( self->tree, types, n, mode, strict, & score, defs, 1 );
	}
Finalize:
	if( defs ) DMap_Delete( defs );
	return rout;
}
static DaoRoutine* DRoutines_Lookup( DRoutines *self, DaoValue *obj, DaoValue *p[], int n, int code, int mode )
{
	return DRoutines_Lookup2( self, obj, p, n, code, mode, 0 );
}
static DaoRoutine* DRoutines_LookupByType( DRoutines *self, DaoType *selftype, DaoType *types[], int n, int code, int mode )
{
	return DRoutines_LookupByType2( self, selftype, types, n, code, mode, 0 );
}
DaoRoutine* DaoRoutine_ResolveX( DaoRoutine *self, DaoValue *obj, DaoValue *p[], int n, int codemode )
{
	DaoRoutine *rout;
	int code = codemode & 0xffff;
	int mode = codemode >> 16;
	int mcall = code == DVM_MCALL;
	int b1, b2;

	if( self == NULL ) return NULL;
	if( self->overloads ){
		self = DRoutines_Lookup( self->overloads, obj, p, n, code, mode );
		if( self == NULL ) return NULL;
	}
	rout = self;
	if( rout->specialized ){
		/* strict checking for specialized routines: */
		DaoRoutine *rt = DRoutines_Lookup2( rout->specialized, obj, p, n, code, mode, 1 );
		/*
		// If the routine has a body, check if it has done specialization.
		// Only used specialized routine for thread safety to avoid the
		// situation where the routine is used for execution, but its body
		// is still undergoing specialization.
		*/
		if( rt && (rt->body == NULL || rt->body->specialized) ) rout = rt;
	}
	b1 = (mode & DAO_CALL_BLOCK) != 0;
	b2 = (rout->attribs & DAO_ROUT_CODESECT) != 0;
	if( b1 != b2 ) return NULL;
	return (DaoRoutine*) rout;
}
DaoRoutine* DaoRoutine_ResolveByTypeX( DaoRoutine *self, DaoType *st, DaoType *t[], int n, int codemode )
{
	int code = codemode & 0xffff;
	int mode = codemode >> 16;
	int b1, b2;
	if( self == NULL ) return NULL;
	if( self->overloads ){
		self = DRoutines_LookupByType( self->overloads, st, t, n, code, mode );
		if( self == NULL ) return NULL;
	}
	if( self->specialized ){
		/* strict checking for specialized routines: */
		DaoRoutine *rt = DRoutines_LookupByType2( self->specialized, st, t, n, code, mode, 1 );
		/*
		// no need to check for specialization,
		// because routines returned by this are not used for execution:
		*/
		if( rt ) return rt;
	}
	b1 = (mode & DAO_CALL_BLOCK) != 0;
	b2 = (self->attribs & DAO_ROUT_CODESECT) != 0;
	if( b1 != b2 ) return NULL;
	return self;
}
DaoRoutine* DaoRoutine_Resolve( DaoRoutine *self, DaoValue *o, DaoValue *p[], int n )
{
	DaoRoutine *rout = DaoRoutine_ResolveX( self, o, p, n, DVM_CALL );
	if( rout == (DaoRoutine*)self ){ /* parameters not yet checked: */
		if( DaoRoutine_Check( rout, o, p, n, DVM_CALL ) ==0 ) return NULL;
	}
	return rout;
}


