/*=========================================================================================
  This file is a part of a virtual machine for the Dao programming language.
  Copyright (C) 2006-2010, Fu Limin. Email: fu@daovm.net, limin.fu@yahoo.com

  This software is free software; you can redistribute it and/or modify it under the terms 
  of the GNU Lesser General Public License as published by the Free Software Foundation; 
  either version 2.1 of the License, or (at your option) any later version.

  This software is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
  See the GNU Lesser General Public License for more details.
=========================================================================================*/

#include"stdlib.h"
#include"stdio.h"
#include"string.h"
#include"ctype.h"
#include"assert.h"

#include"daoType.h"
#include"daoVmspace.h"
#include"daoNamespace.h"
#include"daoNumeric.h"
#include"daoStream.h"
#include"daoRoutine.h"
#include"daoObject.h"
#include"daoContext.h"
#include"daoProcess.h"
#include"daoGC.h"
#include"daoStdlib.h"
#include"daoClass.h"
#include"daoParser.h"
#include"daoMacro.h"
#include"daoRegex.h"

static void DNS_GetField( DValue *self0, DaoContext *ctx, DString *name )
{
  DaoNameSpace *self = self0->v.ns;
  DValue *value = NULL;
  DNode *node = NULL;
  if( ( node = MAP_Find( self->varIndex, name ) ) )
    value = self->varData->data + node->value.pInt;
  if( ( node = MAP_Find( self->cstIndex, name ) ) )
    value = self->cstData->data + node->value.pInt;
  
  if( value == NULL ){
    DaoContext_RaiseException( ctx, DAO_ERROR, "invalid field" );
    return;
  }
  DaoContext_PutValue( ctx, *value );
}
static void DNS_SetField( DValue *self0, DaoContext *ctx, DString *name, DValue value )
{
  DaoNameSpace *self = self0->v.ns;
  DaoNameSpace_SetData( self, name, value );
}
static void DNS_GetItem( DValue *self0, DaoContext *ctx, DValue pid )
{
}
static void DNS_SetItem( DValue *self0, DaoContext *ctx, DValue pid, DValue value )
{
}
static DaoTypeCore nsCore =
{
  0,
#ifdef DEV_HASH_LOOKUP
  NULL, NULL,
#endif
  NULL, NULL, NULL, 0, 0,
  DNS_GetField,
  DNS_SetField,
  DNS_GetItem,
  DNS_SetItem,
  DaoBase_Print,
  DaoBase_Copy, /* do not copy namespace */
};

#if 0
static void NS_AddConst( DaoNameSpace *self, const char *name, DValue data )
{
  DString *s = DString_New(1);
  DaoBase *dbase = (DaoBase*)data;
  dbase->subType |= DAO_DATA_CONST;
  DString_SetMBS( s, name );
  DaoNameSpace_AddConst( self, s, dbase );
  DString_Delete( s );
}
#endif
DaoNameSpace* DaoNameSpace_GetNameSpace( DaoNameSpace *self, const char *name )
{
  DValue value = { DAO_NAMESPACE, 0, 0, 0, {0} };
  DaoNameSpace *ns;
  DString *mbs = DString_New(1);
  DString_SetMBS( mbs, name );
  ns = DaoNameSpace_FindNameSpace( self, mbs );
  if( ns == NULL ){
    ns = DaoNameSpace_New( self->vmSpace );
    ns->parent = self;
    DaoNameSpace_SetName( ns, name );
    value.v.ns = ns;
    DaoNameSpace_AddConst( self, mbs, value );
    value.v.ns = self;
    GC_IncRC( self );
    DVarray_Append( ns->cstData, value ); /* for GC */
    DValue_MarkConst( & ns->cstData->data[ns->cstData->size-1] );
  }
  DString_Delete( mbs );
  return ns;
}
void DaoNameSpace_AddValue( DaoNameSpace *self, const char *s, DValue v, const char *t )
{
  DaoAbsType *abtp = NULL;
  DString *name = DString_New(1);
  if( t && strlen( t ) >0 ){
    DaoParser *parser = DaoParser_New();
    parser->nameSpace = self;
    parser->vmSpace = self->vmSpace;
    abtp = DaoParser_ParseTypeName( t, self, NULL, NULL ); /* XXX warn */
    DaoParser_Delete( parser );
  }
  /*
  printf( "%s %s\n", name, abtp->name->mbs );
  */
  DString_SetMBS( name, s );
  DaoNameSpace_AddVariable( self, name, v, abtp );
  DString_Delete( name );
}
void DaoNameSpace_AddData( DaoNameSpace *self, const char *s, DaoBase *d, const char *t )
{
  DValue v = daoNilValue;
  if( d ){
    v.t = d->type;
    v.v.p = d;
  }
  DaoNameSpace_AddValue( self, s, v, t );
}
DValue DaoNameSpace_FindData( DaoNameSpace *self, const char *name )
{
  DString *s = DString_New(1);
  DValue res;
  DString_SetMBS( s, name );
  res = DaoNameSpace_GetData( self, s );
  DString_Delete( s );
  return res;
}
#if 0
static void NS_AddConstString( DaoNameSpace *self0, const char *name, const char *cst )
{
  DaoNameSpace *self = (DaoNameSpace*)self0;
  DString *s = DString_New(1);
  DaoBase *dbase = (DaoBase*)DaoString_New();
  dbase->subType |= DAO_DATA_CONST;
  DString_SetMBS( STRV( dbase ), cst );
  DString_SetMBS( s, name );
  DaoNameSpace_AddConst( self, s, dbase );
  DString_Delete( s );
}
#endif
void DaoNameSpace_AddConstNumbers( DaoNameSpace *self0, DaoNumItem *items )
{
  DaoNameSpace *self = (DaoNameSpace*)self0;
  DString *s = DString_New(1);
  DValue value = daoNilValue;
  int i = 0;
  while( items[i].name != NULL ){
    value.t = items[i].type;
    if( value.t < DAO_INTEGER || value.t > DAO_DOUBLE ) continue;
    switch( value.t ){
    case DAO_INTEGER : value.v.i = (int) items[i].value; break;
    case DAO_FLOAT   : value.v.f = (float) items[i].value; break;
    case DAO_DOUBLE  : value.v.d = items[i].value; break;
    default: break;
    }
    DString_SetMBS( s, items[i].name );
    DaoNameSpace_AddConst( self, s, value );
    i ++;
  }
  DString_Delete( s );
}
void DaoNameSpace_AddConstValue( DaoNameSpace *self, const char *name, DValue value )
{
  DString *s = DString_New(1);
  DString_SetMBS( s, name );
  DaoNameSpace_AddConst( self, s, value );
  DString_Delete( s );
}
void DaoNameSpace_AddConstData( DaoNameSpace *self, const char *name, DaoBase *data )
{
  DString *s = DString_New(1);
  DValue value = { 0, 0, 0, 0, {0} };
  value.t = data->type;
  value.v.p = data;
  DString_SetMBS( s, name );
  DaoNameSpace_AddConst( self, s, value );
  DString_Delete( s );
}
#if 0
static DaoCData* NS_AddCPointer( DaoNameSpace *self0, const char *name, void *data, int size )
{
  DaoNameSpace *self = (DaoNameSpace*)self0;
  DString *s = DString_New(1);
  DaoCData *cptr = DaoCData_New();
  cptr->typer = & cptrTyper;
  cptr->data = data;
  cptr->size = cptr->bufsize = size;
  DString_SetMBS( s, name );
  DaoNameSpace_AddVariable( self, s, (DaoBase*) cptr, NULL );
  DString_Delete( s );
  return (DaoCData*) cptr;
}
static void NS_AlignData( DaoNameSpace *self0, DaoNameSpace *ns0 )
{
  DaoNameSpace *self = (DaoNameSpace*)self0;
  DaoNameSpace *ns = (DaoNameSpace*)ns0;
  DaoAbsType **vtype = ns->varType->items.pAbtp;
  DNode *node, *search;
  DString *mbs = DString_New(1);
  DaoBase *q;
  int from = 0;

  DString_SetMBS( mbs, "mpi" );
  node = MAP_Find( ns->cstIndex, mbs );
  from = node->value.pInt;

  node = DMap_First( ns->cstIndex );
  for( ; node !=NULL; node = DNode_Next( node ) ){
    DaoBase *p = DVarray_GetItem( ns->cstData, node->value.pInt );
    if( node->value.pInt <= from ) continue;
    search = MAP_Find( self->cstIndex, node->key.pString );
    q = search == NULL ? NULL : DVarray_GetItem( self->cstData, search->value.pInt );
    /* import only if the const do not exist, or is NULL */
    if( q == NULL ) DaoNameSpace_AddConst( self, node->key.pString, p );
  }
  node = DMap_First( ns->varIndex );
  for( ; node !=NULL; node = DNode_Next( node ) ){
    search = MAP_Find( self->varIndex, node->key.pString );
    q = search == NULL ? NULL : DVarray_GetItem( self->varData, search->value.pInt );
    /* import only if the variable do not exist, or is NULL */
    if( q == NULL ) /* adding a reference */
      DaoNameSpace_AddVariable( self, node->key.pString, 0, vtype[ node->value.pInt ] );
  }
}
#endif
static void DaoRoutine_GetSignature( DaoAbsType *rt, DString *sig )
{
  DaoAbsType *it;
  int i;
  DString_Clear( sig );
  DString_ToMBS( sig );
  for(i=((rt->attrib & DAO_ROUT_PARSELF)!=0); i<rt->nested->size; i++){
    it = rt->nested->items.pAbtp[i];
    if( sig->size ) DString_AppendChar( sig, ',' );
    if( it->tid == DAO_PAR_NAMED || it->tid == DAO_PAR_DEFAULT ){
      DString_Append( sig, it->X.abtype->name );
    }else{
      DString_Append( sig, it->name );
    }
  }
}
static int DaoPrepareNumber( DaoNameSpace *self, DaoTypeBase *t )
{
  int i, j, valCount;
  DaoTypeBase *typer = (DaoTypeBase*)t;
  DaoTypeCore *core;
  DaoNamedValue *nnum;
  DArray *allNumbers;
  DMap *hash;
  if( typer->priv == NULL ) return 0;
  if( typer->priv->values != NULL ) return 1;
  hash = DHash_New( D_STRING, 0 );
  for(i=0; i<DAO_MAX_CDATA_SUPER; i++){
    if( typer->supers[i] == NULL ) break;
    DaoPrepareNumber( self, typer->supers[i] );
  }
  valCount = 0;
  allNumbers = DArray_New(0);
  if( typer->numItems != NULL ){
    while( typer->numItems[ valCount ].name != NULL ) valCount ++;
  }

  for(i=0; i<valCount; i++){
    double value = typer->numItems[i].value;
    nnum = dao_malloc( sizeof(DaoNamedValue) );
    nnum->name = DString_New(1);
    nnum->value = daoNilValue;
    DString_SetMBS( nnum->name, typer->numItems[i].name );
    DMap_Insert( hash, nnum->name, nnum );
    DArray_Append( allNumbers, nnum );
    nnum->value.t = typer->numItems[i].type;
    switch( typer->numItems[i].type ){
    case DAO_INTEGER : nnum->value.v.i = (int) value; break;
    case DAO_FLOAT : nnum->value.v.f = (float) value; break;
    case DAO_DOUBLE : nnum->value.v.d = value; break;
    default : break;
    }
  }
  for(i=0; i<DAO_MAX_CDATA_SUPER; i++){
    if( typer->supers[i] == NULL ) break;
    core = typer->supers[i]->priv;
    for(j=0; j<core->valCount; j++){
      nnum = core->values[j];
      DArray_Append( allNumbers, nnum );
      DMap_Insert( hash, nnum->name, nnum );
    }
  }
  core = typer->priv;
  core->mapValues = hash;
  core->valCount = allNumbers->size;
  core->values = dao_malloc( (core->valCount+1) * sizeof(DaoNamedValue*) );
  memcpy( core->values, allNumbers->items.pVoid, core->valCount * sizeof(void*) );
  DArray_Delete( allNumbers );
  for( i=0; i<core->valCount-1; i++ ){
    int k = i;
    char *p = core->values[i]->name->mbs;
    for( j=i+1; j<core->valCount; j++ ){
      /* According to gprof, using strcmp() seems to be much fast than 
       * using DString_Compare(), but this is just a profiler effect. */
      if( strcmp( p, core->values[j]->name->mbs ) >0 ){
        k = j;
        p = core->values[j]->name->mbs;
      }
    }
    if( k != i ){
      DaoNamedValue *temp = core->values[i];
      core->values[i] = core->values[k];
      core->values[k] = temp;
    }
  }
#if 0
  for( i=0; i<core->methCount; i++ )
    printf( "method of %s: %s(), %s\n", typer->name, core->methods[i]->routName->mbs, core->methods[i]->routType->name->mbs );
#endif
  return 1;
}
static int DaoPrepareMethod( DaoNameSpace *self, DaoTypeBase *typer )
{
  DaoParser *parser;
  DaoFunction *cur;
  DaoTypeCore *core;
  DRoutine *drout;
  DArray *allMethods;
  DString *name1, *name2;
  DMap *hash;
  DMap *mapRout;
  DNode *node;
  int i, j, size;
  if( typer->priv->methods != NULL ) return 1;
  for(i=0; i<DAO_MAX_CDATA_SUPER; i++){
    if( typer->supers[i] == NULL ) break;
    DaoPrepareMethod( self, typer->supers[i] );
  }
  size = 0;
  allMethods = DArray_New(0);
  name1 = DString_New(1);
  name2 = DString_New(1);
  mapRout = DMap_New(D_STRING,0);
  parser = DaoParser_New();
  parser->vmSpace = self->vmSpace;
  parser->nameSpace = self;
  parser->hostCData = typer->priv->host;

  if( typer->funcItems != NULL ){
    while( typer->funcItems[ size ].proto != NULL ) size ++;
  }

  for( i=0; i<size; i++ ){
    cur = DaoNameSpace_ParsePrototype( self, typer->funcItems[i].proto, parser );
    if( cur == NULL ){
      printf( "  In function: %s::%s\n", typer->name, typer->funcItems[i].proto );
      continue;
    }
    cur->pFunc = typer->funcItems[i].fpter;
    cur->hostCData = typer->priv->host;
    if( self->vmSpace->safeTag ) cur->attribs |= DAO_ROUT_EXTFUNC;

    node = MAP_Find( mapRout, cur->routName );
    if( node !=NULL ){
      drout = (DRoutine*) allMethods->items.pBase[ node->value.pInt ];
      DRoutine_AddOverLoad( drout, (DRoutine*) cur );
    }else{
      GC_IncRC( cur ); /* there is an entry in the structure */
      MAP_Insert( mapRout, cur->routName, allMethods->size );
      DArray_Append( allMethods, cur );
    }
  }
  for(i=0; i<DAO_MAX_CDATA_SUPER; i++){
    if( typer->supers[i] == NULL ) break;
    core = typer->supers[i]->priv;
    for(j=0; j<core->methCount; j++){
      cur = core->methods[j];
      if( STRCMP( cur->routName, typer->supers[i]->name ) ==0 ) continue;
      node = MAP_Find( mapRout, cur->routName );
      if( node == NULL ){
        /*
        cur = DaoFunction_Copy( cur, 0 );
        cur->firstRoutine = (DRoutine*) cur;
        */
        GC_IncRC( cur );
        DArray_Append( allMethods, cur );
        MAP_Insert( mapRout, cur->routName, allMethods->size -1 );
      }else{
        DRoutine *drt = (DRoutine*) allMethods->items.pBase[ node->value.pInt ];
        DaoFunction *func = (DaoFunction*) drt->firstRoutine;
        DaoFunction **fs = (DaoFunction**) func->routOverLoad->items.pBase;
        int k, matched = 0;
        DaoRoutine_GetSignature( cur->routType, name1 );
        for(k=0; k<func->routOverLoad->size; k++){
          if( (func->routType->attrib & DAO_ROUT_PARSELF) 
            == (fs[k]->routType->attrib & DAO_ROUT_PARSELF) ){
            DaoRoutine_GetSignature( fs[k]->routType, name2 );
            if( DString_EQ( name1, name2 ) ){
              matched = 1;
              break;
            }
          }
        }
        if( matched ==0 ){
          int dist = cur->distance + 1;
          cur = DaoFunction_Copy( cur, 0 ); /* there is no entry in the structure */
          cur->hostCData = typer->priv->host;
          cur->distance = dist; /* XXX distance , also for class */
          if( func->hostCData != typer->priv->host ){
            /* there is only an entry from parent types,
               duplicate it before adding overloaed function: */
            drt = (DRoutine*) DaoFunction_Copy( func, 0 );
            drt->firstRoutine = drt;
            drt->distance = func->distance + 1;
            DRoutine_AddOverLoad( drt, drt );
            GC_IncRC( drt );
            GC_DecRC( func );
            allMethods->items.pBase[ node->value.pInt ] = (DaoBase*) drt;
            func = (DaoFunction*) drt;
            func->hostCData = typer->priv->host;
          }
          DRoutine_AddOverLoad( drt, (DRoutine*) cur );
        }
      }
    }
  }
  DMap_Delete( mapRout );
  core = typer->priv;
  core->methCount = allMethods->size;
  core->methods = dao_malloc( (core->methCount+1) * sizeof(DaoFunction*) );
  memcpy( core->methods, allMethods->items.pVoid, core->methCount * sizeof(void*) );

  DArray_Delete( allMethods );
  DaoParser_Delete( parser );

  hash = DHash_New( D_STRING, 0 );
  core->mapMethods = hash;
  assert( DAO_ROUT_MAIN < (1<<DVM_MOVE) );
  for( i=0; i<core->methCount; i++ ){
    cur = core->methods[i];
    MAP_Insert( hash, cur->routName, cur );
  }
  for(i=DVM_MOVE; i<=DVM_BITRIT; i++){
    DString_SetMBS( name1, daoBitBoolArithOpers[i-DVM_MOVE] );
    if( DMap_Find( hash, name1 ) == NULL ) continue;
    core->attribs |= DAO_OPER_OVERLOADED | (1<<i);
  }
  DString_Delete( name1 );
  DString_Delete( name2 );
  for( i=0; i<core->methCount-1; i++ ){
    int k = i;
    char *p = core->methods[i]->routName->mbs;
    for( j=i+1; j<core->methCount; j++ ){
      if( strcmp( p, core->methods[j]->routName->mbs ) >0 ){
        k = j;
        p = core->methods[j]->routName->mbs;
      }
    }
    if( k != i ){
      DaoFunction *temp = core->methods[i];
      core->methods[i] = core->methods[k];
      core->methods[k] = temp;
    }
  }
#if 0
  if( strcmp( typer->name, "QWidget" ) ==0 ){
  for( i=0; i<core->methCount; i++ )
    printf( "%2i method of %s: %s(), %s\n", core->methods[i]->routOverLoad->size, typer->name, core->methods[i]->routName->mbs, core->methods[i]->routType->name->mbs );
  }
#endif
  return 1;
}

int DaoNameSpace_TypeDefine( DaoNameSpace *self, const char *old, const char *type )
{
  DaoNameSpace *ns;
  DaoAbsType *tp, *tp2;
  DString *name = DString_New(1);
  int i;
  DString_SetMBS( name, old );
  tp = DaoNameSpace_FindAbsType( self, name );
  if( tp == NULL && (i=DString_FindMBS( name, "::", 0 )) != MAXSIZE ){
    DString_SetDataMBS( name, old, i );
    ns = DaoNameSpace_FindNameSpace( self, name );
    if( ns ){
      DString_SetMBS( name, old + i + 2 );
      tp = DaoNameSpace_FindAbsType( ns, name );
    }
  }
  /* printf( "ns = %p  tp = %p  name = %s\n", self, tp, type ); */
  DString_SetMBS( name, type );
  tp2 = DaoNameSpace_FindAbsType( self, name );
  DString_Delete( name );
  if( tp == NULL || tp2 != NULL ) return 0;
  tp = DaoAbsType_Copy( tp );
  DString_SetMBS( tp->name, type );
  DaoNameSpace_AddAbsType( self, tp->name, tp );
  return 1;
}
DaoCDataCore* DaoCDataCore_New();
extern void DaoTypeCData_SetMethods( DaoTypeBase *self );

static int DaoNameSpace_AddType2( DaoNameSpace *self, DaoTypeBase *typer )
{
  DaoAbsType *abstype;
  DaoCDataCore *plgCore;
  DaoCData *plugin;
  DString *s;
  DValue value = daoNilCData;

  if( typer->priv && typer->priv->methods ) return 1;
  plgCore = DaoCDataCore_New();
  plugin = DaoCData_New( typer, NULL );
  s = DString_New(1);

  value.v.cdata = plugin;
  DString_SetMBS( s, typer->name );
  /* Add it before preparing methods, since itself may appear in parameter lists: */
  DaoNameSpace_AddConst( self, s, value );
  abstype = DaoNameSpace_MakeAbsType( self, s->mbs, DAO_CDATA, value.v.p, NULL, 0 );
  DaoNameSpace_AddAbsType( self, s, abstype );
  DString_Delete( s );

  plgCore->host = plugin;
  plgCore->NewData = typer->New;
  plgCore->DelData = typer->Delete;
  typer->priv = (DaoTypeCore*)plgCore;
  DaoTypeCData_SetMethods( typer );
  return 1;
}
int DaoNameSpace_AddType( DaoNameSpace *self, DaoTypeBase *typer, int setup )
{
  DaoNameSpace_AddType2( self, typer );
  DArray_Append( self->ctypers, typer );
  if( DaoPrepareNumber( self, typer ) == 0 ) return 0;
  if( setup ) return DaoNameSpace_SetupType( self, typer );
  return 1;
}
int DaoNameSpace_SetupType( DaoNameSpace *self, DaoTypeBase *typer )
{
  DaoTypeCore *core;
  int j;
  if( DaoPrepareMethod( self, typer ) == 0 ) return 0;
  core = typer->priv;
  for(j=0; j<core->methCount; j++) DArray_Append( self->cmethods, core->methods[j] );
  return 1;
}
int DaoNameSpace_AddTypes( DaoNameSpace *self, DaoTypeBase *typers[], int setup )
{
  int i, e = 0;
  for(i=0; typers[i]; i++ ){
    DaoNameSpace_AddType2( self, typers[i] );
    DArray_Append( self->ctypers, typers[i] );
    e |= ( DaoPrepareNumber( self, typers[i] ) == 0 );
  }
  if( setup ) return DaoNameSpace_SetupTypes( self, typers );
  return e;
}
int DaoNameSpace_TypeDefines( DaoNameSpace *self, const char *alias[] )
{
  int rc = 1;
  if( alias ){
    int i = 0;
    while( alias[i] && alias[i+1] ){
      if( DaoNameSpace_TypeDefine( self, alias[i], alias[i+1] ) ==0 ) rc = 0;
      i += 2;
    }
  }
  return rc;
}
int DaoNameSpace_SetupTypes( DaoNameSpace *self, DaoTypeBase *typers[] )
{
  DaoTypeCore *core;
  int i, j, e = 0;
  for(i=0; typers[i]; i++ ){
    e |= ( DaoPrepareMethod( self, typers[i] ) == 0 );
    core = typers[i]->priv;
    for(j=0; j<core->methCount; j++) DArray_Append( self->cmethods, core->methods[j] );
  }
  return e == 0;
}
DaoFunction* DaoNameSpace_MakeFunction( DaoNameSpace *self, 
    const char *proto, DaoParser *parser )
{
  DaoFunction *func = DaoNameSpace_ParsePrototype( self, proto, parser );
  DValue val = daoNilValue;

  if( func == NULL ) return NULL;
  val = DaoNameSpace_GetData( self, func->routName );
  if( val.t == DAO_ROUTINE || val.t == DAO_FUNCTION ){
    DRoutine_AddOverLoad( (DRoutine*) val.v.p, (DRoutine*)func );
  }else{
    val = daoNilFunction;
    val.v.func = func;
    DaoNameSpace_AddConst( self, func->routName, val );
  }
  return func;
}
int DaoNameSpace_AddFunction( DaoNameSpace *self, DaoFuncPtr fptr, const char *proto )
{
  DaoFunction *func;
  func = DaoNameSpace_MakeFunction( self, proto, NULL );
  if( func == NULL ) return 0;
  func->pFunc = fptr;
  return 1;
}

int DaoNameSpace_AddFunctions( DaoNameSpace *self, DaoFuncItem *items )
{
  DaoParser *parser = DaoParser_New();
  DaoFunction *func;
  int i = 0;
  parser->vmSpace = self->vmSpace;
  parser->nameSpace = self;
  while( items[i].fpter != NULL ){
    func = DaoNameSpace_MakeFunction( self, items[i].proto, parser );
    if( func == NULL ) break;
    func->pFunc = (DaoFuncPtr)items[i].fpter;
    i ++;
  }
  DaoParser_Delete( parser );
  return (items[i].fpter == NULL);
}
int DaoNameSpace_Load( DaoNameSpace *self, const char *fname )
{
  DaoVmSpace *vms = self->vmSpace;
  DString *src;
  FILE *fin = fopen( fname, "r" );
  int ch;
  if( ! fin ){
    DaoStream_WriteMBS( vms->stdStream, "ERROR: can not open file \"" );
    DaoStream_WriteMBS( vms->stdStream, fname );
    DaoStream_WriteMBS( vms->stdStream, "\".\n" );
    return 0;
  }
  src = DString_New(1);
  while( ( ch=getc(fin) ) != EOF ) DString_AppendChar( src, ch );
  fclose( fin );
  ch = DaoVmProcess_Eval( self->vmSpace->mainProcess, self, src, 1 );
  DString_Delete( src );
  return ch;
}
int DaoNameSpace_PrepareType( DaoNameSpace *self, DaoTypeBase *typer )
{
  return DaoPrepareNumber( self, typer ) && DaoPrepareMethod( self, typer );
}

DaoTypeBase nsTyper=
{
  & nsCore,
  "NAMESPACE",
  NULL, NULL, {0}, NULL,
  (FuncPtrDel) DaoNameSpace_Delete
};

static complex16 comImag = { 0.0, 1.0 };

typedef struct DaoNamedPointer DaoNamedPointer;
struct DaoNamedPointer
{
  char *name;
  void *value;
};

static DaoNamedPointer daoNsConsts2[] = 
{
  { "std" ,         & libStandard },
  { "math" ,        & libMath },
  { "coroutine" ,   & libCoroutine },
  { "reflect" ,     & libReflect },
#ifdef DAO_WITH_NETWORK
  { "network" ,     & libNetwork },
#endif
  { "mpi" ,         & libMPI },
  { NULL, NULL }
};
static const char *const daoNsAlias[][2] =
{
  { "io", "stdio" },
  { "std","stdlib" }
};

DaoNameSpace* DaoNameSpace_New( DaoVmSpace *vms )
{
  int i = 0;
  DNode *node;
  DValue value = daoNilValue;
  DString *name = DString_New(1);
  DaoNameSpace *self = (DaoNameSpace*) dao_malloc( sizeof(DaoNameSpace) );
  DaoBase_Init( self, DAO_NAMESPACE );
  self->vmSpace = vms;
  self->parent = NULL;
  self->cstUser = 0;
  self->mainRoutine = NULL;
  self->cstData  = DVarray_New();
  self->varData  = DVarray_New();
  self->varType  = DArray_New(0);
  self->mainRoutines  = DArray_New(0); /* XXX GC delete */
  self->definedRoutines  = DArray_New(0);
  self->nsLoaded  = DArray_New(0);
  self->ctypers  = DArray_New(0);
  self->cmethods  = DArray_New(0);
  self->cstIndex = DHash_New(D_STRING,0);
  self->varIndex = DHash_New(D_STRING,0);
  self->cstStatic = DHash_New(D_STRING,0);
  self->varStatic = DHash_New(D_STRING,0);
  self->macros   = DHash_New(D_STRING,0);
  self->abstypes = DHash_New(D_STRING,0);
  self->time = 0;
  self->file = DString_New(1);
  self->path = DString_New(1);
  self->name = DString_New(1);
  self->source = DString_New(1);
  self->libHandle = NULL;
  self->udfType1 = DaoAbsType_New( "?", DAO_UDF, 0,0 );
  self->udfType2 = DaoAbsType_New( "?", DAO_UDF, 0,0 );
  GC_IncRC( self->udfType1 );
  GC_IncRC( self->udfType2 );

  DString_SetMBS( name, "null" ); 
  DaoNameSpace_AddConst( self, name, value );
  DVarray_Append( self->cstData, daoNilValue );

  value.t = DAO_STREAM;
  value.v.stream = vms->stdStream;
  DString_SetMBS( name, "io" ); 
  DaoNameSpace_AddConst( self, name, value );
  if( vms->thdMaster ){
    value.t = DAO_THDMASTER;
    value.v.p = (DaoBase*) vms->thdMaster;
    DString_SetMBS( name, "mtlib" ); 
    DaoNameSpace_AddConst( self, name, value );
  }
  value.t = DAO_CDATA;
  i = 0;
  while( daoNsConsts2[i].name ){
    DString_SetMBS( name, daoNsConsts2[i].name ); 
    value.v.p = daoNsConsts2[i].value;
    DaoNameSpace_AddConst( self, name, value );
    i ++;
  }
  /* alias: */
  for(i=0; i<2; i++){
    DString_SetMBS( name, daoNsAlias[i][0] ); 
    node = DMap_Find( self->cstIndex, name );
    if( node ){
      DString_SetMBS( name, daoNsAlias[i][1] ); 
      DMap_Insert( self->cstIndex, name, node->value.pVoid );
    }
  }

  DString_SetMBS( name, "exceptions" );
  value.t = DAO_LIST;
  value.v.list = DaoList_New();
  DaoNameSpace_AddVariable( self, name, value, NULL );

  self->vmpEvalConst = DaoVmProcess_New(vms);
  self->routEvalConst = DaoRoutine_New();
  self->routEvalConst->nameSpace = self;
  self->routEvalConst->routType = dao_routine;
  GC_IncRC( dao_routine );
  GC_IncRC( self );
  DaoVmProcess_PushRoutine( self->vmpEvalConst, self->routEvalConst );
  self->vmpEvalConst->topFrame->context->subType = DAO_CONSTEVAL;
  value.t = DAO_ROUTINE;
  value.v.routine = self->routEvalConst;
  DVarray_Append( self->cstData, value );
  value.t = DAO_VMPROCESS;
  value.v.vmp = self->vmpEvalConst;
  DVarray_Append( self->cstData, value );
  DString_Delete( name );
  self->cstUser = self->cstData->size;

  if( vms && vms->nsInternal ) DaoNameSpace_Import( self, vms->nsInternal, 0 );
  return self;
}
void DaoNameSpace_Delete( DaoNameSpace *self )
{
  DaoTypeCore *core;
  DaoNamedValue *val;
  DNode *it;
  int i, j;
  it = DMap_First( self->macros );
  for( ; it !=NULL; it = DMap_Next(self->macros, it) ) GC_DecRC( it->value.pBase );
  it = DMap_First( self->abstypes );
  for( ; it !=NULL; it = DMap_Next(self->abstypes, it) ) GC_DecRC( it->value.pBase );
  for(i=0; i<self->ctypers->size; i++){
    core = ((DaoTypeBase*)self->ctypers->items.pBase[i])->priv;
    for(j=0; j<core->valCount; j++){
      val = core->values[j];
      DString_Delete( val->name );
      dao_free( val );
    }
    dao_free( core );
  }
  
  GC_DecRC( self->udfType1 );
  GC_DecRC( self->udfType2 );
  GC_DecRC( self->vmpEvalConst );
  GC_DecRC( self->routEvalConst );
  DVarray_Delete( self->cstData );
  DVarray_Delete( self->varData );
  DArray_Delete( self->varType );
  /* no need for GC, because these namespaces are indirectly
   * referenced through functions. */
  DArray_Delete( self->nsLoaded );
  DArray_Delete( self->ctypers );
  DArray_Delete( self->cmethods );
  DArray_Delete( self->mainRoutines );
  DArray_Delete( self->definedRoutines );
  DMap_Delete( self->cstIndex );
  DMap_Delete( self->varIndex );
  DMap_Delete( self->cstStatic );
  DMap_Delete( self->varStatic );
  DMap_Delete( self->macros );
  DMap_Delete( self->abstypes );
  DString_Delete( self->file );
  DString_Delete( self->path );
  DString_Delete( self->name );
  DString_Delete( self->source );
  dao_free( self );
}
void DaoNameSpace_SetName( DaoNameSpace *self, const char *name )
{
  int i;
  DString_SetMBS( self->name, name );
  i = DString_RFindChar( self->name, '/', -1 );
  if( i != MAXSIZE ){
    DString_SetMBS( self->file, name + i + 1 );
    DString_SetDataMBS( self->path, name, i );
  }else{
    DString_Clear( self->file );
    DString_Clear( self->path );
  }
}
int  DaoNameSpace_FindConst( DaoNameSpace *self, DString *name )
{
  DNode *node = DMap_Find( self->cstIndex, name );
  if( node ) return (int)node->value.pInt;
  if( self->parent ){
    node = DMap_Find( self->parent->cstIndex, name );
    if( node ){
      DaoNameSpace_AddConst( self, name, self->parent->cstData->data[ (int)node->value.pInt ] );
      return self->cstData->size - 1;
    }
  }
  return -1;
}
void DaoNameSpace_AddConst( DaoNameSpace *self, DString *name, DValue value )
{
  DNode *node;

  node = MAP_Find( self->cstIndex, name );
  if( node ){
    int id = (int)node->value.pInt;
    DValue p = self->cstData->data[id];
    if( p.t == value.t && ( p.t ==DAO_ROUTINE || p.t ==DAO_FUNCTION ) ){
      DRoutine_AddOverLoad( (DRoutine*) p.v.routine, (DRoutine*) value.v.p );
    }else{
      DValue_SimpleMove( value, self->cstData->data + id );
      DValue_MarkConst( & self->cstData->data[id] );
    }
  }else{
    MAP_Insert( self->cstIndex, name, self->cstData->size ) ;
    DVarray_Append( self->cstData, daoNilValue );
    DValue_SimpleMove( value, & self->cstData->data[self->cstData->size-1] );
    DValue_MarkConst( & self->cstData->data[self->cstData->size-1] );
  }
}
void DaoNameSpace_SetConst( DaoNameSpace *self, int id, DValue value )
{
  DValue_SimpleMove( value, self->cstData->data + id );
  DValue_MarkConst( & self->cstData->data[id] );
}
DValue DaoNameSpace_GetConst( DaoNameSpace *self, int i )
{
  if( i <0 || i >= self->cstData->size ) return daoNilValue;
  return self->cstData->data[i];
}
int  DaoNameSpace_FindVariable( DaoNameSpace *self, DString *name )
{
  DNode *node = MAP_Find( self->varIndex, name );
  if( node ) return (int)node->value.pInt;
  if( self->parent ){
    node = DMap_Find( self->parent->varIndex, name );
    if( node ){
      int id = (int) node->value.pInt;
      DValue var = self->parent->cstData->data[id];
      DaoNameSpace_AddVariable( self, name, var, self->parent->varType->items.pAbtp[id] );
      return self->varData->size - 1;
    }
  }
  return -1;
}
void DaoNameSpace_AddVariable( DaoNameSpace *self, DString *name, DValue value, DaoAbsType *tp )
{
  DaoAbsType *abtp = DaoNameSpace_GetAbsTypeV( self, value );
  DNode    *node;

  if( tp && tp->tid <= DAO_DOUBLE ) value.t = tp->tid;
  if( abtp && tp && DaoAbsType_MatchTo( abtp, tp, 0 ) == 0 ){
    printf( "unmatched type: %s %s\n", abtp->name->mbs, tp->name->mbs );
    return; /*XXX*/
  }
  if( tp == NULL ) tp = abtp;

  node = MAP_Find( self->varIndex, name );
  if( node ){
    int id = (int)node->value.pInt;
    DaoAbsType *type = self->varType->items.pAbtp[ id ];
    if( type && tp && DaoAbsType_MatchTo( tp, type, 0 ) == 0 ){
      printf( "unmatched type2\n" );
      return;
    }
    DValue_Move( value, self->varData->data + id, type );
  }else{
    /*
    printf("%p; %s : %p ; %i\n", self, name->mbs, dbase, dbase->type );
     */
    MAP_Insert( self->varIndex, name, self->varData->size ) ;
    DVarray_Append( self->varData, value );
    DArray_Append( self->varType, (void*)tp );
    GC_IncRC( tp );
  }
  if( abtp->attrib & DAO_TYPE_EMPTY ){
    switch( value.t ){
    case DAO_LIST : value.v.list->unitype = tp; break;
    case DAO_MAP :  value.v.map->unitype = tp; break;
    case DAO_ARRAY : value.v.array->unitype = tp; break;
    case DAO_TUPLE : value.v.tuple->unitype = tp; break;
    default : break;
    }
  }
}
int DaoNameSpace_SetVariable( DaoNameSpace *self, int index, DValue value )
{
  DaoAbsType *type = self->varType->items.pAbtp[ index ];
  return DValue_Move( value, self->varData->data + index, type );
}
DValue DaoNameSpace_GetVariable( DaoNameSpace *self, int i )
{
  if( i <0 || i >= self->varData->size ) return daoNilValue;
  return self->varData->data[i];
}
void DaoNameSpace_SetData( DaoNameSpace *self, DString *name, DValue value )
{
  DNode *node = NULL;
  if( ( node = MAP_Find( self->varIndex, name ) ) ){
    DaoAbsType *type = self->varType->items.pAbtp[ node->value.pInt ];
    DValue_Move( value, self->varData->data + node->value.pInt, type ); /*XXX return*/
    return;
  }
  DaoNameSpace_AddVariable( self, name, value, NULL );
}
DValue DaoNameSpace_GetData( DaoNameSpace *self, DString *name )
{
  DNode *node = NULL;
  if( ( node = MAP_Find( self->varIndex, name ) ) )
    return self->varData->data[ node->value.pInt ];
  if( ( node = MAP_Find( self->cstIndex, name ) ) )
    return self->cstData->data[ node->value.pInt ];
  return daoNilValue;
}
DaoClass* DaoNameSpace_FindClass( DaoNameSpace *self, DString *name )
{
  DValue val;
  DNode *node = MAP_Find( self->cstIndex, name );
  if( ! node ) return NULL;
  val = self->cstData->data[ node->value.pInt ];
  if( val.t == DAO_CLASS ) return val.v.klass;
  return NULL;
}
DaoNameSpace* DaoNameSpace_FindNameSpace( DaoNameSpace *self, DString *name )
{
  DValue val;
  DNode *node = MAP_Find( self->cstIndex, name );
  if( ! node ) return NULL;
  val = self->cstData->data[ node->value.pInt ];
  if( val.t == DAO_NAMESPACE ) return (DaoNameSpace*) val.v.p;
  return NULL;
}
void DaoNameSpace_AddMacro( DaoNameSpace *self, DString *name, DaoMacro *macro )
{
  DNode *node = MAP_Find( self->macros, name );
  if( node == NULL ){
    GC_IncRC( macro );
    MAP_Insert( self->macros, name, macro );
  }else{
    DaoMacro *m2 = (DaoMacro*) node->value.pVoid;
    GC_IncRC( macro );
    DArray_Append( m2->macroList, macro );
  }
}
void DaoNameSpace_Import( DaoNameSpace *self, DaoNameSpace *ns, DArray *varImport )
{
  DArray *array = NULL;
  size_t *found = NULL;
  DaoAbsType **vtype = ns->varType->items.pAbtp;
  DArray *names = DArray_New(D_STRING);
  DNode *node, *search;
  int k;

  if( varImport && varImport->size > 0 ){
    array = DArray_New(0);
    DArray_Resize( array, varImport->size, (void*)1 );
    found = array->items.pSize;
    for( k=0; k<varImport->size; k++){
      DString *name = varImport->items.pString[k];
      int c1 = DaoNameSpace_FindConst( ns, name );
      int c2 = DaoNameSpace_FindVariable( ns, name );
      if( MAP_Find( ns->cstStatic, name ) ) continue;
      if( MAP_Find( ns->varStatic, name ) ) continue;
      node = MAP_Find( ns->macros, name );
      if( c1 >= 0 ){
        int c3 = DaoNameSpace_FindConst( self, name );
        DValue p1 = ns->cstData->data[ c1 ];
        if( c3 >= 0 ){
          DValue p3 = self->cstData->data[ c3 ];
          if( p1.t == p3.t && ( p1.t == DAO_ROUTINE || p1.t == DAO_FUNCTION ) ){
            DRoutine_AddOverLoad( (DRoutine*)p3.v.p, (DRoutine*)p1.v.p );
          }else{
            DaoNameSpace_AddConst( self, name, p1 );
          }
        }else{
          DaoNameSpace_AddConst( self, name, p1 );
        }
      }else if( c2 >= 0 ){
        DaoNameSpace_AddVariable( self, name, DaoNameSpace_GetVariable( ns, c1 ), vtype[c2] );
      }else if( node ){
        DaoNameSpace_AddMacro( self, name, (DaoMacro*) node->value.pVoid );
      }else{
        found[k] = 0;
      }
    }
  }else{
    DValue q;
    node = DMap_First( ns->cstIndex );
    for( ; node !=NULL; node = DMap_Next(ns->cstIndex, node ) ){
      DValue p = ns->cstData->data[ node->value.pInt ];
      if( MAP_Find( ns->cstStatic, node->key.pString ) ) continue;
      search = MAP_Find( self->cstIndex, node->key.pString );
      q = search == NULL ? daoNilValue : self->cstData->data[ search->value.pInt ];
      /* import only if the const do not exist, or is NULL */
      if( p.t == DAO_ROUTINE && (p.v.routine->attribs & DAO_ROUT_MAIN) ) continue;
      if( q.t == 0 ){
        DaoNameSpace_AddConst( self, node->key.pString, p );
      }else if( p.t == q.t && ( p.t == DAO_ROUTINE || p.t == DAO_FUNCTION ) ){
        DRoutine_AddOverLoad( (DRoutine*)q.v.p, (DRoutine*)p.v.p );
      }
    }
    node = DMap_First( ns->varIndex );
    for( ; node !=NULL; node = DMap_Next(ns->varIndex, node ) ){
      DValue p = ns->varData->data[ node->value.pInt ];
      if( MAP_Find( ns->cstStatic, node->key.pString ) ) continue;
      search = MAP_Find( self->varIndex, node->key.pString );
      q = search == NULL ? daoNilValue : self->varData->data[ search->value.pInt ];
      /* import only if the variable do not exist, or is NULL */
      if( q.t ==0 ){
        DaoNameSpace_AddVariable( self, node->key.pString, p, vtype[ node->value.pInt ] );
      }
    }
    node = DMap_First( ns->macros );
    for( ; node !=NULL; node = DMap_Next(ns->macros, node ) )
      DaoNameSpace_AddMacro( self, node->key.pString, (DaoMacro*) node->value.pVoid );
  }
  if( varImport ){
    DArray_Swap( names, varImport );
    for( k=0; k<names->size; k++)
      if( ! found[k] ) DArray_Append( varImport, names->items.pString[k] );
  }
  node = DMap_First( ns->abstypes );
  for( ; node !=NULL; node = DMap_Next(ns->abstypes, node ) )
    DaoNameSpace_AddAbsType( self, node->key.pString, node->value.pAbtp );

  DArray_Delete( names );
  if( array ) DArray_Delete( array );
}

DaoAbsType* DaoNameSpace_FindAbsType( DaoNameSpace *self, DString *name )
{
  DNode *node;
  if( DString_FindChar( name, '?', 0 ) != MAXSIZE
      || DString_FindChar( name, '@', 0 ) != MAXSIZE ) return NULL;
  node = MAP_Find( self->abstypes, name );
  if( node == NULL && self->parent ){
    node = DMap_Find( self->parent->abstypes, name );
    if( node ){
      MAP_Insert( self->abstypes, name, node->value.pAbtp );
      GC_IncRC( node->value.pAbtp );
    }
  }
  if( node ) return node->value.pAbtp;
  return NULL;
}
int DaoNameSpace_AddAbsType( DaoNameSpace *self, DString *name, DaoAbsType *tp )
{
  DNode *node = MAP_Find( self->abstypes, name );
  int id = DaoNameSpace_FindConst( self, name );
#if 0
  //XXX no need? if( DString_FindChar( name, '?', 0 ) != MAXSIZE ) return 0;
#endif
  if( node == NULL ){
    MAP_Insert( self->abstypes, name, tp );
    GC_IncRC( tp );
  }
  if( id <0 && (tp->tid == DAO_CLASS || tp->tid == DAO_CDATA) ){
    DValue val = daoNilValue;
    val.t = tp->tid;
    val.v.p = tp->X.extra;
    DaoNameSpace_AddConst( self, name, val );
  }
  /*
  node = DMap_First( self->abstypes );
  for(; node!=NULL; node=DNode_Next(node)){
    if( DString_Compare( node->key.pString, node->value.pAbtp->name ) != 0 )
    printf( ">>>>>>>>>>> %s %s\n", node->key.pString->mbs, 
        node->value.pAbtp->name->mbs );
  }
  */
  return 1;
}

static DaoAbsType *simpleTypes[ DAO_ARRAY ] = { 0, 0, 0, 0, 0, 0, 0 };

void* DValue_GetTypeID( DValue self )
{
  void *id = NULL;
  switch( self.t ){
  case DAO_INTEGER :
  case DAO_FLOAT :
  case DAO_DOUBLE :
  case DAO_COMPLEX :
  case DAO_STRING :
  case DAO_LONG :  id = simpleTypes[ self.t ]; break;
  case DAO_ARRAY : id = self.v.array->unitype; break;
  case DAO_LIST :  id = self.v.list->unitype; break;
  case DAO_MAP :   id = self.v.map->unitype; break;
  case DAO_PAIR :
  case DAO_PAR_NAMED : id = self.v.pair->unitype; break;
  case DAO_TUPLE :  id = self.v.tuple->unitype; break;
  case DAO_OBJECT : id = self.v.object->myClass->objType; break;
  case DAO_CLASS :  id = self.v.klass->clsType; break;
  case DAO_CDATA :  id = self.v.cdata->typer; break;
  case DAO_ROUTINE :
  case DAO_FUNCTION :  id = self.v.routine->routType; break;
  case DAO_VMPROCESS : id = self.v.vmp->abtype; break;
  default : id = DaoVmSpace_GetTyper( self.t ); break;
  }
  return id;
}

DaoAbsType* DaoNameSpace_GetAbsTypeV( DaoNameSpace *self, DValue val )
{
  DaoAbsType *abtp = NULL;
  switch( val.t ){
  case DAO_NIL :
  case DAO_INTEGER : case DAO_FLOAT : case DAO_DOUBLE :
  case DAO_COMPLEX : case DAO_STRING : case DAO_LONG :
    abtp = simpleTypes[ val.t ];
    if( abtp ) break;
    abtp = DaoAbsType_New( coreTypeNames[val.t], val.t, NULL, NULL );
    simpleTypes[ val.t ] = abtp;
    GC_IncRC( abtp );
    break;
  default:
    abtp = DaoNameSpace_GetAbsType( self, val.v.p );
    break;
  }
  return abtp;
}
DaoAbsType* DaoNameSpace_GetAbsType( DaoNameSpace *self, DaoBase *p )
{
  DNode *node;
  DArray *nested = NULL;
  DString *mbs;
  DRoutine *rout;
  DaoAbsType *abtp = NULL;
  DaoAbsType *itp = (DaoAbsType*) p;
  DaoTuple *tuple = (DaoTuple*) p;
  DaoPair *pair = (DaoPair*) p;
  DaoList *list = (DaoList*) p;
  DaoMap *map = (DaoMap*) p;
  DaoArray *array = (DaoArray*) p;
  DaoCData *cdata = (DaoCData*) p;
  DaoVmProcess *vmp = (DaoVmProcess*) p;
  DaoTypeBase *typer;
  int i, tid, zerosize = 0;

  if( p == NULL ) return NULL;
  if( p->type == DAO_ABSTYPE && itp->tid == DAO_ABSTYPE ) return itp;
  tid = p->type;

  switch( p->type ){
  case DAO_INTEGER : case DAO_FLOAT : case DAO_DOUBLE :
  case DAO_COMPLEX : case DAO_STRING : case DAO_LONG :
    abtp = simpleTypes[ p->type ]; break;
  case DAO_LIST :
    abtp = list->unitype; break;
  case DAO_MAP :
    abtp = map->unitype; break;
#ifdef DAO_WITH_NUMARRAY
  case DAO_ARRAY :
    abtp = array->unitype; break;
#endif
  case DAO_OBJECT :
    abtp = ((DaoObject*)p)->myClass->objType; break;
  case DAO_CLASS :
    abtp = ((DaoClass*)p)->clsType; break;
  case DAO_ROUTINE :
  case DAO_FUNCTION :
    rout = (DRoutine*) p;
    abtp = rout->routType;
    break;
  case DAO_PAIR :
  case DAO_PAR_NAMED :
    abtp = pair->unitype; break;
  case DAO_TUPLE :
    abtp = tuple->unitype; break;
  case DAO_VMPROCESS :
    abtp = vmp->abtype; break;
  default : break;
  }
  if( abtp ){
    abtp->typer = DaoBase_GetTyper( p );
    return abtp;
  }

  mbs = DString_New(1);
  if( p->type <= DAO_STREAM ){
    DString_SetMBS( mbs, coreTypeNames[p->type] );
    if( p->type == DAO_LIST ){
      nested = DArray_New(0);
      if( list->items->size ==0 ){
        DString_AppendMBS( mbs, "<?>" );
        DArray_Append( nested, self->udfType1 );
        zerosize = 1;
      }else{
        itp = DaoNameSpace_MakeAbsType( self, "any", DAO_ANY, 0,0,0 );
        DString_AppendMBS( mbs, "<any>" );
        DArray_Append( nested, itp );
      }  
    }else if( p->type == DAO_MAP ){
      nested = DArray_New(0);
      if( map->items->size ==0 ){
        DString_AppendMBS( mbs, "<?,?>" );
        DArray_Append( nested, self->udfType1 );
        DArray_Append( nested, self->udfType2 );
        zerosize = 1;
      }else{
        itp = DaoNameSpace_MakeAbsType( self, "any", DAO_ANY, 0,0,0 );
        DString_AppendMBS( mbs, "<any,any>" );
        DArray_Append( nested, itp );
        DArray_Append( nested, itp );
      }
#ifdef DAO_WITH_NUMARRAY
    }else if( p->type == DAO_ARRAY ){
      nested = DArray_New(0);
      if( array->size ==0 ){
        DString_AppendMBS( mbs, "<?>" );
        DArray_Append( nested, self->udfType1 );
        zerosize = 1;
      }else if( array->numType == DAO_INTEGER ){
        itp = DaoNameSpace_MakeAbsType( self, "int", DAO_INTEGER, 0,0,0 );
        DString_AppendMBS( mbs, "<int>" );
        DArray_Append( nested, itp );
      }else if( array->numType == DAO_FLOAT ){
        itp = DaoNameSpace_MakeAbsType( self, "float", DAO_FLOAT, 0,0,0 );
        DString_AppendMBS( mbs, "<float>" );
        DArray_Append( nested, itp );
      }else if( array->numType == DAO_DOUBLE ){
        itp = DaoNameSpace_MakeAbsType( self, "double", DAO_DOUBLE, 0,0,0 );
        DString_AppendMBS( mbs, "<double>" );
        DArray_Append( nested, itp );
      }else{
        itp = DaoNameSpace_MakeAbsType( self, "complex", DAO_COMPLEX, 0,0,0 );
        DString_AppendMBS( mbs, "<complex>" );
        DArray_Append( nested, itp );
      }
#endif
    }else if( p->type == DAO_PAIR ){
      DString_SetMBS( mbs, "pair<" );
      nested = DArray_New(0);
      DArray_Append( nested, DaoNameSpace_GetAbsTypeV( self, pair->first ) );
      DString_Append( mbs, nested->items.pAbtp[0]->name );
      DArray_Append( nested, DaoNameSpace_GetAbsTypeV( self, pair->second ) );
      DString_AppendMBS( mbs, "," );
      DString_Append( mbs, nested->items.pAbtp[1]->name );
      DString_AppendMBS( mbs, ">" );
    }else if( p->type == DAO_TUPLE ){
      DString_SetMBS( mbs, "tuple<" );
      nested = DArray_New(0);
      for(i=0; i<tuple->items->size; i++){
        itp = DaoNameSpace_GetAbsTypeV( self, tuple->items->data[i] );
        DArray_Append( nested, itp );
        DString_Append( mbs, itp->name );
        if( i+1 < tuple->items->size ) DString_AppendMBS( mbs, "," );
      }
      DString_AppendMBS( mbs, ">" );
    }
    node = MAP_Find( self->abstypes, mbs );
    if( node ){
      abtp = (DaoAbsType*) node->value.pBase;
    }else{
      abtp = DaoAbsType_New( mbs->mbs, tid, NULL, nested );
      if( p->type < DAO_ARRAY ){
        simpleTypes[ p->type ] = abtp;
        GC_IncRC( abtp );
      }
#if 1
      if( zerosize ) abtp->attrib |= DAO_TYPE_EMPTY;
      switch( p->type ){
      case DAO_LIST :
        if( ! ( abtp->attrib & DAO_TYPE_EMPTY ) ){
          list->unitype = abtp;
          GC_IncRC( abtp );
        }
        break;
      case DAO_MAP :
        if( ! ( abtp->attrib & DAO_TYPE_EMPTY ) ){
          map->unitype = abtp;
          GC_IncRC( abtp );
        }
        break;
#ifdef DAO_WITH_NUMARRAY
      case DAO_ARRAY :
        if( ! ( abtp->attrib & DAO_TYPE_EMPTY ) ){
          array->unitype = abtp;
          GC_IncRC( abtp );
        }
        break;
#endif
      case DAO_PAIR :
        GC_IncRC( abtp );
        pair->unitype = abtp;
        break;
      case DAO_TUPLE :
        GC_IncRC( abtp );
        tuple->unitype = abtp;
        break;
      default : break;
      }
#endif
      /* XXX if( DString_FindChar( abtp->name, '?', 0 ) == MAXSIZE ) */
      DaoNameSpace_AddAbsType( self, abtp->name, abtp );
    }
  }else if( p->type == DAO_ROUTINE || p->type == DAO_FUNCTION ){
    DRoutine *rout = (DRoutine*) p;
    abtp = rout->routType; /* might be NULL */
  }else if( p->type == DAO_CDATA ){
    DString_AppendMBS( mbs, cdata->typer->name );
    node = MAP_Find( self->abstypes, mbs );
    if( node ){
      abtp = (DaoAbsType*) node->value.pBase;
    }else{
      abtp = DaoAbsType_New( mbs->mbs, p->type, p, NULL );
      DaoNameSpace_AddAbsType( self, abtp->name, abtp );
    }
    abtp->typer = cdata->typer;
  }else if( p->type == DAO_ABSTYPE ){
    DString_SetMBS( mbs, "type<" );
    nested = DArray_New(0);
    DArray_Append( nested, itp );
    DString_Append( mbs, itp->name );
    DString_AppendMBS( mbs, ">" );
    node = MAP_Find( self->abstypes, mbs );
    if( node ){
      abtp = (DaoAbsType*) node->value.pBase;
    }else{
      abtp = DaoAbsType_New( mbs->mbs, p->type, NULL, nested );
      DaoNameSpace_AddAbsType( self, abtp->name, abtp );
    }
  }else{
    typer = DaoBase_GetTyper( p );
    DString_SetMBS( mbs, typer->name );
    node = MAP_Find( self->abstypes, mbs );
    if( node ){
      abtp = (DaoAbsType*) node->value.pBase;
    }else{
      abtp = DaoAbsType_New( typer->name, p->type, NULL, NULL );
      abtp->typer = typer;
      DaoNameSpace_AddAbsType( self, abtp->name, abtp );
    }
  }
  /* abtp might be rout->routType, which might be NULL,
   * in case rout is DaoNameSpace.routEvalConst */
  if( abtp && abtp->typer ==NULL ) abtp->typer = DaoBase_GetTyper( p );
  DString_Delete( mbs );
  if( nested ) DArray_Delete( nested );
  return abtp;
}
DaoAbsType* DaoNameSpace_MakeAbsType( DaoNameSpace *self, const char *name, 
    uchar_t tid, DaoBase *pb, DaoAbsType *nest[], int N )
{
  DaoClass   *klass;
  DaoAbsType *any = NULL;
  DaoAbsType *tp;
  DNode      *node;
  DString    *mbs = DString_New(1);
  DArray     *nstd = DArray_New(0);
  int i;

  if( tid != DAO_ANY )
    any = DaoNameSpace_MakeAbsType( self, "any", DAO_ANY, 0,0,0 );

  DString_SetMBS( mbs, name );
  if( N > 0 ){
    DString_AppendChar( mbs, tid == DAO_PAR_GROUP ? '(' : '<' );
    DString_Append( mbs, nest[0]->name );
    DArray_Append( nstd, nest[0] );
    for(i=1; i<N; i++){
      DString_AppendChar( mbs, ',' );
      DString_Append( mbs, nest[i]->name );
      DArray_Append( nstd, nest[i] );
    }
    DString_AppendChar( mbs, tid == DAO_PAR_GROUP ? ')' : '>' );
  }else if( tid == DAO_LIST || tid == DAO_ARRAY ){
    DString_AppendMBS( mbs, "<any>" );
    DArray_Append( nstd, any );
  }else if( tid == DAO_MAP ){
    DString_AppendMBS( mbs, "<any,any>" );
    DArray_Append( nstd, any );
    DArray_Append( nstd, any );
  }else if( tid == DAO_CLASS ){
    /* do not save the abstract type for class and object in namespace,
     * because the class may be nested in another class, and different
     * class may nest different class with the same name, eg:
     * Error::Field::NotExist and Error::Key::NotExist
     * */
    klass = (DaoClass*) pb;
    tp = klass->clsType;
    goto Finalizing;
  }else if( tid == DAO_OBJECT ){
    klass = (DaoClass*) pb;
    tp = klass->objType;
    goto Finalizing;
  }else if( tid == DAO_PAR_NAMED ){
    DString_AppendMBS( mbs, ":" );
    if( pb->type == DAO_ABSTYPE ) DString_Append( mbs, ((DaoAbsType*)pb)->name );
  }
  node = MAP_Find( self->abstypes, mbs );
  if( node == NULL ){
    tp = DaoAbsType_New( mbs->mbs, tid, pb, nstd );
    if( pb && pb->type == DAO_CDATA ) tp->typer = ((DaoCData*)pb)->typer;
    if( tid == DAO_PAR_NAMED ){
      tp->X.extra = pb;
      tp->fname = DString_New(1);
      DString_SetMBS( tp->fname, name );
    }else if( tid == DAO_ROUTINE ){
      tp->X.extra = pb;
    }
    DaoNameSpace_AddAbsType( self, tp->name, tp );
  }else{
    tp = node->value.pAbtp;
  }
Finalizing:
  DString_Delete( mbs );
  DArray_Delete( nstd );
  return tp;
}
DaoAbsType* DaoNameSpace_MakeRoutType( DaoNameSpace *self, DaoAbsType *routype,
    DValue *vals, DaoAbsType *types[], DaoAbsType *retp )
{
  DaoAbsType *tp, *tp2, *abtp;
  DString *fname = NULL;
  DNode *node;
  int i, ip, ch = 0;

  abtp = DaoAbsType_New( "", DAO_ROUTINE, NULL, NULL );
  if( routype->mapNames ){
    if( abtp->mapNames ) DMap_Delete( abtp->mapNames );
    abtp->mapNames = DMap_Copy( routype->mapNames );
  }
  
  DString_AppendMBS( abtp->name, "routine<" );
  for(i=0, ip=0; i<routype->nested->size; i++, ip++){
    if( i >0 ) DString_AppendMBS( abtp->name, "," );
    tp = tp2 = routype->nested->items.pAbtp[i];
    if( tp && (tp->tid == DAO_PAR_NAMED || tp->tid == DAO_PAR_DEFAULT) ){
      ch = tp->name->mbs[tp->fname->size];
      tp2 = tp->X.abtype;
    }
    if( tp2 && tp2->tid ==DAO_UDF ){
      if( vals ){
        tp2 = DaoNameSpace_GetAbsTypeV( self, vals[ip] );
      }else if( types && types[i] ){
        tp2 = types[i];
      }
    }
    /* XXX typing DString_AppendMBS( abtp->name, tp ? tp->name->mbs : "..." ); */
    if( tp2 != tp && tp2 != tp->X.abtype ){
      fname = tp->fname;
      tp = DaoAbsType_New( fname->mbs, tp->tid, (DaoBase*) tp2, NULL );
      DString_AppendChar( tp->name, ch );
      DString_Append( tp->name, tp2->name );
      tp->fname = DString_Copy( fname );
    }
    DString_Append( abtp->name, tp->name );
    DArray_Append( abtp->nested, tp );
    if( tp->tid == DAO_PAR_GROUP ) ip += tp->nested->size-1;
  }
  tp = retp ? retp : routype->X.abtype;
  if( tp ){
    DString_AppendMBS( abtp->name, "=>" );
    DString_Append( abtp->name, tp->name );
  }
  DString_AppendMBS( abtp->name, ">" );
  abtp->X.abtype = tp;
  GC_IncRC( abtp->X.abtype );
  GC_IncRCs( abtp->nested );
  node = MAP_Find( self->abstypes, abtp->name );
  if( node ){
    DaoAbsType_Delete( abtp );
    GC_IncRC( node->value.pAbtp );
    return node->value.pAbtp;
  }
  GC_IncRC( abtp );
  DaoAbsType_CheckName( abtp );
  DaoNameSpace_AddAbsType( self, abtp->name, abtp );
  return abtp;
}

DaoFunction* DaoNameSpace_ParsePrototype( DaoNameSpace *self, const char *proto, DaoParser *parser )
{
  DaoFunction *func = DaoFunction_New();
  DaoVmSpace *vms = self->vmSpace;
  DaoParser *pp = parser;
  int key = DKEY_OPERATOR;

  GC_IncRC( self );
  func->nameSpace = self;
  if( parser == NULL ){
    parser = DaoParser_New();
    parser->vmSpace = vms;
    parser->nameSpace = self;
  }
  if( ! DaoToken_Tokenize( parser->tokens, proto, 0, 0, 0 ) ) goto Error;
  if( parser->tokens->size ==0 ) goto Error;
  if( parser->tokens->items.pToken[0]->type == DTOK_IDENTIFIER ) key = 0;
  DArray_Clear( parser->partoks );

  parser->routine = (DaoRoutine*) func; /* safe to parse params only */
  if( DaoParser_ParsePrototype( parser, parser, key, 0 ) < 0 ) goto Error;
  if( DaoParser_ParseParams( parser ) == 0 ) goto Error;
  if( parser != pp ) DaoParser_Delete( parser );
  return func;
Error:
  if( parser != pp ) DaoParser_Delete( parser );
  DArray_Clear( func->routOverLoad ); /* routOverLoad contains func */
  DaoFunction_Delete( func );
  return NULL;
}
