/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains routines used for analyzing expressions and
** for generating VDBE code that evaluates expressions in SQLite.
**
** $Id: expr.c,v 1.6 2002/03/26 14:17:52 matt Exp $
*/
#include "sqliteInt.h"


/*
** Construct a new expression node and return a pointer to it.  Memory
** for this node is obtained from sqliteMalloc().  The calling function
** is responsible for making sure the node eventually gets freed.
*/
Expr *sqliteExpr(int op, Expr *pLeft, Expr *pRight, Token *pToken){
  Expr *pNew;
  pNew = sqliteMalloc( sizeof(Expr) );
  if( pNew==0 ){
    sqliteExprDelete(pLeft);
    sqliteExprDelete(pRight);
    return 0;
  }
  pNew->op = op;
  pNew->pLeft = pLeft;
  pNew->pRight = pRight;
  if( pToken ){
    pNew->token = *pToken;
  }else{
    pNew->token.z = 0;
    pNew->token.n = 0;
  }
  if( pLeft && pRight ){
    sqliteExprSpan(pNew, &pLeft->span, &pRight->span);
  }else{
    pNew->span = pNew->token;
  }
  return pNew;
}

/*
** Set the Expr.token field of the given expression to span all
** text between the two given tokens.
*/
void sqliteExprSpan(Expr *pExpr, Token *pLeft, Token *pRight){
  if( pExpr ){
    pExpr->span.z = pLeft->z;
    pExpr->span.n = pRight->n + Addr(pRight->z) - Addr(pLeft->z);
  }
}

/*
** Construct a new expression node for a function with multiple
** arguments.
*/
Expr *sqliteExprFunction(ExprList *pList, Token *pToken){
  Expr *pNew;
  pNew = sqliteMalloc( sizeof(Expr) );
  if( pNew==0 ){
    sqliteExprListDelete(pList);
    return 0;
  }
  pNew->op = TK_FUNCTION;
  pNew->pList = pList;
  if( pToken ){
    pNew->token = *pToken;
  }else{
    pNew->token.z = 0;
    pNew->token.n = 0;
  }
  return pNew;
}

/*
** Recursively delete an expression tree.
*/
void sqliteExprDelete(Expr *p){
  if( p==0 ) return;
  if( p->pLeft ) sqliteExprDelete(p->pLeft);
  if( p->pRight ) sqliteExprDelete(p->pRight);
  if( p->pList ) sqliteExprListDelete(p->pList);
  if( p->pSelect ) sqliteSelectDelete(p->pSelect);
  sqliteFree(p);
}

/*
** The following group of functions are used to translate the string
** pointers of tokens in expression from one buffer to another.
**
** Normally, the Expr.token.z and Expr.span.z fields point into the
** original input buffer of an SQL statement.  This is usually OK
** since the SQL statement is executed and the expression is deleted
** before the input buffer is freed.  Making the tokens point to the
** original input buffer saves many calls to malloc() and thus helps
** the library to run faster. 
**
** But sometimes we need an expression to persist past the time when
** the input buffer is freed.  (Example: The SELECT clause of a
** CREATE VIEW statement contains expressions that must persist for
** the life of the view.)  When that happens we have to make a
** persistent copy of the input buffer and translate the Expr.token.z
** and Expr.span.z fields to point to the copy rather than the 
** original input buffer.  The following group of routines handle that
** translation.
**
** The "offset" parameter is the distance from the original input buffer
** to the persistent copy.  These routines recursively walk the entire
** expression tree and shift all tokens by "offset" amount.
**
** The work of figuring out the appropriate "offset" and making the
** presistent copy of the input buffer is done by the calling routine.
*/
void sqliteExprMoveStrings(Expr *p, int offset){
  if( p==0 ) return;
  if( p->token.z ) p->token.z += offset;
  if( p->span.z ) p->span.z += offset;
  if( p->pLeft ) sqliteExprMoveStrings(p->pLeft, offset);
  if( p->pRight ) sqliteExprMoveStrings(p->pRight, offset);
  if( p->pList ) sqliteExprListMoveStrings(p->pList, offset);
  if( p->pSelect ) sqliteSelectMoveStrings(p->pSelect, offset);
}
void sqliteExprListMoveStrings(ExprList *pList, int offset){
  int i;
  if( pList==0 ) return;
  for(i=0; i<pList->nExpr; i++){
    sqliteExprMoveStrings(pList->a[i].pExpr, offset);
  }
}
void sqliteSelectMoveStrings(Select *pSelect, int offset){
  if( pSelect==0 ) return;
  sqliteExprListMoveStrings(pSelect->pEList, offset);
  sqliteExprMoveStrings(pSelect->pWhere, offset);
  sqliteExprListMoveStrings(pSelect->pGroupBy, offset);
  sqliteExprMoveStrings(pSelect->pHaving, offset);
  sqliteExprListMoveStrings(pSelect->pOrderBy, offset);
  sqliteSelectMoveStrings(pSelect->pPrior, offset);
}

/*
** The following group of routines make deep copies of expressions,
** expression lists, ID lists, and select statements.  The copies can
** be deleted (by being passed to their respective ...Delete() routines)
** without effecting the originals.
**
** Note, however, that the Expr.token.z and Expr.span.z fields point to
** string space that is allocated separately from the expression tree
** itself.  These routines do NOT duplicate that string space.
**
** The expression list and ID list return by sqliteExprListDup() and 
** sqliteIdListDup() can not be further expanded by subsequent calls
** to sqliteExprListAppend() or sqliteIdListAppend().
**
** Any tables that the ID list might point to are not duplicated.
*/
Expr *sqliteExprDup(Expr *p){
  Expr *pNew;
  if( p==0 ) return 0;
  pNew = sqliteMalloc( sizeof(*p) );
  if( pNew==0 ) return 0;
  pNew->op = p->op;
  pNew->pLeft = sqliteExprDup(p->pLeft);
  pNew->pRight = sqliteExprDup(p->pRight);
  pNew->pList = sqliteExprListDup(p->pList);
  pNew->iTable = p->iTable;
  pNew->iColumn = p->iColumn;
  pNew->iAgg = p->iAgg;
  pNew->token = p->token;
  pNew->span = p->span;
  pNew->pSelect = sqliteSelectDup(p->pSelect);
  return pNew;
}
ExprList *sqliteExprListDup(ExprList *p){
  ExprList *pNew;
  int i;
  if( p==0 ) return 0;
  pNew = sqliteMalloc( sizeof(*pNew) );
  if( pNew==0 ) return 0;
  pNew->nExpr = p->nExpr;
  pNew->a = sqliteMalloc( p->nExpr*sizeof(p->a[0]) );
  for(i=0; i<p->nExpr; i++){
    pNew->a[i].pExpr = sqliteExprDup(p->a[i].pExpr);
    pNew->a[i].zName = sqliteStrDup(p->a[i].zName);
    pNew->a[i].sortOrder = p->a[i].sortOrder;
    pNew->a[i].isAgg = p->a[i].isAgg;
    pNew->a[i].done = 0;
  }
  return pNew;
}
IdList *sqliteIdListDup(IdList *p){
  IdList *pNew;
  int i;
  if( p==0 ) return 0;
  pNew = sqliteMalloc( sizeof(*pNew) );
  if( pNew==0 ) return 0;
  pNew->nId = p->nId;
  pNew->a = sqliteMalloc( p->nId*sizeof(p->a[0]) );
  for(i=0; i<p->nId; i++){
    pNew->a[i].zName = sqliteStrDup(p->a[i].zName);
    pNew->a[i].zAlias = sqliteStrDup(p->a[i].zAlias);
    pNew->a[i].idx = p->a[i].idx;
    pNew->a[i].pTab = 0;
    pNew->a[i].pSelect = sqliteSelectDup(p->a[i].pSelect);
  }
  return pNew;
}
Select *sqliteSelectDup(Select *p){
  Select *pNew;
  if( p==0 ) return 0;
  pNew = sqliteMalloc( sizeof(*p) );
  if( pNew==0 ) return 0;
  pNew->isDistinct = p->isDistinct;
  pNew->pEList = sqliteExprListDup(p->pEList);
  pNew->pSrc = sqliteIdListDup(p->pSrc);
  pNew->pWhere = sqliteExprDup(p->pWhere);
  pNew->pGroupBy = sqliteExprListDup(p->pGroupBy);
  pNew->pHaving = sqliteExprDup(p->pHaving);
  pNew->pOrderBy = sqliteExprListDup(p->pOrderBy);
  pNew->op = p->op;
  pNew->pPrior = sqliteSelectDup(p->pPrior);
  pNew->nLimit = p->nLimit;
  pNew->nOffset = p->nOffset;
  pNew->zSelect = 0;
  return pNew;
}


/*
** Add a new element to the end of an expression list.  If pList is
** initially NULL, then create a new expression list.
*/
ExprList *sqliteExprListAppend(ExprList *pList, Expr *pExpr, Token *pName){
  int i;
  if( pList==0 ){
    pList = sqliteMalloc( sizeof(ExprList) );
    if( pList==0 ){
      sqliteExprDelete(pExpr);
      return 0;
    }
  }
  if( (pList->nExpr & 7)==0 ){
    int n = pList->nExpr + 8;
    struct ExprList_item *a;
    a = sqliteRealloc(pList->a, n*sizeof(pList->a[0]));
    if( a==0 ){
      sqliteExprDelete(pExpr);
      return pList;
    }
    pList->a = a;
  }
  if( pExpr || pName ){
    i = pList->nExpr++;
    pList->a[i].pExpr = pExpr;
    pList->a[i].zName = 0;
    if( pName ){
      sqliteSetNString(&pList->a[i].zName, pName->z, pName->n, 0);
      sqliteDequote(pList->a[i].zName);
    }
  }
  return pList;
}

/*
** Delete an entire expression list.
*/
void sqliteExprListDelete(ExprList *pList){
  int i;
  if( pList==0 ) return;
  for(i=0; i<pList->nExpr; i++){
    sqliteExprDelete(pList->a[i].pExpr);
    sqliteFree(pList->a[i].zName);
  }
  sqliteFree(pList->a);
  sqliteFree(pList);
}

/*
** Walk an expression tree.  Return 1 if the expression is constant
** and 0 if it involves variables.
*/
int sqliteExprIsConstant(Expr *p){
  switch( p->op ){
    case TK_ID:
    case TK_COLUMN:
    case TK_DOT:
      return 0;
    case TK_INTEGER:
    case TK_FLOAT:
    case TK_STRING:
      return 1;
    default: {
      if( p->pLeft && !sqliteExprIsConstant(p->pLeft) ) return 0;
      if( p->pRight && !sqliteExprIsConstant(p->pRight) ) return 0;
      if( p->pList ){
        int i;
        for(i=0; i<p->pList->nExpr; i++){
          if( !sqliteExprIsConstant(p->pList->a[i].pExpr) ) return 0;
        }
      }
      return p->pLeft!=0 || p->pRight!=0 || (p->pList && p->pList->nExpr>0);
    }
  }
  return 0;
}

/*
** Return TRUE if the given string is a row-id column name.
*/
static int sqliteIsRowid(const char *z){
  if( sqliteStrICmp(z, "_ROWID_")==0 ) return 1;
  if( sqliteStrICmp(z, "ROWID")==0 ) return 1;
  if( sqliteStrICmp(z, "OID")==0 ) return 1;
  return 0;
}

/*
** This routine walks an expression tree and resolves references to
** table columns.  Nodes of the form ID.ID or ID resolve into an
** index to the table in the table list and a column offset.  The 
** Expr.opcode for such nodes is changed to TK_COLUMN.  The Expr.iTable
** value is changed to the index of the referenced table in pTabList
** plus the "base" value.  The base value will ultimately become the
** VDBE cursor number for a cursor that is pointing into the referenced
** table.  The Expr.iColumn value is changed to the index of the column 
** of the referenced table.  The Expr.iColumn value for the special
** ROWID column is -1.  Any INTEGER PRIMARY KEY column is tried as an
** alias for ROWID.
**
** We also check for instances of the IN operator.  IN comes in two
** forms:
**
**           expr IN (exprlist)
** and
**           expr IN (SELECT ...)
**
** The first form is handled by creating a set holding the list
** of allowed values.  The second form causes the SELECT to generate 
** a temporary table.
**
** This routine also looks for scalar SELECTs that are part of an expression.
** If it finds any, it generates code to write the value of that select
** into a memory cell.
**
** Unknown columns or tables provoke an error.  The function returns
** the number of errors seen and leaves an error message on pParse->zErrMsg.
*/
int sqliteExprResolveIds(
  Parse *pParse,     /* The parser context */
  int base,          /* VDBE cursor number for first entry in pTabList */
  IdList *pTabList,  /* List of tables used to resolve column names */
  ExprList *pEList,  /* List of expressions used to resolve "AS" */
  Expr *pExpr        /* The expression to be analyzed. */
){
  if( pExpr==0 || pTabList==0 ) return 0;
  assert( base+pTabList->nId<=pParse->nTab );
  switch( pExpr->op ){
    /* A lone identifier.  Try and match it as follows:
    **
    **     1.  To the name of a column of one of the tables in pTabList
    **
    **     2.  To the right side of an AS keyword in the column list of
    **         a SELECT statement.  (For example, match against 'x' in
    **         "SELECT a+b AS 'x' FROM t1".)
    **
    **     3.  One of the special names "ROWID", "OID", or "_ROWID_".
    */
    case TK_ID: {
      int cnt = 0;      /* Number of matches */
      int i;            /* Loop counter */
      char *z;
      assert( pExpr->token.z );
      z = sqliteStrNDup(pExpr->token.z, pExpr->token.n);
      sqliteDequote(z);
      if( z==0 ) return 1;
      for(i=0; i<pTabList->nId; i++){
        int j;
        Table *pTab = pTabList->a[i].pTab;
        if( pTab==0 ) continue;
        assert( pTab->nCol>0 );
        for(j=0; j<pTab->nCol; j++){
          if( sqliteStrICmp(pTab->aCol[j].zName, z)==0 ){
            cnt++;
            pExpr->iTable = i + base;
            if( j==pTab->iPKey ){
              /* Substitute the record number for the INTEGER PRIMARY KEY */
              pExpr->iColumn = -1;
            }else{
              pExpr->iColumn = j;
            }
            pExpr->op = TK_COLUMN;
          }
        }
      }
      if( cnt==0 && pEList!=0 ){
        int j;
        for(j=0; j<pEList->nExpr; j++){
          char *zAs = pEList->a[j].zName;
          if( zAs!=0 && sqliteStrICmp(zAs, z)==0 ){
            cnt++;
            assert( pExpr->pLeft==0 && pExpr->pRight==0 );
            pExpr->op = TK_AS;
            pExpr->iColumn = j;
            pExpr->pLeft = sqliteExprDup(pEList->a[j].pExpr);
          }
        } 
      }
      if( cnt==0 && sqliteIsRowid(z) ){
        pExpr->iColumn = -1;
        pExpr->iTable = base;
        cnt = 1 + (pTabList->nId>1);
        pExpr->op = TK_COLUMN;
      }
      sqliteFree(z);
      if( cnt==0 ){
        sqliteSetNString(&pParse->zErrMsg, "no such column: ", -1,  
          pExpr->token.z, pExpr->token.n, 0);
        pParse->nErr++;
        return 1;
      }else if( cnt>1 ){
        sqliteSetNString(&pParse->zErrMsg, "ambiguous column name: ", -1,  
          pExpr->token.z, pExpr->token.n, 0);
        pParse->nErr++;
        return 1;
      }
      break; 
    }
  
    /* A table name and column name:  ID.ID */
    case TK_DOT: {
      int cnt = 0;             /* Number of matches */
      int cntTab = 0;          /* Number of matching tables */
      int i;                   /* Loop counter */
      Expr *pLeft, *pRight;    /* Left and right subbranches of the expr */
      char *zLeft, *zRight;    /* Text of an identifier */

      pLeft = pExpr->pLeft;
      pRight = pExpr->pRight;
      assert( pLeft && pLeft->op==TK_ID && pLeft->token.z );
      assert( pRight && pRight->op==TK_ID && pRight->token.z );
      zLeft = sqliteStrNDup(pLeft->token.z, pLeft->token.n);
      zRight = sqliteStrNDup(pRight->token.z, pRight->token.n);
      if( zLeft==0 || zRight==0 ){
        sqliteFree(zLeft);
        sqliteFree(zRight);
        return 1;
      }
      sqliteDequote(zLeft);
      sqliteDequote(zRight);
      pExpr->iTable = -1;
      for(i=0; i<pTabList->nId; i++){
        int j;
        char *zTab;
        Table *pTab = pTabList->a[i].pTab;
        if( pTab==0 ) continue;
        assert( pTab->nCol>0 );
        if( pTabList->a[i].zAlias ){
          zTab = pTabList->a[i].zAlias;
        }else{
          zTab = pTab->zName;
        }
        if( zTab==0 || sqliteStrICmp(zTab, zLeft)!=0 ) continue;
        if( 0==(cntTab++) ) pExpr->iTable = i + base;
        for(j=0; j<pTab->nCol; j++){
          if( sqliteStrICmp(pTab->aCol[j].zName, zRight)==0 ){
            cnt++;
            pExpr->iTable = i + base;
            if( j==pTab->iPKey ){
              /* Substitute the record number for the INTEGER PRIMARY KEY */
              pExpr->iColumn = -1;
            }else{
              pExpr->iColumn = j;
            }
          }
        }
      }
      if( cnt==0 && cntTab==1 && sqliteIsRowid(zRight) ){
        cnt = 1;
        pExpr->iColumn = -1;
      }
      sqliteFree(zLeft);
      sqliteFree(zRight);
      if( cnt==0 ){
        sqliteSetNString(&pParse->zErrMsg, "no such column: ", -1,  
          pLeft->token.z, pLeft->token.n, ".", 1, 
          pRight->token.z, pRight->token.n, 0);
        pParse->nErr++;
        return 1;
      }else if( cnt>1 ){
        sqliteSetNString(&pParse->zErrMsg, "ambiguous column name: ", -1,  
          pLeft->token.z, pLeft->token.n, ".", 1,
          pRight->token.z, pRight->token.n, 0);
        pParse->nErr++;
        return 1;
      }
      sqliteExprDelete(pLeft);
      pExpr->pLeft = 0;
      sqliteExprDelete(pRight);
      pExpr->pRight = 0;
      pExpr->op = TK_COLUMN;
      break;
    }

    case TK_IN: {
      Vdbe *v = sqliteGetVdbe(pParse);
      if( v==0 ) return 1;
      if( sqliteExprResolveIds(pParse, base, pTabList, pEList, pExpr->pLeft) ){
        return 1;
      }
      if( pExpr->pSelect ){
        /* Case 1:     expr IN (SELECT ...)
        **
        ** Generate code to write the results of the select into a temporary
        ** table.  The cursor number of the temporary table has already
        ** been put in iTable by sqliteExprResolveInSelect().
        */
        pExpr->iTable = pParse->nTab++;
        sqliteVdbeAddOp(v, OP_OpenTemp, pExpr->iTable, 1);
        sqliteSelect(pParse, pExpr->pSelect, SRT_Set, pExpr->iTable, 0,0,0);
      }else if( pExpr->pList ){
        /* Case 2:     expr IN (exprlist)
        **
        ** Create a set to put the exprlist values in.  The Set id is stored
        ** in iTable.
        */
        int i, iSet;
        for(i=0; i<pExpr->pList->nExpr; i++){
          Expr *pE2 = pExpr->pList->a[i].pExpr;
          if( !sqliteExprIsConstant(pE2) ){
            sqliteSetString(&pParse->zErrMsg,
              "right-hand side of IN operator must be constant", 0);
            pParse->nErr++;
            return 1;
          }
          if( sqliteExprCheck(pParse, pE2, 0, 0) ){
            return 1;
          }
        }
        iSet = pExpr->iTable = pParse->nSet++;
        for(i=0; i<pExpr->pList->nExpr; i++){
          Expr *pE2 = pExpr->pList->a[i].pExpr;
          switch( pE2->op ){
            case TK_FLOAT:
            case TK_INTEGER:
            case TK_STRING: {
              int addr = sqliteVdbeAddOp(v, OP_SetInsert, iSet, 0);
              assert( pE2->token.z );
              sqliteVdbeChangeP3(v, addr, pE2->token.z, pE2->token.n);
              sqliteVdbeDequoteP3(v, addr);
              break;
            }
            default: {
              sqliteExprCode(pParse, pE2);
              sqliteVdbeAddOp(v, OP_SetInsert, iSet, 0);
              break;
            }
          }
        }
      }
      break;
    }

    case TK_SELECT: {
      /* This has to be a scalar SELECT.  Generate code to put the
      ** value of this select in a memory cell and record the number
      ** of the memory cell in iColumn.
      */
      pExpr->iColumn = pParse->nMem++;
      if( sqliteSelect(pParse, pExpr->pSelect, SRT_Mem, pExpr->iColumn,0,0,0) ){
        return 1;
      }
      break;
    }

    /* For all else, just recursively walk the tree */
    default: {
      if( pExpr->pLeft
      && sqliteExprResolveIds(pParse, base, pTabList, pEList, pExpr->pLeft) ){
        return 1;
      }
      if( pExpr->pRight 
      && sqliteExprResolveIds(pParse, base, pTabList, pEList, pExpr->pRight) ){
        return 1;
      }
      if( pExpr->pList ){
        int i;
        ExprList *pList = pExpr->pList;
        for(i=0; i<pList->nExpr; i++){
          Expr *pArg = pList->a[i].pExpr;
          if( sqliteExprResolveIds(pParse, base, pTabList, pEList, pArg) ){
            return 1;
          }
        }
      }
    }
  }
  return 0;
}

/*
** Error check the functions in an expression.  Make sure all
** function names are recognized and all functions have the correct
** number of arguments.  Leave an error message in pParse->zErrMsg
** if anything is amiss.  Return the number of errors.
**
** if pIsAgg is not null and this expression is an aggregate function
** (like count(*) or max(value)) then write a 1 into *pIsAgg.
*/
int sqliteExprCheck(Parse *pParse, Expr *pExpr, int allowAgg, int *pIsAgg){
  int nErr = 0;
  if( pExpr==0 ) return 0;
  switch( pExpr->op ){
    case TK_FUNCTION: {
      int n = pExpr->pList ? pExpr->pList->nExpr : 0;
      int no_such_func = 0;
      int wrong_num_args = 0;
      int is_agg = 0;
      int i;
      FuncDef *pDef;

      pDef = sqliteFindFunction(pParse->db,
         pExpr->token.z, pExpr->token.n, n, 0);
      if( pDef==0 ){
        pDef = sqliteFindFunction(pParse->db,
           pExpr->token.z, pExpr->token.n, -1, 0);
        if( pDef==0 ){
          no_such_func = 1;
        }else{
          wrong_num_args = 1;
        }
      }else{
        is_agg = pDef->xFunc==0;
      }
      if( is_agg && !allowAgg ){
        sqliteSetNString(&pParse->zErrMsg, "misuse of aggregate function ", -1,
           pExpr->token.z, pExpr->token.n, "()", 2, 0);
        pParse->nErr++;
        nErr++;
        is_agg = 0;
      }else if( no_such_func ){
        sqliteSetNString(&pParse->zErrMsg, "no such function: ", -1,
           pExpr->token.z, pExpr->token.n, 0);
        pParse->nErr++;
        nErr++;
      }else if( wrong_num_args ){
        sqliteSetNString(&pParse->zErrMsg, 
           "wrong number of arguments to function ",-1,
           pExpr->token.z, pExpr->token.n, "()", 2, 0);
        pParse->nErr++;
        nErr++;
      }
      if( is_agg ) pExpr->op = TK_AGG_FUNCTION;
      if( is_agg && pIsAgg ) *pIsAgg = 1;
      for(i=0; nErr==0 && i<n; i++){
        nErr = sqliteExprCheck(pParse, pExpr->pList->a[i].pExpr,
                               allowAgg && !is_agg, pIsAgg);
      }
    }
    default: {
      if( pExpr->pLeft ){
        nErr = sqliteExprCheck(pParse, pExpr->pLeft, allowAgg, pIsAgg);
      }
      if( nErr==0 && pExpr->pRight ){
        nErr = sqliteExprCheck(pParse, pExpr->pRight, allowAgg, pIsAgg);
      }
      if( nErr==0 && pExpr->pList ){
        int n = pExpr->pList->nExpr;
        int i;
        for(i=0; nErr==0 && i<n; i++){
          Expr *pE2 = pExpr->pList->a[i].pExpr;
          nErr = sqliteExprCheck(pParse, pE2, allowAgg, pIsAgg);
        }
      }
      break;
    }
  }
  return nErr;
}

/*
** Generate code into the current Vdbe to evaluate the given
** expression and leave the result on the top of stack.
*/
void sqliteExprCode(Parse *pParse, Expr *pExpr){
  Vdbe *v = pParse->pVdbe;
  int op;
  if( v==0 || pExpr==0 ) return;
  switch( pExpr->op ){
    case TK_PLUS:     op = OP_Add;      break;
    case TK_MINUS:    op = OP_Subtract; break;
    case TK_STAR:     op = OP_Multiply; break;
    case TK_SLASH:    op = OP_Divide;   break;
    case TK_AND:      op = OP_And;      break;
    case TK_OR:       op = OP_Or;       break;
    case TK_LT:       op = OP_Lt;       break;
    case TK_LE:       op = OP_Le;       break;
    case TK_GT:       op = OP_Gt;       break;
    case TK_GE:       op = OP_Ge;       break;
    case TK_NE:       op = OP_Ne;       break;
    case TK_EQ:       op = OP_Eq;       break;
    case TK_LIKE:     op = OP_Like;     break;
    case TK_GLOB:     op = OP_Glob;     break;
    case TK_ISNULL:   op = OP_IsNull;   break;
    case TK_NOTNULL:  op = OP_NotNull;  break;
    case TK_NOT:      op = OP_Not;      break;
    case TK_UMINUS:   op = OP_Negative; break;
    case TK_BITAND:   op = OP_BitAnd;   break;
    case TK_BITOR:    op = OP_BitOr;    break;
    case TK_BITNOT:   op = OP_BitNot;   break;
    case TK_LSHIFT:   op = OP_ShiftLeft;  break;
    case TK_RSHIFT:   op = OP_ShiftRight; break;
    case TK_REM:      op = OP_Remainder;  break;
    default: break;
  }
  switch( pExpr->op ){
    case TK_COLUMN: {
      if( pParse->useAgg ){
        sqliteVdbeAddOp(v, OP_AggGet, 0, pExpr->iAgg);
      }else if( pExpr->iColumn>=0 ){
        sqliteVdbeAddOp(v, OP_Column, pExpr->iTable, pExpr->iColumn);
      }else{
        sqliteVdbeAddOp(v, OP_Recno, pExpr->iTable, 0);
      }
      break;
    }
    case TK_INTEGER: {
      sqliteVdbeAddOp(v, OP_Integer, atoi(pExpr->token.z), 0);
      sqliteVdbeChangeP3(v, -1, pExpr->token.z, pExpr->token.n);
      break;
    }
    case TK_FLOAT: {
      sqliteVdbeAddOp(v, OP_String, 0, 0);
      assert( pExpr->token.z );
      sqliteVdbeChangeP3(v, -1, pExpr->token.z, pExpr->token.n);
      break;
    }
    case TK_STRING: {
      int addr = sqliteVdbeAddOp(v, OP_String, 0, 0);
      assert( pExpr->token.z );
      sqliteVdbeChangeP3(v, addr, pExpr->token.z, pExpr->token.n);
      sqliteVdbeDequoteP3(v, addr);
      break;
    }
    case TK_NULL: {
      sqliteVdbeAddOp(v, OP_String, 0, 0);
      break;
    }
    case TK_AND:
    case TK_OR:
    case TK_PLUS:
    case TK_STAR:
    case TK_MINUS:
    case TK_REM:
    case TK_BITAND:
    case TK_BITOR:
    case TK_SLASH: {
      sqliteExprCode(pParse, pExpr->pLeft);
      sqliteExprCode(pParse, pExpr->pRight);
      sqliteVdbeAddOp(v, op, 0, 0);
      break;
    }
    case TK_LSHIFT:
    case TK_RSHIFT: {
      sqliteExprCode(pParse, pExpr->pRight);
      sqliteExprCode(pParse, pExpr->pLeft);
      sqliteVdbeAddOp(v, op, 0, 0);
      break;
    }
    case TK_CONCAT: {
      sqliteExprCode(pParse, pExpr->pLeft);
      sqliteExprCode(pParse, pExpr->pRight);
      sqliteVdbeAddOp(v, OP_Concat, 2, 0);
      break;
    }
    case TK_LT:
    case TK_LE:
    case TK_GT:
    case TK_GE:
    case TK_NE:
    case TK_EQ: 
    case TK_LIKE: 
    case TK_GLOB: {
      int dest;
      sqliteVdbeAddOp(v, OP_Integer, 1, 0);
      sqliteExprCode(pParse, pExpr->pLeft);
      sqliteExprCode(pParse, pExpr->pRight);
      dest = sqliteVdbeCurrentAddr(v) + 2;
      sqliteVdbeAddOp(v, op, 0, dest);
      sqliteVdbeAddOp(v, OP_AddImm, -1, 0);
      break;
    }
    case TK_UMINUS: {
      assert( pExpr->pLeft );
      if( pExpr->pLeft->op==TK_FLOAT || pExpr->pLeft->op==TK_INTEGER ){
        Token *p = &pExpr->pLeft->token;
        char *z = sqliteMalloc( p->n + 2 );
        sprintf(z, "-%.*s", p->n, p->z);
        if( pExpr->pLeft->op==TK_INTEGER ){
          sqliteVdbeAddOp(v, OP_Integer, atoi(z), 0);
        }else{
          sqliteVdbeAddOp(v, OP_String, 0, 0);
        }
        sqliteVdbeChangeP3(v, -1, z, p->n+1);
        sqliteFree(z);
        break;
      }
      /* Fall through into TK_NOT */
    }
    case TK_BITNOT:
    case TK_NOT: {
      sqliteExprCode(pParse, pExpr->pLeft);
      sqliteVdbeAddOp(v, op, 0, 0);
      break;
    }
    case TK_ISNULL:
    case TK_NOTNULL: {
      int dest;
      sqliteVdbeAddOp(v, OP_Integer, 1, 0);
      sqliteExprCode(pParse, pExpr->pLeft);
      dest = sqliteVdbeCurrentAddr(v) + 2;
      sqliteVdbeAddOp(v, op, 0, dest);
      sqliteVdbeAddOp(v, OP_AddImm, -1, 0);
      break;
    }
    case TK_AGG_FUNCTION: {
      sqliteVdbeAddOp(v, OP_AggGet, 0, pExpr->iAgg);
      break;
    }
    case TK_FUNCTION: {
      int i;
      ExprList *pList = pExpr->pList;
      int nExpr = pList ? pList->nExpr : 0;
      FuncDef *pDef;
      pDef = sqliteFindFunction(pParse->db,
                      pExpr->token.z, pExpr->token.n, nExpr, 0);
      assert( pDef!=0 );
      for(i=0; i<nExpr; i++){
        sqliteExprCode(pParse, pList->a[i].pExpr);
      }
      sqliteVdbeAddOp(v, OP_Function, nExpr, 0);
      sqliteVdbeChangeP3(v, -1, (char*)pDef, P3_POINTER);
      break;
    }
    case TK_SELECT: {
      sqliteVdbeAddOp(v, OP_MemLoad, pExpr->iColumn, 0);
      break;
    }
    case TK_IN: {
      int addr;
      sqliteVdbeAddOp(v, OP_Integer, 1, 0);
      sqliteExprCode(pParse, pExpr->pLeft);
      addr = sqliteVdbeCurrentAddr(v);
      if( pExpr->pSelect ){
        sqliteVdbeAddOp(v, OP_Found, pExpr->iTable, addr+2);
      }else{
        sqliteVdbeAddOp(v, OP_SetFound, pExpr->iTable, addr+2);
      }
      sqliteVdbeAddOp(v, OP_AddImm, -1, 0);
      break;
    }
    case TK_BETWEEN: {
      int lbl = sqliteVdbeMakeLabel(v);
      sqliteVdbeAddOp(v, OP_Integer, 0, 0);
      sqliteExprIfFalse(pParse, pExpr, lbl);
      sqliteVdbeAddOp(v, OP_AddImm, 1, 0);
      sqliteVdbeResolveLabel(v, lbl);
      break;
    }
    case TK_AS: {
      sqliteExprCode(pParse, pExpr->pLeft);
      break;
    }
  }
  return;
}

/*
** Generate code for a boolean expression such that a jump is made
** to the label "dest" if the expression is true but execution
** continues straight thru if the expression is false.
*/
void sqliteExprIfTrue(Parse *pParse, Expr *pExpr, int dest){
  Vdbe *v = pParse->pVdbe;
  int op = 0;
  if( v==0 || pExpr==0 ) return;
  switch( pExpr->op ){
    case TK_LT:       op = OP_Lt;       break;
    case TK_LE:       op = OP_Le;       break;
    case TK_GT:       op = OP_Gt;       break;
    case TK_GE:       op = OP_Ge;       break;
    case TK_NE:       op = OP_Ne;       break;
    case TK_EQ:       op = OP_Eq;       break;
    case TK_LIKE:     op = OP_Like;     break;
    case TK_GLOB:     op = OP_Glob;     break;
    case TK_ISNULL:   op = OP_IsNull;   break;
    case TK_NOTNULL:  op = OP_NotNull;  break;
    default:  break;
  }
  switch( pExpr->op ){
    case TK_AND: {
      int d2 = sqliteVdbeMakeLabel(v);
      sqliteExprIfFalse(pParse, pExpr->pLeft, d2);
      sqliteExprIfTrue(pParse, pExpr->pRight, dest);
      sqliteVdbeResolveLabel(v, d2);
      break;
    }
    case TK_OR: {
      sqliteExprIfTrue(pParse, pExpr->pLeft, dest);
      sqliteExprIfTrue(pParse, pExpr->pRight, dest);
      break;
    }
    case TK_NOT: {
      sqliteExprIfFalse(pParse, pExpr->pLeft, dest);
      break;
    }
    case TK_LT:
    case TK_LE:
    case TK_GT:
    case TK_GE:
    case TK_NE:
    case TK_EQ:
    case TK_LIKE:
    case TK_GLOB: {
      sqliteExprCode(pParse, pExpr->pLeft);
      sqliteExprCode(pParse, pExpr->pRight);
      sqliteVdbeAddOp(v, op, 0, dest);
      break;
    }
    case TK_ISNULL:
    case TK_NOTNULL: {
      sqliteExprCode(pParse, pExpr->pLeft);
      sqliteVdbeAddOp(v, op, 0, dest);
      break;
    }
    case TK_IN: {
      sqliteExprCode(pParse, pExpr->pLeft);
      if( pExpr->pSelect ){
        sqliteVdbeAddOp(v, OP_Found, pExpr->iTable, dest);
      }else{
        sqliteVdbeAddOp(v, OP_SetFound, pExpr->iTable, dest);
      }
      break;
    }
    case TK_BETWEEN: {
      int lbl = sqliteVdbeMakeLabel(v);
      sqliteExprCode(pParse, pExpr->pLeft);
      sqliteVdbeAddOp(v, OP_Dup, 0, 0);
      sqliteExprCode(pParse, pExpr->pList->a[0].pExpr);
      sqliteVdbeAddOp(v, OP_Lt, 0, lbl);
      sqliteExprCode(pParse, pExpr->pList->a[1].pExpr);
      sqliteVdbeAddOp(v, OP_Le, 0, dest);
      sqliteVdbeAddOp(v, OP_Integer, 0, 0);
      sqliteVdbeResolveLabel(v, lbl);
      sqliteVdbeAddOp(v, OP_Pop, 1, 0);
      break;
    }
    default: {
      sqliteExprCode(pParse, pExpr);
      sqliteVdbeAddOp(v, OP_If, 0, dest);
      break;
    }
  }
}

/*
** Generate code for a boolean expression such that a jump is made
** to the label "dest" if the expression is false but execution
** continues straight thru if the expression is true.
*/
void sqliteExprIfFalse(Parse *pParse, Expr *pExpr, int dest){
  Vdbe *v = pParse->pVdbe;
  int op = 0;
  if( v==0 || pExpr==0 ) return;
  switch( pExpr->op ){
    case TK_LT:       op = OP_Ge;       break;
    case TK_LE:       op = OP_Gt;       break;
    case TK_GT:       op = OP_Le;       break;
    case TK_GE:       op = OP_Lt;       break;
    case TK_NE:       op = OP_Eq;       break;
    case TK_EQ:       op = OP_Ne;       break;
    case TK_LIKE:     op = OP_Like;     break;
    case TK_GLOB:     op = OP_Glob;     break;
    case TK_ISNULL:   op = OP_NotNull;  break;
    case TK_NOTNULL:  op = OP_IsNull;   break;
    default:  break;
  }
  switch( pExpr->op ){
    case TK_AND: {
      sqliteExprIfFalse(pParse, pExpr->pLeft, dest);
      sqliteExprIfFalse(pParse, pExpr->pRight, dest);
      break;
    }
    case TK_OR: {
      int d2 = sqliteVdbeMakeLabel(v);
      sqliteExprIfTrue(pParse, pExpr->pLeft, d2);
      sqliteExprIfFalse(pParse, pExpr->pRight, dest);
      sqliteVdbeResolveLabel(v, d2);
      break;
    }
    case TK_NOT: {
      sqliteExprIfTrue(pParse, pExpr->pLeft, dest);
      break;
    }
    case TK_LT:
    case TK_LE:
    case TK_GT:
    case TK_GE:
    case TK_NE:
    case TK_EQ: {
      sqliteExprCode(pParse, pExpr->pLeft);
      sqliteExprCode(pParse, pExpr->pRight);
      sqliteVdbeAddOp(v, op, 0, dest);
      break;
    }
    case TK_LIKE:
    case TK_GLOB: {
      sqliteExprCode(pParse, pExpr->pLeft);
      sqliteExprCode(pParse, pExpr->pRight);
      sqliteVdbeAddOp(v, op, 1, dest);
      break;
    }
    case TK_ISNULL:
    case TK_NOTNULL: {
      sqliteExprCode(pParse, pExpr->pLeft);
      sqliteVdbeAddOp(v, op, 0, dest);
      break;
    }
    case TK_IN: {
      sqliteExprCode(pParse, pExpr->pLeft);
      if( pExpr->pSelect ){
        sqliteVdbeAddOp(v, OP_NotFound, pExpr->iTable, dest);
      }else{
        sqliteVdbeAddOp(v, OP_SetNotFound, pExpr->iTable, dest);
      }
      break;
    }
    case TK_BETWEEN: {
      int addr;
      sqliteExprCode(pParse, pExpr->pLeft);
      sqliteVdbeAddOp(v, OP_Dup, 0, 0);
      sqliteExprCode(pParse, pExpr->pList->a[0].pExpr);
      addr = sqliteVdbeCurrentAddr(v);
      sqliteVdbeAddOp(v, OP_Ge, 0, addr+3);
      sqliteVdbeAddOp(v, OP_Pop, 1, 0);
      sqliteVdbeAddOp(v, OP_Goto, 0, dest);
      sqliteExprCode(pParse, pExpr->pList->a[1].pExpr);
      sqliteVdbeAddOp(v, OP_Gt, 0, dest);
      break;
    }
    default: {
      sqliteExprCode(pParse, pExpr);
      sqliteVdbeAddOp(v, OP_Not, 0, 0);
      sqliteVdbeAddOp(v, OP_If, 0, dest);
      break;
    }
  }
}

/*
** Do a deep comparison of two expression trees.  Return TRUE (non-zero)
** if they are identical and return FALSE if they differ in any way.
*/
int sqliteExprCompare(Expr *pA, Expr *pB){
  int i;
  if( pA==0 ){
    return pB==0;
  }else if( pB==0 ){
    return 0;
  }
  if( pA->op!=pB->op ) return 0;
  if( !sqliteExprCompare(pA->pLeft, pB->pLeft) ) return 0;
  if( !sqliteExprCompare(pA->pRight, pB->pRight) ) return 0;
  if( pA->pList ){
    if( pB->pList==0 ) return 0;
    if( pA->pList->nExpr!=pB->pList->nExpr ) return 0;
    for(i=0; i<pA->pList->nExpr; i++){
      if( !sqliteExprCompare(pA->pList->a[i].pExpr, pB->pList->a[i].pExpr) ){
        return 0;
      }
    }
  }else if( pB->pList ){
    return 0;
  }
  if( pA->pSelect || pB->pSelect ) return 0;
  if( pA->token.z ){
    if( pB->token.z==0 ) return 0;
    if( pB->token.n!=pA->token.n ) return 0;
    if( sqliteStrNICmp(pA->token.z, pB->token.z, pA->token.n)!=0 ) return 0;
  }
  return 1;
}

/*
** Add a new element to the pParse->aAgg[] array and return its index.
*/
static int appendAggInfo(Parse *pParse){
  if( (pParse->nAgg & 0x7)==0 ){
    int amt = pParse->nAgg + 8;
    AggExpr *aAgg = sqliteRealloc(pParse->aAgg, amt*sizeof(pParse->aAgg[0]));
    if( aAgg==0 ){
      return -1;
    }
    pParse->aAgg = aAgg;
  }
  memset(&pParse->aAgg[pParse->nAgg], 0, sizeof(pParse->aAgg[0]));
  return pParse->nAgg++;
}

/*
** Analyze the given expression looking for aggregate functions and
** for variables that need to be added to the pParse->aAgg[] array.
** Make additional entries to the pParse->aAgg[] array as necessary.
**
** This routine should only be called after the expression has been
** analyzed by sqliteExprResolveIds() and sqliteExprCheck().
**
** If errors are seen, leave an error message in zErrMsg and return
** the number of errors.
*/
int sqliteExprAnalyzeAggregates(Parse *pParse, Expr *pExpr){
  int i;
  AggExpr *aAgg;
  int nErr = 0;

  if( pExpr==0 ) return 0;
  switch( pExpr->op ){
    case TK_COLUMN: {
      aAgg = pParse->aAgg;
      for(i=0; i<pParse->nAgg; i++){
        if( aAgg[i].isAgg ) continue;
        if( aAgg[i].pExpr->iTable==pExpr->iTable
         && aAgg[i].pExpr->iColumn==pExpr->iColumn ){
          break;
        }
      }
      if( i>=pParse->nAgg ){
        i = appendAggInfo(pParse);
        if( i<0 ) return 1;
        pParse->aAgg[i].isAgg = 0;
        pParse->aAgg[i].pExpr = pExpr;
      }
      pExpr->iAgg = i;
      break;
    }
    case TK_AGG_FUNCTION: {
      aAgg = pParse->aAgg;
      for(i=0; i<pParse->nAgg; i++){
        if( !aAgg[i].isAgg ) continue;
        if( sqliteExprCompare(aAgg[i].pExpr, pExpr) ){
          break;
        }
      }
      if( i>=pParse->nAgg ){
        i = appendAggInfo(pParse);
        if( i<0 ) return 1;
        pParse->aAgg[i].isAgg = 1;
        pParse->aAgg[i].pExpr = pExpr;
        pParse->aAgg[i].pFunc = sqliteFindFunction(pParse->db,
             pExpr->token.z, pExpr->token.n,
             pExpr->pList ? pExpr->pList->nExpr : 0, 0);
      }
      pExpr->iAgg = i;
      break;
    }
    default: {
      if( pExpr->pLeft ){
        nErr = sqliteExprAnalyzeAggregates(pParse, pExpr->pLeft);
      }
      if( nErr==0 && pExpr->pRight ){
        nErr = sqliteExprAnalyzeAggregates(pParse, pExpr->pRight);
      }
      if( nErr==0 && pExpr->pList ){
        int n = pExpr->pList->nExpr;
        int i;
        for(i=0; nErr==0 && i<n; i++){
          nErr = sqliteExprAnalyzeAggregates(pParse, pExpr->pList->a[i].pExpr);
        }
      }
      break;
    }
  }
  return nErr;
}

/*
** Locate a user function given a name and a number of arguments.
** Return a pointer to the FuncDef structure that defines that
** function, or return NULL if the function does not exist.
**
** If the createFlag argument is true, then a new (blank) FuncDef
** structure is created and liked into the "db" structure if a
** no matching function previously existed.  When createFlag is true
** and the nArg parameter is -1, then only a function that accepts
** any number of arguments will be returned.
**
** If createFlag is false and nArg is -1, then the first valid
** function found is returned.  A function is valid if either xFunc
** or xStep is non-zero.
*/
FuncDef *sqliteFindFunction(
  sqlite *db,        /* An open database */
  const char *zName, /* Name of the function.  Not null-terminated */
  int nName,         /* Number of characters in the name */
  int nArg,          /* Number of arguments.  -1 means any number */
  int createFlag     /* Create new entry if true and does not otherwise exist */
){
  FuncDef *pFirst, *p, *pMaybe;
  pFirst = p = (FuncDef*)sqliteHashFind(&db->aFunc, zName, nName);
  if( p && !createFlag && nArg<0 ){
    while( p && p->xFunc==0 && p->xStep==0 ){ p = p->pNext; }
    return p;
  }
  pMaybe = 0;
  while( p && p->nArg!=nArg ){
    if( p->nArg<0 && !createFlag && (p->xFunc || p->xStep) ) pMaybe = p;
    p = p->pNext;
  }
  if( p && !createFlag && p->xFunc==0 && p->xStep==0 ){
    return 0;
  }
  if( p==0 && pMaybe ){
    assert( createFlag==0 );
    return pMaybe;
  }
  if( p==0 && createFlag && (p = sqliteMalloc(sizeof(*p)))!=0 ){
    p->nArg = nArg;
    p->pNext = pFirst;
    sqliteHashInsert(&db->aFunc, zName, nName, (void*)p);
  }
  return p;
}