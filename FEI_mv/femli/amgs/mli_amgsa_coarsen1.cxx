/*BHEADER**********************************************************************
 * (c) 2001   The Regents of the University of California
 *
 * See the file COPYRIGHT_and_DISCLAIMER for a complete copyright
 * notice, contact person, and disclaimer.
 *
 *********************************************************************EHEADER*/

// *********************************************************************
// This file is customized to use HYPRE matrix format
// *********************************************************************

// *********************************************************************
// local includes
// ---------------------------------------------------------------------

#include <string.h>
#include <assert.h>
#include "HYPRE.h"
#include "utilities/utilities.h"
#include "IJ_mv/HYPRE_IJ_mv.h"
#include "seq_mv/seq_mv.h"
#include "parcsr_mv/parcsr_mv.h"

#include "vector/mli_vector.h"
#include "amgs/mli_method_amgsa.h"
#include "util/mli_utils.h"
#include "solver/mli_solver.h"
 
// *********************************************************************
// local defines
// ---------------------------------------------------------------------

#define MLI_METHOD_AMGSA_READY       -1
#define MLI_METHOD_AMGSA_SELECTED    -2
#define MLI_METHOD_AMGSA_PENDING     -3
#define MLI_METHOD_AMGSA_NOTSELECTED -4

#define habs(x) ((x > 0 ) ? x : -(x))

// ********************************************************************* 
// Purpose   : Given Amat and aggregation information, create the 
//             corresponding Pmat using the local aggregation scheme 
// ---------------------------------------------------------------------

double MLI_Method_AMGSA::genPLocal(MLI_Matrix *mli_Amat,
                                   MLI_Matrix **Pmat_out,
                                   int initCount, int *initAggr)
{
   HYPRE_IJMatrix         IJPmat;
   hypre_ParCSRMatrix     *Amat, *A2mat, *Pmat, *Gmat, *Jmat, *Pmat2;
   hypre_ParCSRCommPkg    *comm_pkg;
   MLI_Matrix             *mli_Pmat, *mli_Jmat, *mli_A2mat;
   MLI_Function           *func_ptr;
   MPI_Comm  comm;
   int       i, j, k, index, irow, mypid, numProcs, AStartRow, AEndRow;
   int       ALocalNRows, *partition, naggr, *node2aggr, *eqn2aggr, ierr;
   int       PLocalNCols, PStartCol, PGlobalNCols, *colInd, *P_cols;
   int       PLocalNRows, PStartRow, *rowLengths, rowNum;
   int       blkSize, maxAggSize, *aggCntArray, **aggIndArray;
   int       aggSize, info, nzcnt, *localLabels, AGlobalNRows;
   double    *colVal, **P_vecs, maxEigen=0, alpha;
   double    *qArray, *newNull, *rArray, ritzValues[2];
   char      paramString[200];

   /*-----------------------------------------------------------------
    * fetch matrix and machine information
    *-----------------------------------------------------------------*/

   Amat = (hypre_ParCSRMatrix *) mli_Amat->getMatrix();
   comm = hypre_ParCSRMatrixComm(Amat);
   MPI_Comm_rank(comm,&mypid);
   MPI_Comm_size(comm,&numProcs);

   /*-----------------------------------------------------------------
    * fetch other matrix information
    *-----------------------------------------------------------------*/

   HYPRE_ParCSRMatrixGetRowPartitioning((HYPRE_ParCSRMatrix) Amat,&partition);
   AStartRow = partition[mypid];
   AEndRow   = partition[mypid+1] - 1;
   AGlobalNRows = partition[numProcs];
   ALocalNRows  = AEndRow - AStartRow + 1;
   free( partition );
   if ( AGlobalNRows/currNodeDofs_ < minCoarseSize_ || 
        AGlobalNRows/currNodeDofs_ <= numProcs ) 
   {
      (*Pmat_out) = NULL;
      return 0.0;
   }

   /*-----------------------------------------------------------------
    * reduce Amat based on the block size information (if nodeDofs_ > 1)
    *-----------------------------------------------------------------*/

   if ( initAggr == NULL )
   {
      blkSize = currNodeDofs_;
      if (blkSize > 1) 
      {
         MLI_Matrix_Compress(mli_Amat, blkSize, &mli_A2mat);
         if ( saLabels_ != NULL && saLabels_[currLevel_] != NULL )
         {
            localLabels = new int[ALocalNRows/blkSize];
            for ( i = 0; i < ALocalNRows; i+=blkSize )
               localLabels[i/blkSize] = saLabels_[currLevel_][i];
         }
         else localLabels = NULL;
      }
      else 
      {
         mli_A2mat = mli_Amat;
         if ( saLabels_ != NULL && saLabels_[currLevel_] != NULL )
            localLabels = saLabels_[currLevel_];
         else
            localLabels = NULL;
      }
      A2mat = (hypre_ParCSRMatrix *) mli_A2mat->getMatrix();
   }

   /*-----------------------------------------------------------------
    * form aggregation graph by taking out weak edges
    *-----------------------------------------------------------------*/

   if ( initAggr == NULL ) formLocalGraph(A2mat, &Gmat, localLabels);

   /*-----------------------------------------------------------------
    * modify minimum aggregate size, if needed
    *-----------------------------------------------------------------*/

   if (minAggrSize_ < (nullspaceDim_/blkSize)) 
      minAggrSize_ = nullspaceDim_ / blkSize;

   /*-----------------------------------------------------------------
    * perform coarsening
    *-----------------------------------------------------------------*/
  
   if ( initAggr == NULL ) coarsenLocal(Gmat, &naggr, &node2aggr);
   else 
   {
      blkSize = currNodeDofs_;
      naggr = initCount;
      node2aggr = new int[ALocalNRows];
      for ( i = 0; i < ALocalNRows; i++ ) node2aggr[i] = initAggr[i];
   }

   /*-----------------------------------------------------------------
    * clean up graph and clean up duplicate matrix if block size > 1
    *-----------------------------------------------------------------*/

   if ( initAggr == NULL )
   {
      if ( blkSize > 1 ) 
      {
         delete mli_A2mat;
         if ( saLabels_ != NULL && saLabels_[currLevel_] != NULL )
            delete [] localLabels;
      }
      ierr = hypre_ParCSRMatrixDestroy(Gmat);
      assert( !ierr );
   }

   /*-----------------------------------------------------------------
    * fetch the coarse grid information and instantiate P
    * If coarse grid size is below a given threshold, stop
    *-----------------------------------------------------------------*/

   PLocalNCols  = naggr * nullspaceDim_;
   MLI_Utils_GenPartition(comm, PLocalNCols, &partition);
   PStartCol    = partition[mypid];
   PGlobalNCols = partition[numProcs];
   free( partition );
   if ( PGlobalNCols/nullspaceDim_ <= minCoarseSize_ || 
        PGlobalNCols/nullspaceDim_ <= numProcs ) 
   {
      (*Pmat_out) = NULL;
      delete [] node2aggr;
      return 0.0;
   }
   PLocalNRows = ALocalNRows;
   PStartRow   = AStartRow;
   ierr = HYPRE_IJMatrixCreate(comm,PStartRow,PStartRow+PLocalNRows-1,
                          PStartCol,PStartCol+PLocalNCols-1,&IJPmat);
   ierr = HYPRE_IJMatrixSetObjectType(IJPmat, HYPRE_PARCSR);
   assert(!ierr);

   /*-----------------------------------------------------------------
    * expand the aggregation information if block size > 1 ==> eqn2aggr
    *-----------------------------------------------------------------*/

   if ( blkSize > 1 && initAggr == NULL )
   {
      eqn2aggr = new int[ALocalNRows];
      for ( i = 0; i < ALocalNRows; i++ )
         eqn2aggr[i] = node2aggr[i/blkSize];
      delete [] node2aggr;
   }
   else eqn2aggr = node2aggr;
 
   /*-----------------------------------------------------------------
    * construct the next set of labels for the next level
    *-----------------------------------------------------------------*/

   if ( saLabels_ != NULL && saLabels_[currLevel_] != NULL )
   {
      if ( (currLevel_+1) < maxLevels_ )
      {
         if ( saLabels_[currLevel_+1] != NULL ) 
            delete [] saLabels_[currLevel_+1];
         saLabels_[currLevel_+1] = new int[PLocalNCols];
         for ( i = 0; i < PLocalNCols; i++ ) saLabels_[currLevel_+1][i] = -1;
         for ( i = 0; i < naggr; i++ )
         {
            for ( j = 0; j < ALocalNRows; j++ )
               if ( eqn2aggr[j] == i ) break;
            for ( k = 0; k < nullspaceDim_; k++ )
               saLabels_[currLevel_+1][i*nullspaceDim_+k] = 
                                              saLabels_[currLevel_][j];
         }
         for ( i = 0; i < PLocalNCols; i++ ) 
            if ( saLabels_[currLevel_+1][i] < 0 ||
                 saLabels_[currLevel_+1][i] >= naggr ) 
               printf("saLabels[%d][%d] = %d (%d)\n",currLevel_+1,i,
                      saLabels_[currLevel_+1][i], naggr);
      }
   }

   /*-----------------------------------------------------------------
    * compute smoothing factor for the prolongation smoother
    *-----------------------------------------------------------------*/

   if ( (currLevel_ >= 0 && Pweight_ != 0.0) || 
        !strcmp(preSmoother_, "MLS") ||
        !strcmp(postSmoother_, "MLS") || initAggr != NULL )
   {
      MLI_Utils_ComputeExtremeRitzValues(Amat, ritzValues, 1);
      maxEigen = ritzValues[0];
      if ( mypid == 0 && outputLevel_ > 1 )
         printf("\tEstimated spectral radius of A = %e\n", maxEigen);
      assert ( maxEigen > 0.0 );
      alpha = Pweight_ / maxEigen;
   }

   /*-----------------------------------------------------------------
    * create a compact form for the null space vectors 
    * (get ready to perform QR on them)
    *-----------------------------------------------------------------*/

   P_vecs = new double*[nullspaceDim_];
   P_cols = new int[PLocalNRows];
   for (i = 0; i < nullspaceDim_; i++) P_vecs[i] = new double[PLocalNRows];
   for ( irow = 0; irow < PLocalNRows; irow++ )
   {
      if ( eqn2aggr[irow] >= 0 )
      {
         P_cols[irow] = PStartCol + eqn2aggr[irow] * nullspaceDim_;
         if ( nullspaceVec_ != NULL )
         {
            for ( j = 0; j < nullspaceDim_; j++ )
               P_vecs[j][irow] = nullspaceVec_[j*PLocalNRows+irow];
         }
         else
         {
            for ( j = 0; j < nullspaceDim_; j++ )
            {
               if ( irow % blkSize == j ) P_vecs[j][irow] = 1.0;
               else                       P_vecs[j][irow] = 0.0;
            }
         }
      }
      else
      {
         P_cols[irow] = -1;
         for ( j = 0; j < nullspaceDim_; j++ ) P_vecs[j][irow] = 0.0;
      }
   }

   /*-----------------------------------------------------------------
    * perform QR for null space
    *-----------------------------------------------------------------*/

   newNull = NULL;
   if ( PLocalNRows > 0 )
   {
      /* ------ count the size of each aggregate ------ */

      aggCntArray = new int[naggr];
      for ( i = 0; i < naggr; i++ ) aggCntArray[i] = 0;
      for ( irow = 0; irow < PLocalNRows; irow++ )
         if ( eqn2aggr[irow] >= 0 ) aggCntArray[eqn2aggr[irow]]++;
      maxAggSize = 0;
      for ( i = 0; i < naggr; i++ ) 
         if (aggCntArray[i] > maxAggSize) maxAggSize = aggCntArray[i];

      /* ------ register which equation is in which aggregate ------ */

      aggIndArray = new int*[naggr];
      for ( i = 0; i < naggr; i++ ) 
      {
         aggIndArray[i] = new int[aggCntArray[i]];
         aggCntArray[i] = 0;
      }
      for ( irow = 0; irow < PLocalNRows; irow++ )
      {
         index = eqn2aggr[irow];
         if ( index >= 0 )
            aggIndArray[index][aggCntArray[index]++] = irow;
      }

      /* ------ allocate storage for QR factorization ------ */

      qArray  = new double[maxAggSize * nullspaceDim_];
      rArray  = new double[nullspaceDim_ * nullspaceDim_];
      newNull = new double[naggr*nullspaceDim_*nullspaceDim_]; 

      /* ------ perform QR on each aggregate ------ */

      for ( i = 0; i < naggr; i++ ) 
      {
         aggSize = aggCntArray[i];

         if ( aggSize < nullspaceDim_ )
         {
            printf("Aggregation ERROR : underdetermined system in QR.\n");
            printf("            error on Proc %d\n", mypid);
            printf("            error on aggr %d (%d)\n", i, naggr);
            printf("            aggr size is %d\n", aggSize);
            exit(1);
         }
          
         /* ------ put data into the temporary array ------ */

         for ( j = 0; j < aggSize; j++ ) 
         {
            for ( k = 0; k < nullspaceDim_; k++ ) 
               qArray[aggSize*k+j] = P_vecs[k][aggIndArray[i][j]]; 
         }

         /* ------ call QR function ------ */

#if 0
         if ( mypid == 0 )
         {
            for ( j = 0; j < aggSize; j++ ) 
            {
               printf("%5d : (size=%d)\n", aggIndArray[i][j], aggSize);
               for ( k = 0; k < nullspaceDim_; k++ ) 
                  printf("%10.3e ", qArray[aggSize*k+j]);
               printf("\n");
            }
         }
#endif
         info = MLI_Utils_QR(qArray, rArray, aggSize, nullspaceDim_); 
         if (info != 0)
         {
            printf("%4d : Aggregation WARNING : QR returned a non-zero for\n",
                   mypid);
            printf("  aggregate %d, size = %d, info = %d\n",i,aggSize,info);
#if 1
/*
            for ( j = 0; j < aggSize; j++ ) 
            {
               for ( k = 0; k < nullspaceDim_; k++ ) 
                  qArray[aggSize*k+j] = P_vecs[k][aggIndArray[i][j]]; 
            }
*/
            for ( j = 0; j < aggSize; j++ ) 
            {
               printf("%5d : ", aggIndArray[i][j]);
               for ( k = 0; k < nullspaceDim_; k++ ) 
                  printf("%10.3e ", qArray[aggSize*k+j]);
               printf("\n");
            }
#endif
         }

         /* ------ after QR, put the R into the next null space ------ */

         for ( j = 0; j < nullspaceDim_; j++ )
            for ( k = 0; k < nullspaceDim_; k++ )
               newNull[i*nullspaceDim_+j+k*naggr*nullspaceDim_] = 
                         rArray[j+nullspaceDim_*k];

         /* ------ put the P to P_vecs ------ */

         for ( j = 0; j < aggSize; j++ )
         {
            for ( k = 0; k < nullspaceDim_; k++ )
            {
               index = aggIndArray[i][j];
               P_vecs[k][index] = qArray[ k*aggSize + j ];
            }
         } 
      }
      for ( i = 0; i < naggr; i++ ) delete [] aggIndArray[i];
      delete [] aggIndArray;
      delete [] aggCntArray;
      delete [] qArray;
      delete [] rArray;
   }
   if ( nullspaceVec_ != NULL ) delete [] nullspaceVec_;
   nullspaceVec_ = newNull;
   currNodeDofs_ = nullspaceDim_;

   /*-----------------------------------------------------------------
    * if damping factor for prolongator smoother = 0
    *-----------------------------------------------------------------*/

// if ( currLevel_ == 0 || Pweight_ == 0.0 )
   if ( Pweight_ == 0.0 )
   {
      /*--------------------------------------------------------------
       * create and initialize Pmat 
       *--------------------------------------------------------------*/

      rowLengths = new int[PLocalNRows];
      for ( i = 0; i < PLocalNRows; i++ ) rowLengths[i] = nullspaceDim_;
      ierr = HYPRE_IJMatrixSetRowSizes(IJPmat, rowLengths);
      ierr = HYPRE_IJMatrixInitialize(IJPmat);
      assert(!ierr);
      delete [] rowLengths;

      /*-----------------------------------------------------------------
       * load and assemble Pmat 
       *-----------------------------------------------------------------*/

      colInd = new int[nullspaceDim_];
      colVal = new double[nullspaceDim_];
      for ( irow = 0; irow < PLocalNRows; irow++ )
      {
         if ( P_cols[irow] >= 0 )
         {
            nzcnt = 0;
            for ( j = 0; j < nullspaceDim_; j++ )
            {
               if ( P_vecs[j][irow] != 0.0 )
               {
                  colInd[nzcnt] = P_cols[irow] + j;
                  colVal[nzcnt++] = P_vecs[j][irow];
               }
            }
            rowNum = PStartRow + irow;
            HYPRE_IJMatrixSetValues(IJPmat, 1, &nzcnt, 
                             (const int *) &rowNum, (const int *) colInd, 
                             (const double *) colVal);
         }
      }
      ierr = HYPRE_IJMatrixAssemble(IJPmat);
      assert( !ierr );
      HYPRE_IJMatrixGetObject(IJPmat, (void **) &Pmat);
      hypre_MatvecCommPkgCreate((hypre_ParCSRMatrix *) Pmat);
      comm_pkg = hypre_ParCSRMatrixCommPkg(Amat);
      if (!comm_pkg) hypre_MatvecCommPkgCreate(Amat);
      HYPRE_IJMatrixSetObjectType(IJPmat, -1);
      HYPRE_IJMatrixDestroy( IJPmat );
      delete [] colInd;
      delete [] colVal;
   }

   /*-----------------------------------------------------------------
    * form prolongator by P = (I - alpha A) tentP
    *-----------------------------------------------------------------*/

   else
   {
      MLI_Matrix_FormJacobi(mli_Amat, alpha, &mli_Jmat);
      Jmat = (hypre_ParCSRMatrix *) mli_Jmat->getMatrix();
      rowLengths = new int[PLocalNRows];
      for ( i = 0; i < PLocalNRows; i++ ) rowLengths[i] = nullspaceDim_;
      ierr = HYPRE_IJMatrixSetRowSizes(IJPmat, rowLengths);
      ierr = HYPRE_IJMatrixInitialize(IJPmat);
      assert(!ierr);
      delete [] rowLengths;
      colInd = new int[nullspaceDim_];
      colVal = new double[nullspaceDim_];
      for ( irow = 0; irow < PLocalNRows; irow++ )
      {
         if ( P_cols[irow] >= 0 )
         {
            nzcnt = 0;
            for ( j = 0; j < nullspaceDim_; j++ )
            {
               if ( P_vecs[j][irow] != 0.0 )
               {
                  colInd[nzcnt] = P_cols[irow] + j;
                  colVal[nzcnt++] = P_vecs[j][irow];
               }
            }
            rowNum = PStartRow + irow;
            HYPRE_IJMatrixSetValues(IJPmat, 1, &nzcnt, 
                             (const int *) &rowNum, (const int *) colInd, 
                             (const double *) colVal);
         }
      }
      ierr = HYPRE_IJMatrixAssemble(IJPmat);
      assert( !ierr );
      HYPRE_IJMatrixGetObject(IJPmat, (void **) &Pmat2);
      HYPRE_IJMatrixSetObjectType(IJPmat, -1);
      HYPRE_IJMatrixDestroy( IJPmat );
      delete [] colInd;
      delete [] colVal;
      Pmat = hypre_ParMatmul( Jmat, Pmat2);
      hypre_ParCSRMatrixOwnsRowStarts(Jmat) = 0; 
      hypre_ParCSRMatrixOwnsColStarts(Pmat2) = 0;
      hypre_ParCSRMatrixDestroy(Pmat2);
      delete mli_Jmat;
   }

   /*-----------------------------------------------------------------
    * clean up
    *-----------------------------------------------------------------*/

   if ( P_cols != NULL ) delete [] P_cols;
   if ( P_vecs != NULL ) 
   {
      for (i = 0; i < nullspaceDim_; i++) 
         if ( P_vecs[i] != NULL ) delete [] P_vecs[i];
      delete [] P_vecs;
   }
   delete [] eqn2aggr;

   /*-----------------------------------------------------------------
    * set up and return the Pmat 
    *-----------------------------------------------------------------*/

   func_ptr = new MLI_Function();
   MLI_Utils_HypreParCSRMatrixGetDestroyFunc(func_ptr);
   sprintf(paramString, "HYPRE_ParCSR" ); 
   mli_Pmat = new MLI_Matrix( Pmat, paramString, func_ptr );
   (*Pmat_out) = mli_Pmat;
   delete func_ptr;
   return maxEigen;
}

/* ********************************************************************* *
 * local coarsening scheme (Given a graph, aggregate on the local subgraph)
 * --------------------------------------------------------------------- */

int MLI_Method_AMGSA::coarsenLocal(hypre_ParCSRMatrix *hypre_graph,
                                   int *mliAggrLeng, int **mliAggrArray)
{
   MPI_Comm  comm;
   int       mypid, numProcs, *partition, startRow, endRow;
   int       localNRows, naggr=0, *node2aggr, *aggrSizes, nUndone;
   int       irow, icol, colNum, rowNum, rowLeng, *cols, global_nrows;
   int       *nodeStat, selectFlag, nSelected=0, nNotSelected=0, count;
   int       ibuf[2], itmp[2];

   /*-----------------------------------------------------------------
    * fetch machine and matrix parameters
    *-----------------------------------------------------------------*/

   comm = hypre_ParCSRMatrixComm(hypre_graph);
   MPI_Comm_rank(comm,&mypid);
   MPI_Comm_size(comm,&numProcs);
   HYPRE_ParCSRMatrixGetRowPartitioning((HYPRE_ParCSRMatrix) hypre_graph, 
                                        &partition);
   startRow = partition[mypid];
   endRow   = partition[mypid+1] - 1;
   free( partition );
   localNRows = endRow - startRow + 1;
   MPI_Allreduce(&localNRows, &global_nrows, 1, MPI_INT, MPI_SUM, comm);
   if ( mypid == 0 && outputLevel_ > 1 )
   {
      printf("\t*** Aggregation(U) : total nodes to aggregate = %d\n",
             global_nrows);
   }
   if ( nullspaceDim_ / currNodeDofs_ >= minAggrSize_ )
      minAggrSize_ = nullspaceDim_ / currNodeDofs_ + 1;

   /*-----------------------------------------------------------------
    * this array is used to determine which row has been aggregated
    *-----------------------------------------------------------------*/

   if ( localNRows > 0 )
   {
      node2aggr = new int[localNRows];
      aggrSizes = new int[localNRows];
      nodeStat  = new int[localNRows];
      for ( irow = 0; irow < localNRows; irow++ ) 
      {
         aggrSizes[irow] = 0;
         node2aggr[irow] = -1;
         nodeStat[irow] = MLI_METHOD_AMGSA_READY;
         rowNum = startRow + irow;
         hypre_ParCSRMatrixGetRow(hypre_graph,rowNum,&rowLeng,NULL,NULL);
         if (rowLeng <= 0) 
         {
            nodeStat[irow] = MLI_METHOD_AMGSA_NOTSELECTED;
            nNotSelected++;
         }
         hypre_ParCSRMatrixRestoreRow(hypre_graph,rowNum,&rowLeng,NULL,NULL);
      }
   }
   else node2aggr = aggrSizes = nodeStat = NULL;

   /*-----------------------------------------------------------------
    * Phase 1 : form aggregates
    *-----------------------------------------------------------------*/

   for ( irow = 0; irow < localNRows; irow++ )
   {
      if ( nodeStat[irow] == MLI_METHOD_AMGSA_READY )
      {
         rowNum = startRow + irow;
         hypre_ParCSRMatrixGetRow(hypre_graph,rowNum,&rowLeng,&cols,NULL);
         selectFlag = 1;
         count      = 1;
         for ( icol = 0; icol < rowLeng; icol++ )
         {
            colNum = cols[icol] - startRow;
            if ( colNum >= 0 && colNum < localNRows )
            {
               if ( nodeStat[colNum] != MLI_METHOD_AMGSA_READY )
               {
                  selectFlag = 0;
                  break;
               }
               else count++;
            }
         }
         if ( selectFlag == 1 && count >= minAggrSize_ )
         {
            nSelected++;
            node2aggr[irow]  = naggr;
            aggrSizes[naggr] = 1;
            nodeStat[irow]  = MLI_METHOD_AMGSA_SELECTED;
            for ( icol = 0; icol < rowLeng; icol++ )
            {
               colNum = cols[icol] - startRow;
               if ( colNum >= 0 && colNum < localNRows )
               {
                  node2aggr[colNum] = naggr;
                  nodeStat[colNum] = MLI_METHOD_AMGSA_SELECTED;
                  aggrSizes[naggr]++;
                  nSelected++;
               }
            }
            naggr++;
         }
         hypre_ParCSRMatrixRestoreRow(hypre_graph,rowNum,&rowLeng,&cols,NULL);
      }
   }
   itmp[0] = naggr;
   itmp[1] = nSelected;
   if (outputLevel_ > 1) MPI_Allreduce(itmp, ibuf, 2, MPI_INT, MPI_SUM, comm);
   if ( mypid == 0 && outputLevel_ > 1 )
   {
      printf("\t*** Aggregation(U) P1 : no. of aggregates     = %d\n",ibuf[0]);
      printf("\t*** Aggregation(U) P1 : no. nodes aggregated  = %d\n",ibuf[1]);
   }

   /*-----------------------------------------------------------------
    * Phase 2 : put the rest into one of the existing aggregates
    *-----------------------------------------------------------------*/

   if ( (nSelected+nNotSelected) < localNRows )
   {
      for ( irow = 0; irow < localNRows; irow++ )
      {
         if ( nodeStat[irow] == MLI_METHOD_AMGSA_READY )
         {
            rowNum = startRow + irow;
            hypre_ParCSRMatrixGetRow(hypre_graph,rowNum,&rowLeng,&cols,NULL);
            for ( icol = 0; icol < rowLeng; icol++ )
            {
               colNum = cols[icol] - startRow;
               if ( colNum >= 0 && colNum < localNRows )
               {
                  if ( nodeStat[colNum] == MLI_METHOD_AMGSA_SELECTED )
                  {
                     node2aggr[irow] = node2aggr[colNum];
                     nodeStat[irow] = MLI_METHOD_AMGSA_PENDING;
                     aggrSizes[node2aggr[colNum]]++;
                     break;
                  }
               }
            }
            hypre_ParCSRMatrixRestoreRow(hypre_graph,rowNum,&rowLeng,&cols,
                                         NULL);
         }
      }
      for ( irow = 0; irow < localNRows; irow++ )
      {
         if ( nodeStat[irow] == MLI_METHOD_AMGSA_PENDING )
         {
            nodeStat[irow] = MLI_METHOD_AMGSA_SELECTED;
            nSelected++;
         }
      } 
   }
   itmp[0] = naggr;
   itmp[1] = nSelected;
   if (outputLevel_ > 1) MPI_Allreduce(itmp,ibuf,2,MPI_INT,MPI_SUM,comm);
   if ( mypid == 0 && outputLevel_ > 1 )
   {
      printf("\t*** Aggregation(U) P2 : no. of aggregates     = %d\n",ibuf[0]);
      printf("\t*** Aggregation(U) P2 : no. nodes aggregated  = %d\n",ibuf[1]);
   }

   /*-----------------------------------------------------------------
    * Phase 3 : form aggregates for all other rows
    *-----------------------------------------------------------------*/

   if ( (nSelected+nNotSelected) < localNRows )
   {
      for ( irow = 0; irow < localNRows; irow++ )
      {
         if ( nodeStat[irow] == MLI_METHOD_AMGSA_READY )
         {
            rowNum = startRow + irow;
            hypre_ParCSRMatrixGetRow(hypre_graph,rowNum,&rowLeng,&cols,NULL);
            count = 1;
            for ( icol = 0; icol < rowLeng; icol++ )
            {
               colNum = cols[icol] - startRow;
               if ( colNum >= 0 && colNum < localNRows )
               {
                  if ( nodeStat[colNum] == MLI_METHOD_AMGSA_READY ) count++;
               }
            }
            if ( count > 1 && count >= minAggrSize_ ) 
            {
               node2aggr[irow]  = naggr;
               nodeStat[irow]  = MLI_METHOD_AMGSA_SELECTED;
               aggrSizes[naggr] = 1;
               nSelected++;
               for ( icol = 0; icol < rowLeng; icol++ )
               {
                  colNum = cols[icol] - startRow;
                  if ( colNum >= 0 && colNum < localNRows )
                  {
                     if ( nodeStat[colNum] == MLI_METHOD_AMGSA_READY )
                     {
                        nodeStat[colNum] = MLI_METHOD_AMGSA_SELECTED;
                        node2aggr[colNum] = naggr;
                        aggrSizes[naggr]++;
                        nSelected++;
                     }
                  }
               }
               naggr++;
            }
            hypre_ParCSRMatrixRestoreRow(hypre_graph,rowNum,&rowLeng,&cols,
                                         NULL);
         }
      }
   }
   itmp[0] = naggr;
   itmp[1] = nSelected;
   if (outputLevel_ > 1) MPI_Allreduce(itmp,ibuf,2,MPI_INT,MPI_SUM,comm);
   if ( mypid == 0 && outputLevel_ > 1 )
   {
      printf("\t*** Aggregation(U) P3 : no. of aggregates     = %d\n",ibuf[0]);
      printf("\t*** Aggregation(U) P3 : no. nodes aggregated  = %d\n",ibuf[1]);
   }

   /*-----------------------------------------------------------------
    * Phase 4 : finally put all lone rows into some neighbor aggregate
    *-----------------------------------------------------------------*/

   if ( (nSelected+nNotSelected) < localNRows )
   {
      for ( irow = 0; irow < localNRows; irow++ )
      {
         if ( nodeStat[irow] == MLI_METHOD_AMGSA_READY )
         {
            rowNum = startRow + irow;
            hypre_ParCSRMatrixGetRow(hypre_graph,rowNum,&rowLeng,&cols,NULL);
            for ( icol = 0; icol < rowLeng; icol++ )
            {
               colNum = cols[icol] - startRow;
               if ( colNum >= 0 && colNum < localNRows )
               {
                  if ( nodeStat[colNum] == MLI_METHOD_AMGSA_SELECTED )
                  {
                     node2aggr[irow] = node2aggr[colNum];
                     nodeStat[irow] = MLI_METHOD_AMGSA_SELECTED;
                     aggrSizes[node2aggr[colNum]]++;
                     nSelected++;
                     break;
                  }
               }
            }
            hypre_ParCSRMatrixRestoreRow(hypre_graph,rowNum,&rowLeng,&cols,
                                         NULL);
         }
      }
   }
   itmp[0] = naggr;
   itmp[1] = nSelected;
   if ( outputLevel_ > 1 ) MPI_Allreduce(itmp,ibuf,2,MPI_INT,MPI_SUM,comm);
   if ( mypid == 0 && outputLevel_ > 1 )
   {
      printf("\t*** Aggregation(U) P4 : no. of aggregates     = %d\n",ibuf[0]);
      printf("\t*** Aggregation(U) P4 : no. nodes aggregated  = %d\n",ibuf[1]);
   }
   nUndone = localNRows - nSelected - nNotSelected;
   if ( nUndone > 0 )
   {
      count = nUndone / minAggrSize_;
      if ( count == 0 ) count = 1;
      count += naggr;
      irow = icol = 0;
      while ( nUndone > 0 )
      {
         if ( nodeStat[irow] == MLI_METHOD_AMGSA_READY )
         {
            node2aggr[irow] = naggr;
            nodeStat[irow] = MLI_METHOD_AMGSA_SELECTED;
            nUndone--;
            nSelected++;
            icol++;
            if ( icol >= minAggrSize_ && naggr < count-1 ) 
            {
               icol = 0;
               naggr++;
            }
         }
         irow++;
      }
      naggr = count;
   }
   itmp[0] = naggr;
   itmp[1] = nSelected;
   if ( outputLevel_ > 1 ) MPI_Allreduce(itmp,ibuf,2,MPI_INT,MPI_SUM,comm);
   if ( mypid == 0 && outputLevel_ > 1 )
   {
      printf("\t*** Aggregation(U) P5 : no. of aggregates     = %d\n",ibuf[0]);
      printf("\t*** Aggregation(U) P5 : no. nodes aggregated  = %d\n",ibuf[1]);
   }

   /*-----------------------------------------------------------------
    * diagnostics
    *-----------------------------------------------------------------*/

   if ( (nSelected+nNotSelected) < localNRows )
   {
      for ( irow = 0; irow < localNRows; irow++ )
      {
         if ( nodeStat[irow] == MLI_METHOD_AMGSA_READY )
         {
            rowNum = startRow + irow;
#ifdef MLI_DEBUG_DETAILED
            printf("%5d : unaggregated node = %8d\n", mypid, rowNum);
#endif
            hypre_ParCSRMatrixGetRow(hypre_graph,rowNum,&rowLeng,&cols,NULL);
            for ( icol = 0; icol < rowLeng; icol++ )
            {
               colNum = cols[icol];
               printf("ERROR : neighbor of unselected node %9d = %9d\n", 
                     rowNum, colNum);
            }
         }
      }
   }

   /*-----------------------------------------------------------------
    * clean up and initialize the output arrays 
    *-----------------------------------------------------------------*/

   if ( localNRows > 0 ) delete [] aggrSizes; 
   if ( localNRows > 0 ) delete [] nodeStat; 
   if ( localNRows == 1 && naggr == 0 )
   {
      node2aggr[0] = 0;
      naggr = 1;
   }
   (*mliAggrArray) = node2aggr;
   (*mliAggrLeng)  = naggr;
   return 0;
}

/***********************************************************************
 * form graph from matrix (internal subroutine)
 * ------------------------------------------------------------------- */

int MLI_Method_AMGSA::formLocalGraph( hypre_ParCSRMatrix *Amat,
                                      hypre_ParCSRMatrix **graph_in,
                                      int *localLabels)
{
   HYPRE_IJMatrix     IJGraph;
   hypre_CSRMatrix    *AdiagBlock;
   hypre_ParCSRMatrix *graph;
   MPI_Comm           comm;
   int                i, j, jj, index, mypid, *partition;
   int                startRow, endRow, *rowLengths;
   int                *AdiagRPtr, *AdiagCols, AdiagNRows, length;
   int                irow, maxRowNnz, ierr, *colInd, labeli, labelj;
   double             *diagData=NULL, *colVal;
   double             *AdiagVals, dcomp1, dcomp2, epsilon;

   /*-----------------------------------------------------------------
    * fetch machine and matrix parameters
    *-----------------------------------------------------------------*/

   assert( Amat != NULL );
   comm = hypre_ParCSRMatrixComm(Amat);
   MPI_Comm_rank(comm,&mypid);

   HYPRE_ParCSRMatrixGetRowPartitioning((HYPRE_ParCSRMatrix) Amat,&partition);
   startRow = partition[mypid];
   endRow   = partition[mypid+1] - 1;
   free( partition );
   AdiagBlock = hypre_ParCSRMatrixDiag(Amat);
   AdiagNRows = hypre_CSRMatrixNumRows(AdiagBlock);
   AdiagRPtr  = hypre_CSRMatrixI(AdiagBlock);
   AdiagCols  = hypre_CSRMatrixJ(AdiagBlock);
   AdiagVals  = hypre_CSRMatrixData(AdiagBlock);
   
   /*-----------------------------------------------------------------
    * construct the diagonal array (diagData) 
    *-----------------------------------------------------------------*/

   if ( threshold_ > 0.0 )
   {
      diagData = new double[AdiagNRows];

#define HYPRE_SMP_PRIVATE irow,j
#include "utilities/hypre_smp_forloop.h"
      for (irow = 0; irow < AdiagNRows; irow++)
      {
         for (j = AdiagRPtr[irow]; j < AdiagRPtr[irow+1]; j++)
         {
            if ( AdiagCols[j] == irow )
            {
               diagData[irow] = AdiagVals[j];
               break;
            }
         }
      }
   }

   /*-----------------------------------------------------------------
    * initialize the graph
    *-----------------------------------------------------------------*/

   ierr = HYPRE_IJMatrixCreate(comm, startRow, endRow, startRow,
                               endRow, &IJGraph);
   ierr = HYPRE_IJMatrixSetObjectType(IJGraph, HYPRE_PARCSR);
   assert(!ierr);

   /*-----------------------------------------------------------------
    * find and initialize the length of each row in the graph
    *-----------------------------------------------------------------*/

   epsilon = threshold_;
   for ( i = 0; i < currLevel_; i++ ) epsilon *= 0.5;
   if ( mypid == 0 && outputLevel_ > 1 )
   {
      printf("\t*** Aggregation(U) : strength threshold       = %8.2e\n",
             epsilon);
   }
   epsilon = epsilon * epsilon;
   rowLengths = new int[AdiagNRows];

#define HYPRE_SMP_PRIVATE irow,j,jj,index,dcomp1,dcomp2
#include "utilities/hypre_smp_forloop.h"
   for ( irow = 0; irow < AdiagNRows; irow++ )
   {
      rowLengths[irow] = 0;
      index = startRow + irow;
      if ( localLabels != NULL ) labeli = localLabels[irow];
      else                       labeli = 0;
      if ( epsilon > 0.0 )
      {
         for (j = AdiagRPtr[irow]; j < AdiagRPtr[irow+1]; j++)
         {
            jj = AdiagCols[j];
            if ( localLabels != NULL ) labelj = localLabels[jj];
            else                       labelj = 0;
            if ( jj != irow )
            {
               dcomp1 = AdiagVals[j] * AdiagVals[j];
               if (dcomp1 > 0.0 && labeli == labelj) rowLengths[irow]++;
            }
         }
      }
      else 
      {
         for (j = AdiagRPtr[irow]; j < AdiagRPtr[irow+1]; j++)
         {
            jj = AdiagCols[j];
            if ( localLabels != NULL ) labelj = localLabels[jj];
            else                       labelj = 0;
            if ( jj != irow && AdiagVals[j] != 0.0 && labeli == labelj )
               rowLengths[irow]++;
         }
      }
   }
   maxRowNnz = 0;
   for ( irow = 0; irow < AdiagNRows; irow++ )
   {
      if ( rowLengths[irow] > maxRowNnz ) maxRowNnz = rowLengths[irow];
   }
   ierr = HYPRE_IJMatrixSetRowSizes(IJGraph, rowLengths);
   ierr = HYPRE_IJMatrixInitialize(IJGraph);
   assert(!ierr);
   delete [] rowLengths;

   /*-----------------------------------------------------------------
    * load and assemble the graph
    *-----------------------------------------------------------------*/

   colInd = new int[maxRowNnz];
   colVal = new double[maxRowNnz];
   for ( irow = 0; irow < AdiagNRows; irow++ )
   {
      length = 0;
      index  = startRow + irow;
      if ( localLabels != NULL ) labeli = localLabels[irow];
      else                       labeli = 0;
      if ( epsilon > 0.0 )
      {
         for (j = AdiagRPtr[irow]; j < AdiagRPtr[irow+1]; j++)
         {
            jj = AdiagCols[j];
            if ( localLabels != NULL ) labelj = localLabels[jj];
            else                       labelj = 0;
            if ( jj != irow )
            {
               dcomp1 = AdiagVals[j] * AdiagVals[j];
               if ( dcomp1 > 0.0 )
               {
                  dcomp2 = habs(diagData[irow] * diagData[jj]);
                  if ( (dcomp2 >= epsilon * dcomp1) && (labeli == labelj) ) 
                  {
                     colVal[length] = dcomp2 / dcomp1;
                     colInd[length++] = jj + startRow;
                  }
               }
            }
         }
      }
      else 
      {
         for (j = AdiagRPtr[irow]; j < AdiagRPtr[irow+1]; j++)
         {
            jj = AdiagCols[j];
            if ( localLabels != NULL ) labelj = localLabels[jj];
            else                       labelj = 0;
            if ( jj != irow )
            {
               if (AdiagVals[j] != 0.0 && (labeli == labelj)) 
               {
                  colVal[length] = AdiagVals[j];
                  colInd[length++] = jj + startRow;
               }
            }
         }
      }
      HYPRE_IJMatrixSetValues(IJGraph, 1, &length, (const int *) &index, 
                              (const int *) colInd, (const double *) colVal);
   }
   ierr = HYPRE_IJMatrixAssemble(IJGraph);
   assert(!ierr);

   /*-----------------------------------------------------------------
    * return the graph and clean up
    *-----------------------------------------------------------------*/

   HYPRE_IJMatrixGetObject(IJGraph, (void **) &graph);
   HYPRE_IJMatrixSetObjectType(IJGraph, -1);
   HYPRE_IJMatrixDestroy(IJGraph);
   (*graph_in) = graph;
   delete [] colInd;
   delete [] colVal;
   if ( threshold_ > 0.0 ) delete [] diagData;
   return 0;
}

#undef MLI_METHOD_AMGSA_READY
#undef MLI_METHOD_AMGSA_SELECTED
#undef MLI_METHOD_AMGSA_PENDING
#undef MLI_METHOD_AMGSA_NOTSELECTED

