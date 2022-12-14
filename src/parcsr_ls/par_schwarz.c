/******************************************************************************
 * Copyright (c) 1998 Lawrence Livermore National Security, LLC and other
 * HYPRE Project Developers. See the top-level COPYRIGHT file for details.
 *
 * SPDX-License-Identifier: (Apache-2.0 OR MIT)
 ******************************************************************************/

/******************************************************************************
 *
 * Schwarz functions
 *
 *****************************************************************************/

#include "_hypre_parcsr_ls.h"
#include "schwarz.h"

/*--------------------------------------------------------------------------
 * hypre_SchwarzCreate
 *--------------------------------------------------------------------------*/

void *
hypre_SchwarzCreate( void )
{
   hypre_SchwarzData *schwarz_data;

   HYPRE_Int      variant;
   HYPRE_Int      domain_type;
   HYPRE_Int      overlap;
   HYPRE_Int      num_functions;
   HYPRE_Int      use_nonsymm;
   HYPRE_Real     relax_weight;

   /*-----------------------------------------------------------------------
    * Setup default values for parameters
    *-----------------------------------------------------------------------*/

   /* setup params */
   variant = 0;  /* multiplicative Schwarz */
   overlap = 1;  /* minimal overlap */
   domain_type = 2; /* domains generated by agglomeration */
   num_functions = 1;
   use_nonsymm = 0;
   relax_weight = 1.0;

   schwarz_data = hypre_CTAlloc(hypre_SchwarzData, 1, HYPRE_MEMORY_HOST);

   hypre_SchwarzSetVariant(schwarz_data, variant);
   hypre_SchwarzSetDomainType(schwarz_data, domain_type);
   hypre_SchwarzSetOverlap(schwarz_data, overlap);
   hypre_SchwarzSetNumFunctions(schwarz_data, num_functions);
   hypre_SchwarzSetNonSymm(schwarz_data, use_nonsymm);
   hypre_SchwarzSetRelaxWeight(schwarz_data, relax_weight);

   hypre_SchwarzDataDomainStructure(schwarz_data) = NULL;
   hypre_SchwarzDataABoundary(schwarz_data) = NULL;
   hypre_SchwarzDataScale(schwarz_data) = NULL;
   hypre_SchwarzDataVtemp(schwarz_data) = NULL;
   hypre_SchwarzDataDofFunc(schwarz_data) = NULL;

   return (void *) schwarz_data;
}

/*--------------------------------------------------------------------------
 * hypre_SchwarzDestroy
 *--------------------------------------------------------------------------*/

HYPRE_Int
hypre_SchwarzDestroy( void *data )
{
   hypre_SchwarzData  *schwarz_data = (hypre_SchwarzData*) data;

   if (hypre_SchwarzDataScale(schwarz_data))
   {
      hypre_TFree(hypre_SchwarzDataScale(schwarz_data), HYPRE_MEMORY_HOST);
   }
   if (hypre_SchwarzDataDofFunc(schwarz_data))
   {
      hypre_TFree(hypre_SchwarzDataDofFunc(schwarz_data), HYPRE_MEMORY_HOST);
   }
   hypre_CSRMatrixDestroy(hypre_SchwarzDataDomainStructure(schwarz_data));
   if (hypre_SchwarzDataVariant(schwarz_data) == 3)
   {
      hypre_CSRMatrixDestroy(hypre_SchwarzDataABoundary(schwarz_data));
   }
   hypre_ParVectorDestroy(hypre_SchwarzDataVtemp(schwarz_data));

   if (hypre_SchwarzDataPivots(schwarz_data))
   {
      hypre_TFree(hypre_SchwarzDataPivots(schwarz_data), HYPRE_MEMORY_HOST);
   }


   hypre_TFree(schwarz_data, HYPRE_MEMORY_HOST);
   return hypre_error_flag;

}

HYPRE_Int
hypre_SchwarzSetup(void               *schwarz_vdata,
                   hypre_ParCSRMatrix *A,
                   hypre_ParVector    *f,
                   hypre_ParVector    *u         )
{

   hypre_SchwarzData   *schwarz_data = (hypre_SchwarzData*) schwarz_vdata;
   HYPRE_Int *dof_func;
   HYPRE_Real *scale;
   hypre_CSRMatrix *domain_structure;
   hypre_CSRMatrix *A_boundary;
   hypre_ParVector *Vtemp;

   HYPRE_Int *pivots = NULL;

   HYPRE_Int variant = hypre_SchwarzDataVariant(schwarz_data);
   HYPRE_Int domain_type = hypre_SchwarzDataDomainType(schwarz_data);
   HYPRE_Int overlap = hypre_SchwarzDataOverlap(schwarz_data);
   HYPRE_Int num_functions = hypre_SchwarzDataNumFunctions(schwarz_data);
   HYPRE_Real relax_weight = hypre_SchwarzDataRelaxWeight(schwarz_data);
   HYPRE_Int use_nonsymm = hypre_SchwarzDataUseNonSymm(schwarz_data);


   dof_func = hypre_SchwarzDataDofFunc(schwarz_data);

   Vtemp = hypre_ParVectorCreate(hypre_ParCSRMatrixComm(A),
                                 hypre_ParCSRMatrixGlobalNumRows(A),
                                 hypre_ParCSRMatrixRowStarts(A));
   hypre_ParVectorInitialize(Vtemp);
   hypre_SchwarzDataVtemp(schwarz_data) = Vtemp;

   if (variant > 1)
   {
      hypre_ParAMGCreateDomainDof(A,
                                  domain_type, overlap,
                                  num_functions, dof_func,
                                  &domain_structure, &pivots, use_nonsymm);

      if (domain_structure)
      {
         if (variant == 2)
         {
            hypre_ParGenerateScale(A, domain_structure, relax_weight,
                                   &scale);
            hypre_SchwarzDataScale(schwarz_data) = scale;
         }
         else
         {
            hypre_ParGenerateHybridScale(A, domain_structure, &A_boundary, &scale);
            hypre_SchwarzDataScale(schwarz_data) = scale;
            if (hypre_CSRMatrixNumCols(hypre_ParCSRMatrixOffd(A)))
            {
               hypre_SchwarzDataABoundary(schwarz_data) = A_boundary;
            }
            else
            {
               hypre_SchwarzDataABoundary(schwarz_data) = NULL;
            }
         }
      }
   }
   else
   {
      hypre_AMGCreateDomainDof (hypre_ParCSRMatrixDiag(A),
                                domain_type, overlap,
                                num_functions, dof_func,
                                &domain_structure, &pivots, use_nonsymm);
      if (domain_structure)
      {
         if (variant == 1)
         {
            hypre_GenerateScale(domain_structure,
                                hypre_CSRMatrixNumRows(hypre_ParCSRMatrixDiag(A)),
                                relax_weight, &scale);
            hypre_SchwarzDataScale(schwarz_data) = scale;
         }
      }
   }

   hypre_SchwarzDataDomainStructure(schwarz_data) = domain_structure;
   hypre_SchwarzDataPivots(schwarz_data) = pivots;

   return hypre_error_flag;

}

/*--------------------------------------------------------------------
 * hypre_SchwarzSolve
 *--------------------------------------------------------------------*/

HYPRE_Int
hypre_SchwarzSolve(void               *schwarz_vdata,
                   hypre_ParCSRMatrix *A,
                   hypre_ParVector    *f,
                   hypre_ParVector    *u         )
{
   hypre_SchwarzData   *schwarz_data = (hypre_SchwarzData*) schwarz_vdata;

   hypre_CSRMatrix *domain_structure =
      hypre_SchwarzDataDomainStructure(schwarz_data);
   hypre_CSRMatrix *A_boundary = hypre_SchwarzDataABoundary(schwarz_data);
   HYPRE_Real *scale = hypre_SchwarzDataScale(schwarz_data);
   hypre_ParVector *Vtemp = hypre_SchwarzDataVtemp(schwarz_data);
   HYPRE_Int variant = hypre_SchwarzDataVariant(schwarz_data);
   HYPRE_Real relax_wt = hypre_SchwarzDataRelaxWeight(schwarz_data);
   HYPRE_Int use_nonsymm = hypre_SchwarzDataUseNonSymm(schwarz_data);

   HYPRE_Int *pivots = hypre_SchwarzDataPivots(schwarz_data);

   if (domain_structure)
   {
      if (variant == 2)
      {
         hypre_ParAdSchwarzSolve(A, f, domain_structure, scale, u, Vtemp, pivots, use_nonsymm);
      }
      else if (variant == 3)
      {
         hypre_ParMPSchwarzSolve(A, A_boundary, f, domain_structure, u,
                                 relax_wt, scale, Vtemp, pivots, use_nonsymm);
      }
      else if (variant == 1)
      {
         hypre_AdSchwarzSolve(A, f, domain_structure, scale, u, Vtemp, pivots, use_nonsymm);
      }
      else if (variant == 4)
      {
         hypre_MPSchwarzFWSolve(A, hypre_ParVectorLocalVector(f),
                                domain_structure, u, relax_wt,
                                hypre_ParVectorLocalVector(Vtemp), pivots, use_nonsymm);
      }
      else
      {
         hypre_MPSchwarzSolve(A, hypre_ParVectorLocalVector(f),
                              domain_structure, u, relax_wt,
                              hypre_ParVectorLocalVector(Vtemp), pivots, use_nonsymm);
      }
   }

   return hypre_error_flag;
}
/*--------------------------------------------------------------------
 * hypre_SchwarzCFSolve
 *--------------------------------------------------------------------*/

HYPRE_Int
hypre_SchwarzCFSolve(void               *schwarz_vdata,
                     hypre_ParCSRMatrix *A,
                     hypre_ParVector    *f,
                     hypre_ParVector    *u,
                     HYPRE_Int *CF_marker,
                     HYPRE_Int rlx_pt)
{
   hypre_SchwarzData   *schwarz_data = (hypre_SchwarzData*) schwarz_vdata;

   hypre_CSRMatrix *domain_structure =
      hypre_SchwarzDataDomainStructure(schwarz_data);
   HYPRE_Real *scale = hypre_SchwarzDataScale(schwarz_data);
   hypre_ParVector *Vtemp = hypre_SchwarzDataVtemp(schwarz_data);
   HYPRE_Int variant = hypre_SchwarzDataVariant(schwarz_data);
   HYPRE_Real relax_wt = hypre_SchwarzDataRelaxWeight(schwarz_data);

   HYPRE_Int use_nonsymm = hypre_SchwarzDataUseNonSymm(schwarz_data);

   HYPRE_Int *pivots = hypre_SchwarzDataPivots(schwarz_data);

   if (variant == 1)
   {
      hypre_AdSchwarzCFSolve(A, f, domain_structure, scale, u, Vtemp,
                             CF_marker, rlx_pt, pivots, use_nonsymm);
   }
   else if (variant == 4)
   {
      hypre_MPSchwarzCFFWSolve(A, hypre_ParVectorLocalVector(f),
                               domain_structure, u, relax_wt,
                               hypre_ParVectorLocalVector(Vtemp),
                               CF_marker, rlx_pt, pivots, use_nonsymm);
   }
   else
   {
      hypre_MPSchwarzCFSolve(A, hypre_ParVectorLocalVector(f),
                             domain_structure, u, relax_wt,
                             hypre_ParVectorLocalVector(Vtemp),
                             CF_marker, rlx_pt, pivots, use_nonsymm);
   }

   return hypre_error_flag;
}

/*--------------------------------------------------------------------------
 * Routines to set various parameters
 *--------------------------------------------------------------------------*/

HYPRE_Int
hypre_SchwarzSetVariant( void *data, HYPRE_Int variant )
{

   hypre_SchwarzData  *schwarz_data = (hypre_SchwarzData*) data;

   hypre_SchwarzDataVariant(schwarz_data) = variant;
   return hypre_error_flag;

}

HYPRE_Int
hypre_SchwarzSetDomainType( void *data, HYPRE_Int domain_type )
{

   hypre_SchwarzData  *schwarz_data = (hypre_SchwarzData*) data;

   hypre_SchwarzDataDomainType(schwarz_data) = domain_type;
   return hypre_error_flag;

}

HYPRE_Int
hypre_SchwarzSetOverlap( void *data, HYPRE_Int overlap )
{

   hypre_SchwarzData  *schwarz_data = (hypre_SchwarzData*) data;

   hypre_SchwarzDataOverlap(schwarz_data) = overlap;

   return hypre_error_flag;
}

HYPRE_Int
hypre_SchwarzSetNumFunctions( void *data, HYPRE_Int num_functions )
{

   hypre_SchwarzData  *schwarz_data = (hypre_SchwarzData*) data;

   hypre_SchwarzDataNumFunctions(schwarz_data) = num_functions;

   return hypre_error_flag;
}

HYPRE_Int
hypre_SchwarzSetNonSymm( void *data, HYPRE_Int value )
{

   hypre_SchwarzData  *schwarz_data = (hypre_SchwarzData*) data;

   hypre_SchwarzDataUseNonSymm(schwarz_data) = value;

   return hypre_error_flag;

}

HYPRE_Int
hypre_SchwarzSetRelaxWeight( void *data, HYPRE_Real relax_weight )
{

   hypre_SchwarzData  *schwarz_data = (hypre_SchwarzData*) data;

   hypre_SchwarzDataRelaxWeight(schwarz_data) = relax_weight;

   return hypre_error_flag;
}

HYPRE_Int
hypre_SchwarzSetDomainStructure( void *data, hypre_CSRMatrix *domain_structure )
{

   hypre_SchwarzData  *schwarz_data = (hypre_SchwarzData*) data;

   hypre_SchwarzDataDomainStructure(schwarz_data) = domain_structure;

   return hypre_error_flag;
}

HYPRE_Int
hypre_SchwarzSetScale( void *data, HYPRE_Real *scale)
{

   hypre_SchwarzData  *schwarz_data = (hypre_SchwarzData*) data;

   hypre_SchwarzDataScale(schwarz_data) = scale;

   return hypre_error_flag;
}

HYPRE_Int
hypre_SchwarzReScale( void *data, HYPRE_Int size, HYPRE_Real value)
{

   HYPRE_Int i;
   HYPRE_Real *scale;
   hypre_SchwarzData  *schwarz_data = (hypre_SchwarzData*) data;

   scale = hypre_SchwarzDataScale(schwarz_data);
   for (i = 0; i < size; i++)
   {
      scale[i] *= value;
   }

   return hypre_error_flag;

}

HYPRE_Int
hypre_SchwarzSetDofFunc( void *data, HYPRE_Int *dof_func)
{

   hypre_SchwarzData  *schwarz_data = (hypre_SchwarzData*) data;

   hypre_SchwarzDataDofFunc(schwarz_data) = dof_func;

   return hypre_error_flag;
}
