/******************************************************************************
 * Copyright 1998-2019 Lawrence Livermore National Security, LLC and other
 * HYPRE Project Developers. See the top-level COPYRIGHT file for details.
 *
 * SPDX-License-Identifier: (Apache-2.0 OR MIT)
 ******************************************************************************/

/******************************************************************************
 *
 * HYPRE_SStructGraph interface
 *
 *****************************************************************************/

#include "_hypre_sstruct_mv.h"

HYPRE_Int
HYPRE_SStructGraphCreate( MPI_Comm             comm,
                          HYPRE_SStructGrid    grid,
                          HYPRE_SStructGraph  *graph_ptr )
{
   hypre_SStructGraph     *graph;
   HYPRE_Int               ndim   = hypre_SStructGridNDim(grid);
   HYPRE_Int               nparts = hypre_SStructGridNParts(grid);

   hypre_SStructStencil ***stencils;
   hypre_SStructPGrid     *pgrid;
   HYPRE_Int              *fem_nsparse;
   HYPRE_Int             **fem_sparse_i;
   HYPRE_Int             **fem_sparse_j;
   HYPRE_Int             **fem_entries;
   hypre_Box            ***Uvboxes;

   HYPRE_Int               d, part, var, nvars;

   graph = hypre_TAlloc(hypre_SStructGraph, 1, HYPRE_MEMORY_HOST);

   hypre_SStructGraphComm(graph) = comm;
   hypre_SStructGraphNDim(graph) = ndim;
   hypre_SStructGridRef(grid, &hypre_SStructGraphGrid(graph));
   hypre_SStructGridRef(grid, &hypre_SStructGraphDomGrid(graph));
   hypre_SStructGraphNParts(graph)  = nparts;

   stencils     = hypre_TAlloc(hypre_SStructStencil **, nparts, HYPRE_MEMORY_HOST);
   fem_nsparse  = hypre_TAlloc(HYPRE_Int,   nparts, HYPRE_MEMORY_HOST);
   fem_sparse_i = hypre_TAlloc(HYPRE_Int *, nparts, HYPRE_MEMORY_HOST);
   fem_sparse_j = hypre_TAlloc(HYPRE_Int *, nparts, HYPRE_MEMORY_HOST);
   fem_entries  = hypre_TAlloc(HYPRE_Int *, nparts, HYPRE_MEMORY_HOST);
   Uvboxes      = hypre_TAlloc(hypre_Box **, nparts, HYPRE_MEMORY_HOST);
   for (part = 0; part < nparts; part++)
   {
      pgrid = hypre_SStructGraphPGrid(graph, part);
      nvars = hypre_SStructPGridNVars(pgrid);

      /* Allocate pointers */
      stencils[part] = hypre_TAlloc(hypre_SStructStencil *, nvars, HYPRE_MEMORY_HOST);
      Uvboxes[part]  = hypre_TAlloc(hypre_Box *, nvars, HYPRE_MEMORY_HOST);

      /* Initialize pointers */
      fem_nsparse[part]  = 0;
      fem_sparse_i[part] = NULL;
      fem_sparse_j[part] = NULL;
      fem_entries[part]  = NULL;
      for (var = 0; var < nvars; var++)
      {
         stencils[part][var] = NULL;
         Uvboxes[part][var]  = hypre_BoxCreate(ndim);
         for (d = 0; d < ndim; d++)
         {
            hypre_BoxIMinD(Uvboxes[part][var], d) = HYPRE_INT_MAX/2;
            hypre_BoxIMaxD(Uvboxes[part][var], d) = HYPRE_INT_MIN/2;
         }
         for (d = ndim; d < HYPRE_MAXDIM; d++)
         {
            hypre_BoxIMinD(Uvboxes[part][var], d) = 0;
            hypre_BoxIMaxD(Uvboxes[part][var], d) = 0;
         }
      }
   }
   hypre_SStructGraphStencils(graph)    = stencils;
   hypre_SStructGraphFEMNSparse(graph)  = fem_nsparse;
   hypre_SStructGraphFEMSparseJ(graph)  = fem_sparse_i;
   hypre_SStructGraphFEMSparseI(graph)  = fem_sparse_j;
   hypre_SStructGraphFEMEntries(graph)  = fem_entries;

   hypre_SStructGraphNUVEntries(graph) = 0;
   hypre_SStructGraphIUVEntries(graph) = NULL;
   hypre_SStructGraphUVEntries(graph)  = NULL;
   hypre_SStructGraphUVESize(graph)    = 0;
   hypre_SStructGraphUEMaxSize(graph)  = 0;
   hypre_SStructGraphUVEOffsets(graph) = NULL;
   hypre_SStructGraphUVBoxes(graph)    = Uvboxes;

   hypre_SStructGraphRefCount(graph)   = 1;
   hypre_SStructGraphObjectType(graph) = HYPRE_SSTRUCT;

   hypre_SStructGraphEntries(graph)    = NULL;
   hypre_SStructNGraphEntries(graph)   = 0;
   hypre_SStructAGraphEntries(graph)   = 0;

   *graph_ptr = graph;

   return hypre_error_flag;
}

/*--------------------------------------------------------------------------
 *--------------------------------------------------------------------------*/

HYPRE_Int
HYPRE_SStructGraphDestroy( HYPRE_SStructGraph graph )
{
   HYPRE_Int               nparts;
   HYPRE_Int               nvars;
   hypre_SStructPGrid     *pgrid;
   hypre_SStructStencil ***stencils;
   HYPRE_Int              *fem_nsparse;
   HYPRE_Int             **fem_sparse_i;
   HYPRE_Int             **fem_sparse_j;
   HYPRE_Int             **fem_entries;
   HYPRE_Int               nUventries;
   HYPRE_Int              *iUventries;
   hypre_SStructUVEntry  **Uventries;
   hypre_SStructUVEntry   *Uventry;
   HYPRE_BigInt          **Uveoffsets;
   hypre_Box            ***Uvboxes;
   HYPRE_Int               part, var, i;

   if (graph)
   {
      hypre_SStructGraphRefCount(graph) --;
      if (hypre_SStructGraphRefCount(graph) == 0)
      {
         nparts = hypre_SStructGraphNParts(graph);

         /* FEM data */
         fem_nsparse  = hypre_SStructGraphFEMNSparse(graph);
         fem_sparse_i = hypre_SStructGraphFEMSparseJ(graph);
         fem_sparse_j = hypre_SStructGraphFEMSparseI(graph);
         fem_entries  = hypre_SStructGraphFEMEntries(graph);

         /* S-graph data */
         stencils = hypre_SStructGraphStencils(graph);

         /* U-graph data */
         nUventries = hypre_SStructGraphNUVEntries(graph);
         iUventries = hypre_SStructGraphIUVEntries(graph);
         Uventries  = hypre_SStructGraphUVEntries(graph);
         Uveoffsets = hypre_SStructGraphUVEOffsets(graph);
         Uvboxes    = hypre_SStructGraphUVBoxes(graph);

         for (part = 0; part < nparts; part++)
         {
            pgrid = hypre_SStructGraphPGrid(graph, part);
            nvars = hypre_SStructPGridNVars(pgrid);

            for (var = 0; var < nvars; var++)
            {
               HYPRE_SStructStencilDestroy(stencils[part][var]);
               hypre_BoxDestroy(Uvboxes[part][var]);
            }
            hypre_TFree(stencils[part], HYPRE_MEMORY_HOST);
            hypre_TFree(fem_sparse_i[part], HYPRE_MEMORY_HOST);
            hypre_TFree(fem_sparse_j[part], HYPRE_MEMORY_HOST);
            hypre_TFree(fem_entries[part], HYPRE_MEMORY_HOST);
            hypre_TFree(Uveoffsets[part], HYPRE_MEMORY_HOST);
            hypre_TFree(Uvboxes[part], HYPRE_MEMORY_HOST);
         }
         HYPRE_SStructGridDestroy(hypre_SStructGraphGrid(graph));
         HYPRE_SStructGridDestroy(hypre_SStructGraphDomGrid(graph));
         hypre_TFree(stencils, HYPRE_MEMORY_HOST);
         hypre_TFree(fem_nsparse, HYPRE_MEMORY_HOST);
         hypre_TFree(fem_sparse_i, HYPRE_MEMORY_HOST);
         hypre_TFree(fem_sparse_j, HYPRE_MEMORY_HOST);
         hypre_TFree(fem_entries, HYPRE_MEMORY_HOST);

         /* RDF: THREAD? */
         for (i = 0; i < nUventries; i++)
         {
            Uventry = Uventries[iUventries[i]];
            if (Uventry)
            {
               hypre_TFree(hypre_SStructUVEntryUEntries(Uventry), HYPRE_MEMORY_HOST);
               hypre_TFree(Uventry, HYPRE_MEMORY_HOST);
            }
            Uventries[iUventries[i]] = NULL;
         }
         hypre_TFree(iUventries, HYPRE_MEMORY_HOST);
         hypre_TFree(Uventries, HYPRE_MEMORY_HOST);
         hypre_TFree(Uveoffsets, HYPRE_MEMORY_HOST);
         hypre_TFree(Uvboxes, HYPRE_MEMORY_HOST);
         hypre_TFree(graph, HYPRE_MEMORY_HOST);
      }
   }

   return hypre_error_flag;
}

/*--------------------------------------------------------------------------
 *--------------------------------------------------------------------------*/

HYPRE_Int
HYPRE_SStructGraphSetDomainGrid( HYPRE_SStructGraph graph,
                                 HYPRE_SStructGrid  dom_grid)
{
   /* This should only decrement a reference counter */
   HYPRE_SStructGridDestroy(hypre_SStructGraphDomGrid(graph));
   hypre_SStructGridRef(dom_grid, &hypre_SStructGraphDomGrid(graph));

   return hypre_error_flag;
}

/*--------------------------------------------------------------------------
 *--------------------------------------------------------------------------*/

HYPRE_Int
HYPRE_SStructGraphSetStencil( HYPRE_SStructGraph   graph,
                              HYPRE_Int            part,
                              HYPRE_Int            var,
                              HYPRE_SStructStencil stencil )
{
   hypre_SStructStencilRef(stencil, &hypre_SStructGraphStencil(graph, part, var));

   return hypre_error_flag;
}

/*--------------------------------------------------------------------------
 *--------------------------------------------------------------------------*/

HYPRE_Int
HYPRE_SStructGraphSetFEM( HYPRE_SStructGraph graph,
                          HYPRE_Int          part )
{
   if (!hypre_SStructGraphFEMPNSparse(graph, part))
   {
      hypre_SStructGraphFEMPNSparse(graph, part) = -1;
   }

   return hypre_error_flag;
}

/*--------------------------------------------------------------------------
 *--------------------------------------------------------------------------*/

HYPRE_Int
HYPRE_SStructGraphSetFEMSparsity( HYPRE_SStructGraph  graph,
                                  HYPRE_Int           part,
                                  HYPRE_Int           nsparse,
                                  HYPRE_Int          *sparsity )
{
   HYPRE_Int          *fem_sparse_i;
   HYPRE_Int          *fem_sparse_j;
   HYPRE_Int           s;

   hypre_SStructGraphFEMPNSparse(graph, part) = nsparse;
   fem_sparse_i = hypre_TAlloc(HYPRE_Int, nsparse, HYPRE_MEMORY_HOST);
   fem_sparse_j = hypre_TAlloc(HYPRE_Int, nsparse, HYPRE_MEMORY_HOST);
   for (s = 0; s < nsparse; s++)
   {
      fem_sparse_i[s] = sparsity[2 * s];
      fem_sparse_j[s] = sparsity[2 * s + 1];
   }
   hypre_SStructGraphFEMPSparseI(graph, part) = fem_sparse_i;
   hypre_SStructGraphFEMPSparseJ(graph, part) = fem_sparse_j;

   return hypre_error_flag;
}

/*--------------------------------------------------------------------------
 *   THIS IS FOR A NON-OVERLAPPING GRID GRAPH.
 *
 *   Now we just keep track of calls to this function and do all the "work"
 *   in the assemble.
 *--------------------------------------------------------------------------*/

HYPRE_Int
HYPRE_SStructGraphAddEntries( HYPRE_SStructGraph   graph,
                              HYPRE_Int            part,
                              HYPRE_Int           *index,
                              HYPRE_Int            var,
                              HYPRE_Int            to_part,
                              HYPRE_Int           *to_index,
                              HYPRE_Int            to_var )
{
   hypre_SStructGrid        *grid      = hypre_SStructGraphGrid(graph);
   HYPRE_Int                 ndim      = hypre_SStructGridNDim(grid);

   hypre_SStructGraphEntry **entries   = hypre_SStructGraphEntries(graph);
   hypre_SStructGraphEntry  *new_entry;

   HYPRE_Int                 n_entries = hypre_SStructNGraphEntries(graph);
   HYPRE_Int                 a_entries = hypre_SStructAGraphEntries(graph);

   HYPRE_ANNOTATE_FUNC_BEGIN;

   /* check storage */
   if (!a_entries)
   {
      a_entries = 1000;
      entries = hypre_CTAlloc(hypre_SStructGraphEntry *, a_entries, HYPRE_MEMORY_HOST);

      hypre_SStructAGraphEntries(graph) = a_entries;
      hypre_SStructGraphEntries(graph) = entries;
   }
   else if (n_entries >= a_entries)
   {
      a_entries += 1000;
      entries = hypre_TReAlloc(entries, hypre_SStructGraphEntry *, a_entries, HYPRE_MEMORY_HOST);

      hypre_SStructAGraphEntries(graph) = a_entries;
      hypre_SStructGraphEntries(graph) = entries;
   }

   /*save parameters to a new entry */

   new_entry = hypre_TAlloc(hypre_SStructGraphEntry, 1, HYPRE_MEMORY_HOST);

   hypre_SStructGraphEntryPart(new_entry) = part;
   hypre_SStructGraphEntryToPart(new_entry) = to_part;

   hypre_SStructGraphEntryVar(new_entry) = var;
   hypre_SStructGraphEntryToVar(new_entry) = to_var;

   hypre_CopyToCleanIndex(index, ndim, hypre_SStructGraphEntryIndex(new_entry));
   hypre_CopyToCleanIndex(
      to_index, ndim, hypre_SStructGraphEntryToIndex(new_entry));

   entries[n_entries] = new_entry;

   /* update count */
   n_entries++;
   hypre_SStructNGraphEntries(graph) = n_entries;

   HYPRE_ANNOTATE_FUNC_END;

   return hypre_error_flag;
}

/*--------------------------------------------------------------------------
 * This routine mainly computes the column numbers for the non-stencil
 * graph entries (i.e., those created by GraphAddEntries calls).
 *--------------------------------------------------------------------------*/

HYPRE_Int
HYPRE_SStructGraphAssemble( HYPRE_SStructGraph graph )
{

   MPI_Comm                  comm        = hypre_SStructGraphComm(graph);
   HYPRE_Int                 ndim        = hypre_SStructGraphNDim(graph);
   hypre_SStructGrid        *grid        = hypre_SStructGraphGrid(graph);
   hypre_SStructGrid        *dom_grid    = hypre_SStructGraphDomGrid(graph);
   HYPRE_Int                 nparts      = hypre_SStructGraphNParts(graph);
   hypre_SStructStencil   ***stencils    = hypre_SStructGraphStencils(graph);
   hypre_Box              ***Uvboxes     = hypre_SStructGraphUVBoxes(graph);
   hypre_BoxManager       ***managers    = hypre_SStructGridBoxManagers(grid);
   HYPRE_Int                 nUventries;
   HYPRE_Int                *iUventries;
   hypre_SStructUVEntry    **Uventries;
   HYPRE_Int                 Uvesize;
   HYPRE_BigInt            **Uveoffsets;
   HYPRE_Int                 type          = hypre_SStructGraphObjectType(graph);
   hypre_SStructGraphEntry **add_entries   = hypre_SStructGraphEntries(graph);
   HYPRE_Int                 n_add_entries = hypre_SStructNGraphEntries(graph);

   hypre_SStructPGrid       *pgrid;
   hypre_StructGrid         *sgrid;
   HYPRE_Int                 nvars;
   hypre_BoxArray           *boxes;
   hypre_Box                *Uvbox;

   hypre_SStructGraphEntry  *new_entry;
   hypre_SStructUVEntry     *Uventry;
   HYPRE_Int                 nUentries;
   hypre_SStructUEntry      *Uentries;
   HYPRE_Int                 to_part;
   hypre_IndexRef            to_index;
   HYPRE_Int                 to_var;
   HYPRE_Int                 to_boxnum;
   HYPRE_Int                 to_proc;
   HYPRE_BigInt              Uverank, rank;
   hypre_BoxManEntry        *boxman_entry;

   HYPRE_Int                 nprocs, myproc;
   HYPRE_Int                 part, var;
   hypre_IndexRef            index;
   HYPRE_Int                 i, ii, j, d;

   hypre_Box                *pbnd_box;
   hypre_BoxArray           *boxa;
   hypre_BoxArray           *pbnd_boxa;
   hypre_BoxArrayArray      *pbnd_boxaa;
   HYPRE_Int               **idxcnt;
   HYPRE_Int             ****indices;

   /* may need to re-do box managers for the AP*/
   hypre_BoxManager        ***new_managers = NULL;
   hypre_BoxManager          *orig_boxman;
   hypre_BoxManager          *new_boxman;

   HYPRE_Int                  global_n_add_entries;
   HYPRE_Int                  is_gather, k;

   hypre_BoxManEntry         *all_entries, *entry;
   HYPRE_Int                  num_entries;
   void                      *info;
   hypre_Box                 *bbox, *new_box, *grow_box;
   HYPRE_Int                 *num_ghost;

   HYPRE_ANNOTATE_FUNC_BEGIN;

   /*---------------------------------------------------------
    *  If AP, then may need to redo the box managers
    *
    *  Currently using bounding boxes based on the indexes in add_entries to
    *  determine which boxes to gather in the box managers.  We refer to these
    *  bounding boxes as "gather boxes" here (Uvboxes). This should work
    *  well in most cases, but it does have the potential to cause lots of grid
    *  boxes to be gathered (hence lots of communication).
    *
    *  A better algorithm would use more care in computing gather boxes that
    *  aren't "too big", while not computing "too many" either (which can also
    *  be slow).  One approach might be to compute an octree with leaves that
    *  have the same volume as the maximum grid box volume.  The leaves would
    *  then serve as the gather boxes.  The number of gather boxes would then be
    *  on the order of the number of local grid boxes (assuming the add_entries
    *  are local, which is generally how they should be used).
    *---------------------------------------------------------*/

   new_box  = hypre_BoxCreate(ndim);
   grow_box = hypre_BoxCreate(ndim);

   /* if any processor has added entries, then all need to participate */

   hypre_MPI_Allreduce(&n_add_entries, &global_n_add_entries,
                       1, HYPRE_MPI_INT, hypre_MPI_SUM, comm);

   if (global_n_add_entries > 0 )
   {
      /* create new managers */
      new_managers = hypre_TAlloc(hypre_BoxManager **, nparts, HYPRE_MEMORY_HOST);

      for (part = 0; part < nparts; part++)
      {
         pgrid = hypre_SStructGridPGrid(grid, part);
         nvars = hypre_SStructPGridNVars(pgrid);

         new_managers[part] = hypre_TAlloc(hypre_BoxManager *, nvars, HYPRE_MEMORY_HOST);

         for (var = 0; var < nvars; var++)
         {
            sgrid = hypre_SStructPGridSGrid(pgrid, var);

            orig_boxman = managers[part][var];
            bbox =  hypre_BoxManBoundingBox(orig_boxman);

            hypre_BoxManCreate(hypre_BoxManNEntries(orig_boxman),
                               hypre_BoxManEntryInfoSize(orig_boxman),
                               hypre_StructGridNDim(sgrid), bbox,
                               hypre_StructGridComm(sgrid),
                               &new_managers[part][var]);

            /* need to set the num ghost for new manager also */
            num_ghost = hypre_StructGridNumGhost(sgrid);
            hypre_BoxManSetNumGhost(new_managers[part][var], num_ghost);
         }
      } /* end loop over parts */

      /* now go through the local add entries */
      for (j = 0; j < n_add_entries; j++)
      {
         new_entry = add_entries[j];

         /* check part, var, index, to_part, to_var, to_index */
         for (k = 0; k < 2; k++)
         {
            switch (k)
            {
               case 0:
                  part  = hypre_SStructGraphEntryPart(new_entry);
                  var   = hypre_SStructGraphEntryVar(new_entry);
                  index = hypre_SStructGraphEntryIndex(new_entry);
                  break;

               case 1:
                  part  = hypre_SStructGraphEntryToPart(new_entry);
                  var   = hypre_SStructGraphEntryToVar(new_entry);
                  index = hypre_SStructGraphEntryToIndex(new_entry);
                  break;
            }

            /* if the index is not within the bounds of the struct grid bounding
               box (which has been set in the box manager) then there should not
               be a coupling here (doesn't make sense) */
            new_boxman = new_managers[part][var];
            bbox  = hypre_BoxManBoundingBox(new_boxman);
            Uvbox = Uvboxes[part][var];

            if (hypre_IndexInBox(index, bbox))
            {
               /* compute new gather box extents based on index */
               hypre_BoxSpanIndex(Uvbox, index);
            }
         }
      }

      /* Now go through the managers and if gather has been called (on any
         processor) then populate the new manager with the entries from the old
         manager and then assemble and delete the old manager. */
      for (part = 0; part < nparts; part++)
      {
         pgrid = hypre_SStructGridPGrid(grid, part);
         nvars = hypre_SStructPGridNVars(pgrid);

         for (var = 0; var < nvars; var++)
         {
            new_boxman = new_managers[part][var];
            Uvbox = Uvboxes[part][var];

            /* call gather if non-empty gather box */
            if (hypre_BoxVolume(Uvbox) > 0)
            {
               hypre_BoxManGatherEntries(
                  new_boxman, hypre_BoxIMin(Uvbox), hypre_BoxIMax(Uvbox));
            }

            /* check to see if gather was called by some processor */
            hypre_BoxManGetGlobalIsGatherCalled(new_boxman, comm, &is_gather);
            if (is_gather)
            {
               /* copy orig boxman information to the new boxman*/

               orig_boxman = managers[part][var];

               hypre_BoxManGetAllEntries(orig_boxman, &num_entries, &all_entries);

               for (j = 0; j < num_entries; j++)
               {
                  entry = &all_entries[j];

                  hypre_BoxManEntryGetInfo(entry, &info);

                  hypre_BoxManAddEntry(new_boxman,
                                       hypre_BoxManEntryIMin(entry),
                                       hypre_BoxManEntryIMax(entry),
                                       hypre_BoxManEntryProc(entry),
                                       hypre_BoxManEntryId(entry),
                                       info);
               }

               /* call assemble for new boxmanager*/
               hypre_BoxManAssemble(new_boxman);

               /* TEMP for testing
                  if (hypre_BoxManNEntries(new_boxman) != num_entries)
                  {
                  hypre_MPI_Comm_rank(comm, &myproc);
                  hypre_printf("myid = %d, new_entries = %d, old entries = %d\n", myproc, hypre_BoxManNEntries(new_boxman), num_entries);
                  } */

               /* destroy old manager */
               hypre_BoxManDestroy (managers[part][var]);
            }
            else /* no gather called */
            {
               /* leave the old manager (so destroy the new one)  */
               hypre_BoxManDestroy(new_boxman);

               /* copy the old to the new */
               new_managers[part][var] = managers[part][var];
            }
         } /* end of var loop */
         hypre_TFree(managers[part], HYPRE_MEMORY_HOST);

      } /* end of part loop */
      hypre_TFree(managers, HYPRE_MEMORY_HOST);

      /* assign the new ones */
      hypre_SStructGridBoxManagers(grid) = new_managers;
   }

   /* clean up */
   hypre_BoxDestroy(new_box);

   /* end of AP stuff */

   hypre_MPI_Comm_size(comm, &nprocs);
   hypre_MPI_Comm_rank(comm, &myproc);

   /*---------------------------------------------------------
    * Set up UVEntries and iUventries
    *---------------------------------------------------------*/

   /* first set up Uvesize and Uveoffsets */

   Uvesize = 0;
   Uveoffsets = hypre_TAlloc(HYPRE_BigInt *, nparts, HYPRE_MEMORY_HOST);
   for (part = 0; part < nparts; part++)
   {
      pgrid = hypre_SStructGridPGrid(grid, part);
      nvars = hypre_SStructPGridNVars(pgrid);

      Uveoffsets[part] = hypre_TAlloc(HYPRE_BigInt, nvars, HYPRE_MEMORY_HOST);
      for (var = 0; var < nvars; var++)
      {
         Uveoffsets[part][var] = Uvesize;
         sgrid = hypre_SStructPGridSGrid(pgrid, var);
         boxes = hypre_StructGridBoxes(sgrid);
         hypre_ForBoxI(i, boxes)
         {
            hypre_CopyBox(hypre_BoxArrayBox(boxes, i), grow_box);
            hypre_BoxGrowByValue(grow_box, 1);
            Uvesize += hypre_BoxVolume(grow_box);
         }
      }
   }
   hypre_SStructGraphUVESize(graph)    = Uvesize;
   hypre_SStructGraphUVEOffsets(graph) = Uveoffsets;
   hypre_BoxDestroy(grow_box);

   /* now set up indices, nUventries, iUventries, and Uventries */
   indices = hypre_TAlloc(HYPRE_Int ***, nparts, HYPRE_MEMORY_HOST);
   idxcnt  = hypre_TAlloc(HYPRE_Int *, nparts, HYPRE_MEMORY_HOST);
   for (part = 0; part < nparts; part++)
   {
      pgrid = hypre_SStructGridPGrid(grid, part);
      nvars = hypre_SStructPGridNVars(pgrid);

      indices[part] = hypre_TAlloc(HYPRE_Int **, nvars, HYPRE_MEMORY_HOST);
      idxcnt[part]  = hypre_CTAlloc(HYPRE_Int, nvars, HYPRE_MEMORY_HOST);
      for (var = 0; var < nvars; var++)
      {
         indices[part][var] = hypre_CTAlloc(HYPRE_Int *, ndim, HYPRE_MEMORY_HOST);
         for (d = 0; d < ndim; d++)
         {
            /* TODO: n_add_entries is a too large upper bound */
            indices[part][var][d] = hypre_CTAlloc(HYPRE_Int, n_add_entries, HYPRE_MEMORY_HOST);
         }
      }
   }
   iUventries = hypre_TAlloc(HYPRE_Int, n_add_entries, HYPRE_MEMORY_HOST);
   Uventries = hypre_CTAlloc(hypre_SStructUVEntry *, Uvesize, HYPRE_MEMORY_HOST);
   hypre_SStructGraphIUVEntries(graph) = iUventries;
   hypre_SStructGraphUVEntries(graph)  = Uventries;
   nUventries = 0;

   /* go through each entry that was added */
   for (j = 0; j < n_add_entries; j++)
   {
      new_entry = add_entries[j];

      part     = hypre_SStructGraphEntryPart(new_entry);
      var      = hypre_SStructGraphEntryVar(new_entry);
      index    = hypre_SStructGraphEntryIndex(new_entry);
      to_part  = hypre_SStructGraphEntryToPart(new_entry);
      to_var   = hypre_SStructGraphEntryToVar(new_entry);
      to_index = hypre_SStructGraphEntryToIndex(new_entry);

      /* Safety checks */
      hypre_assert((part >= 0) && (part < nparts));
      hypre_assert((to_part >= 0) && (to_part < nparts));

      /* Build indices array */
      for (d = 0; d < ndim; d++)
      {
         indices[part][var][d][idxcnt[part][var]] = hypre_IndexD(index, d);
         indices[to_part][to_var][d][idxcnt[to_part][to_var]] = hypre_IndexD(to_index, d);
      }
      idxcnt[part][var]++;
      idxcnt[to_part][to_var]++;

      /* compute location (rank) for Uventry */
      hypre_SStructGraphGetUVEntryRank(graph, part, var, index, &Uverank);

      if (Uverank > -1)
      {
         iUventries[nUventries] = Uverank;

         if (Uventries[Uverank] == NULL)
         {
            Uventry = hypre_TAlloc(hypre_SStructUVEntry, 1, HYPRE_MEMORY_HOST);
            hypre_SStructUVEntryPart(Uventry) = part;
            hypre_CopyIndex(index, hypre_SStructUVEntryIndex(Uventry));
            hypre_SStructUVEntryVar(Uventry) = var;
            hypre_SStructGridFindBoxManEntry(grid, part, index, var, &boxman_entry);
            hypre_SStructBoxManEntryGetGlobalRank(boxman_entry, index, &rank, type);
            hypre_SStructUVEntryRank(Uventry) = rank;
            nUentries = 1;
            Uentries = hypre_TAlloc(hypre_SStructUEntry, nUentries, HYPRE_MEMORY_HOST);
         }
         else
         {
            Uventry = Uventries[Uverank];
            nUentries = hypre_SStructUVEntryNUEntries(Uventry) + 1;
            Uentries = hypre_SStructUVEntryUEntries(Uventry);
            Uentries = hypre_TReAlloc(Uentries, hypre_SStructUEntry, nUentries, HYPRE_MEMORY_HOST);
         }
         hypre_SStructUVEntryNUEntries(Uventry) = nUentries;
         hypre_SStructUVEntryUEntries(Uventry)  = Uentries;
         hypre_SStructGraphUEMaxSize(graph) =
            hypre_max(hypre_SStructGraphUEMaxSize(graph), nUentries);

         i = nUentries - 1;
         hypre_SStructUVEntryToPart(Uventry, i) = to_part;
         hypre_CopyIndex(to_index, hypre_SStructUVEntryToIndex(Uventry, i));
         hypre_SStructUVEntryToVar(Uventry, i) = to_var;

         hypre_SStructGridFindBoxManEntry(dom_grid, to_part, to_index,
                                          to_var, &boxman_entry);
         hypre_SStructBoxManEntryGetBoxnum(boxman_entry, &to_boxnum);
         hypre_SStructUVEntryToBoxnum(Uventry, i) = to_boxnum;
         hypre_SStructBoxManEntryGetProcess(boxman_entry, &to_proc);
         hypre_SStructUVEntryToProc(Uventry, i) = to_proc;
         hypre_SStructBoxManEntryGetGlobalRank(
            boxman_entry, to_index, &rank, type);
         hypre_SStructUVEntryToRank(Uventry, i) = rank;

         Uventries[Uverank] = Uventry;

         nUventries++;
         hypre_SStructGraphNUVEntries(graph) = nUventries;

         hypre_SStructGraphUVEntries(graph) = Uventries;
      }

      /*free each add entry after copying */
      hypre_TFree(new_entry, HYPRE_MEMORY_HOST);
   } /* end of loop through add entries */

   /*---------------------------------------------------------
    * Set up part boundary data
    *---------------------------------------------------------*/

   /* Create part boundary boxes */
   pbnd_box = hypre_BoxCreate(ndim);
   for (part = 0; part < nparts; part++)
   {
      pgrid = hypre_SStructGridPGrid(grid, part);
      nvars = hypre_SStructPGridNVars(pgrid);
      for (var = 0; var < nvars; var++)
      {
         sgrid = hypre_SStructPGridSGrid(pgrid, var);
         boxes = hypre_StructGridBoxes(sgrid);
         pbnd_boxaa = hypre_SStructPGridPBndBoxArrayArray(pgrid, var);

         /* Eliminate duplicate entries */
         hypre_UniqueIntArrayND(ndim, &idxcnt[part][var], indices[part][var]);

         /* Create array of boxes
            Note: we use a threshold of 0.5 to facilitate the construction
            of boxes when indices are distributed in "every other" fashion
            for a given direction
         */
         boxa = NULL;
         hypre_BoxArrayCreateFromIndices(ndim, idxcnt[part][var],
                                         indices[part][var], 0.5, &boxa);

         /* Intersect newly created BoxArray with grid boxes */
         if (boxa)
         {
            hypre_ForBoxI(i, boxes)
            {
               pbnd_boxa = hypre_BoxArrayArrayBoxArray(pbnd_boxaa, i);
               hypre_ForBoxI(ii, boxa)
               {
                  hypre_IntersectBoxes(hypre_BoxArrayBox(boxes, i),
                                       hypre_BoxArrayBox(boxa, ii),
                                       pbnd_box);
                  if (hypre_BoxVolume(pbnd_box) > 0)
                  {
                     hypre_AppendBox(pbnd_box, pbnd_boxa);
                  }
               }
            }

            /* Free memory */
            hypre_BoxArrayDestroy(boxa);
         }
      }
   }

   /* Free memory */
   for (part = 0; part < nparts; part++)
   {
      pgrid = hypre_SStructGridPGrid(grid, part);
      nvars = hypre_SStructPGridNVars(pgrid);
      for (var = 0; var < nvars; var++)
      {
         for (d = 0; d < ndim; d++)
         {
            hypre_TFree(indices[part][var][d], HYPRE_MEMORY_HOST);
         }
         hypre_TFree(indices[part][var], HYPRE_MEMORY_HOST);
      }
      hypre_TFree(idxcnt[part], HYPRE_MEMORY_HOST);
      hypre_TFree(indices[part], HYPRE_MEMORY_HOST);
   }
   hypre_TFree(idxcnt, HYPRE_MEMORY_HOST);
   hypre_TFree(indices, HYPRE_MEMORY_HOST);
   hypre_TFree(add_entries, HYPRE_MEMORY_HOST);
   hypre_BoxDestroy(pbnd_box);

   /*---------------------------------------------------------
    * Set up the FEM stencil information
    *---------------------------------------------------------*/

   for (part = 0; part < nparts; part++)
   {
      /* only do this if SetFEM was called */
      if (hypre_SStructGraphFEMPNSparse(graph, part))
      {
         HYPRE_Int     fem_nsparse  = hypre_SStructGraphFEMPNSparse(graph, part);
         HYPRE_Int    *fem_sparse_i = hypre_SStructGraphFEMPSparseI(graph, part);
         HYPRE_Int    *fem_sparse_j = hypre_SStructGraphFEMPSparseJ(graph, part);
         HYPRE_Int    *fem_entries  = hypre_SStructGraphFEMPEntries(graph, part);
         HYPRE_Int     fem_nvars    = hypre_SStructGridFEMPNVars(grid, part);
         HYPRE_Int    *fem_vars     = hypre_SStructGridFEMPVars(grid, part);
         hypre_Index  *fem_offsets  = hypre_SStructGridFEMPOffsets(grid, part);
         hypre_Index   offset;
         HYPRE_Int     s, iv, jv, d, nvars, entry;
         HYPRE_Int    *stencil_sizes;
         hypre_Index **stencil_offsets;
         HYPRE_Int   **stencil_vars;

         pgrid = hypre_SStructGridPGrid(grid, part);
         nvars = hypre_SStructPGridNVars(pgrid);

         /* build default full sparsity pattern if nothing set by user */
         if (fem_nsparse < 0)
         {
            fem_nsparse = fem_nvars * fem_nvars;
            fem_sparse_i = hypre_TAlloc(HYPRE_Int, fem_nsparse, HYPRE_MEMORY_HOST);
            fem_sparse_j = hypre_TAlloc(HYPRE_Int, fem_nsparse, HYPRE_MEMORY_HOST);
            s = 0;
            for (i = 0; i < fem_nvars; i++)
            {
               for (j = 0; j < fem_nvars; j++)
               {
                  fem_sparse_i[s] = i;
                  fem_sparse_j[s] = j;
                  s++;
               }
            }
            hypre_SStructGraphFEMPNSparse(graph, part) = fem_nsparse;
            hypre_SStructGraphFEMPSparseI(graph, part) = fem_sparse_i;
            hypre_SStructGraphFEMPSparseJ(graph, part) = fem_sparse_j;
         }

         fem_entries = hypre_CTAlloc(HYPRE_Int, fem_nsparse, HYPRE_MEMORY_HOST);
         hypre_SStructGraphFEMPEntries(graph, part) = fem_entries;

         stencil_sizes   = hypre_CTAlloc(HYPRE_Int, nvars, HYPRE_MEMORY_HOST);
         stencil_offsets = hypre_CTAlloc(hypre_Index *, nvars, HYPRE_MEMORY_HOST);
         stencil_vars    = hypre_CTAlloc(HYPRE_Int *, nvars, HYPRE_MEMORY_HOST);
         for (iv = 0; iv < nvars; iv++)
         {
            stencil_offsets[iv] = hypre_CTAlloc(hypre_Index, fem_nvars * fem_nvars, HYPRE_MEMORY_HOST);
            stencil_vars[iv]    = hypre_CTAlloc(HYPRE_Int, fem_nvars * fem_nvars, HYPRE_MEMORY_HOST);
         }

         for (s = 0; s < fem_nsparse; s++)
         {
            i = fem_sparse_i[s];
            j = fem_sparse_j[s];
            iv = fem_vars[i];
            jv = fem_vars[j];

            /* shift off-diagonal offset by diagonal */
            for (d = 0; d < ndim; d++)
            {
               offset[d] = fem_offsets[j][d] - fem_offsets[i][d];
            }

            /* search stencil_offsets */
            for (entry = 0; entry < stencil_sizes[iv]; entry++)
            {
               /* if offset is already in the stencil, break */
               if ( hypre_IndexesEqual(offset, stencil_offsets[iv][entry], ndim)
                    && (jv == stencil_vars[iv][entry]) )
               {
                  break;
               }
            }
            /* if this is a new stencil offset, add it to the stencil */
            if (entry == stencil_sizes[iv])
            {
               for (d = 0; d < ndim; d++)
               {
                  stencil_offsets[iv][entry][d] = offset[d];
               }
               stencil_vars[iv][entry] = jv;
               stencil_sizes[iv]++;
            }

            fem_entries[s] = entry;
         }

         /* set up the stencils */
         for (iv = 0; iv < nvars; iv++)
         {
            HYPRE_SStructStencilDestroy(stencils[part][iv]);
            HYPRE_SStructStencilCreate(ndim, stencil_sizes[iv],
                                       &stencils[part][iv]);
            for (entry = 0; entry < stencil_sizes[iv]; entry++)
            {
               HYPRE_SStructStencilSetEntry(stencils[part][iv], entry,
                                            stencil_offsets[iv][entry],
                                            stencil_vars[iv][entry]);
            }
         }

         /* free up temporary stuff */
         for (iv = 0; iv < nvars; iv++)
         {
            hypre_TFree(stencil_offsets[iv], HYPRE_MEMORY_HOST);
            hypre_TFree(stencil_vars[iv], HYPRE_MEMORY_HOST);
         }
         hypre_TFree(stencil_sizes, HYPRE_MEMORY_HOST);
         hypre_TFree(stencil_offsets, HYPRE_MEMORY_HOST);
         hypre_TFree(stencil_vars, HYPRE_MEMORY_HOST);
      }
   }

   /*---------------------------------------------------------
    * Sort the iUventries array and eliminate duplicates.
    *---------------------------------------------------------*/

   if (nUventries > 1)
   {
      hypre_qsort0(iUventries, 0, nUventries - 1);

      j = 1;
      for (i = 1; i < nUventries; i++)
      {
         if (iUventries[i] > iUventries[i - 1])
         {
            iUventries[j] = iUventries[i];
            j++;
         }
      }
      nUventries = j;
      hypre_SStructGraphNUVEntries(graph) = nUventries;
   }

   HYPRE_ANNOTATE_FUNC_END;

   return hypre_error_flag;
}

/*--------------------------------------------------------------------------
 *--------------------------------------------------------------------------*/

HYPRE_Int
HYPRE_SStructGraphSetObjectType( HYPRE_SStructGraph  graph,
                                 HYPRE_Int           type )
{
   hypre_SStructGraphObjectType(graph) = type;

   return hypre_error_flag;
}
