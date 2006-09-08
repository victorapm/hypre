/*BHEADER**********************************************************************
 * Copyright (c) 2006   The Regents of the University of California.
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by the HYPRE team <hypre-users@llnl.gov>, UCRL-CODE-222953.
 * All rights reserved.
 *
 * This file is part of HYPRE (see http://www.llnl.gov/CASC/hypre/).
 * Please see the COPYRIGHT_and_LICENSE file for the copyright notice,
 * disclaimer and the GNU Lesser General Public License.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (as published by the Free
 * Software Foundation) version 2.1 dated February 1999.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * $Revision$
*********************************************************************EHEADER*/
#include "headers.h"
#include "aux_interp.h"

/*---------------------------------------------------------------------------
 * Auxilary routines for the long range interpolation methods.
 *  Implemented: "standard", "extended", "multipass", "FF"
 *--------------------------------------------------------------------------*/

/* Inserts nodes to position expected for CF_marker_offd and P_marker_offd.
 * This is different than the send/recv vecs
 * explanation: old offd nodes take up first chunk of CF_marker_offd, new offd 
 * nodes take up the second chunk 0f CF_marker_offd. */
void insert_new_nodes(hypre_ParCSRCommPkg *comm_pkg, int *IN_marker, 
		      int *node_add, int num_cols_A_offd, 
		      int full_off_procNodes, int num_procs,
		      int *OUT_marker)
{   
  hypre_ParCSRCommHandle  *comm_handle;

  int i, j, start, index, ip, min, max;
  int num_sends, back_shift, original_off;
  int num_recvs;
  int *recv_procs;
  int *recv_vec_starts;
  int *end_diag, *int_buf_data;

  num_sends = hypre_ParCSRCommPkgNumSends(comm_pkg);
  num_recvs = hypre_ParCSRCommPkgNumRecvs(comm_pkg);
  recv_procs = hypre_ParCSRCommPkgRecvProcs(comm_pkg);
  recv_vec_starts = hypre_ParCSRCommPkgRecvVecStarts(comm_pkg);
   
  index = hypre_ParCSRCommPkgSendMapStart(comm_pkg,num_sends);
  if(index < full_off_procNodes)
    index = full_off_procNodes;
  int_buf_data = hypre_CTAlloc(int, index);

  index = 0;
  for (i = 0; i < num_sends; i++)
  {
    start = hypre_ParCSRCommPkgSendMapStart(comm_pkg, i);
    for (j = start; j < hypre_ParCSRCommPkgSendMapStart(comm_pkg, i+1); 
	 j++)
      int_buf_data[index++] 
	= IN_marker[hypre_ParCSRCommPkgSendMapElmt(comm_pkg,j)];
  }
   
  comm_handle = hypre_ParCSRCommHandleCreate( 11, comm_pkg, int_buf_data, 
					      OUT_marker);
   
  hypre_ParCSRCommHandleDestroy(comm_handle);
  comm_handle = NULL;
  
  /* Sort fine_to_coarse so that the original off processors are first*/
  back_shift = 0;
  end_diag = hypre_CTAlloc(int, num_procs);
  if(num_recvs)
  {
    min = recv_procs[0];
    max = min;
    for(i = 0; i < num_recvs; i++)
    {
      ip = recv_procs[i];
      start = recv_vec_starts[i];
      original_off = (recv_vec_starts[i+1] - start) - node_add[ip];
      end_diag[ip] = start + original_off;
      for(j = start; j < end_diag[ip]; j++)
	int_buf_data[j-back_shift] = OUT_marker[j];
      back_shift += node_add[ip];
      if(ip < min) min = ip;
      if(ip > max) max = ip;
    }
    back_shift = 0;
    for(i = min; i <= max; i++)
    {
      for(j = 0; j < node_add[i]; j++)
	int_buf_data[back_shift+j+num_cols_A_offd] = 
	  OUT_marker[end_diag[i]+j];
      back_shift += node_add[i];
    }
    
    for(i = 0; i < full_off_procNodes; i++)
      OUT_marker[i] = int_buf_data[i];

    hypre_TFree(int_buf_data);
    hypre_TFree(end_diag);
  } 
  return;
} 

/* Add new communication patterns for new offd nodes */
void
hypre_ParCSRCommExtendA(hypre_ParCSRMatrix *A, int newoff, int *found,
			int *p_num_recvs, int **p_recv_procs,
			int **p_recv_vec_starts, int *p_num_sends,
			int **p_send_procs, int **p_send_map_starts,
			int **p_send_map_elmts, int **p_node_add)
{
  hypre_ParCSRCommPkg *comm_pkg_A = hypre_ParCSRMatrixCommPkg(A);
  int num_recvs_A = hypre_ParCSRCommPkgNumRecvs(comm_pkg_A);
  int *recv_procs_A = hypre_ParCSRCommPkgRecvProcs(comm_pkg_A);
  int *recv_vec_starts_A = hypre_ParCSRCommPkgRecvVecStarts(comm_pkg_A);
  int *col_starts = hypre_ParCSRMatrixColStarts(A);
  hypre_CSRMatrix *A_offd = hypre_ParCSRMatrixOffd(A);
  int num_cols_A_offd = hypre_CSRMatrixNumCols(A_offd);
  int *col_map_offd = hypre_ParCSRMatrixColMapOffd(A);
  int first_col_diag = hypre_ParCSRMatrixFirstColDiag(A);

  int new_num_recv;
  int *new_recv_proc;   
  int *tmp_recv_proc;
  int *tmp;
  int *new_recv_vec_starts;   
  int num_sends;
  int num_elmts;
  int *send_procs;   
  int *send_map_starts;   
  int *send_map_elmts;   
  int *nodes_recv;

  int i, j, k;
  int vec_len, vec_start;
  int num_procs, my_id;
  int num_requests;
  int local_info;
  int *info;
  int *displs;
  int *recv_buf;
  int ip;
  int glob_col;
  int proc_found;
  int *proc_add;
  int *proc_mark;
  int *new_map;
  int *new_map_starts;
  int j1,index;
  int total_cols = num_cols_A_offd + newoff;
  int *node_addition;

  MPI_Comm comm = hypre_ParCSRMatrixComm(A);
  MPI_Request *requests;
  MPI_Status *status;
  
  MPI_Comm_size(comm,&num_procs);
  MPI_Comm_rank(comm,&my_id);
  
  new_num_recv = num_recvs_A;

  /* Allocate vectors for temporary variables */
  tmp_recv_proc = hypre_CTAlloc(int, num_procs);
  nodes_recv = hypre_CTAlloc(int, num_procs);
  proc_add = hypre_CTAlloc(int, num_procs);
  proc_mark = hypre_CTAlloc(int, num_procs);
  info = hypre_CTAlloc(int, num_procs);

  /* Initialize the new node proc counter (already accounted for procs
   * will stay 0 in the loop */
  for(i = 0; i < new_num_recv; i++)
    tmp_recv_proc[i] = recv_procs_A[i];

  /* Set up full offd map, col_map_offd only has neighbor off diag entries.
   * We need neighbor of neighbor nodes as well.*/
  new_map = hypre_CTAlloc(int, total_cols);
  new_map_starts = hypre_CTAlloc(int, num_procs+1);
  node_addition = hypre_CTAlloc(int, num_procs);
  for(i = 0; i < num_procs; i++)
  {
    nodes_recv[i] = 0;
    node_addition[i] = 0;
  }

  if(newoff)
  {
    i = 0;
    k = 0;
    j1 = 0;
    index = 0;
    while(i < num_procs)
    {
      new_map_starts[i] = index;
      while(col_map_offd[j1] < col_starts[i+1] && j1 < num_cols_A_offd)
      {
	new_map[index] = col_map_offd[j1];
	j1++;
	index++;
      }
      proc_found = index;
      for(j = 0; j < newoff; j++)
	if(i != my_id)
	  if(found[j] < col_starts[i+1] && found[j] >= col_starts[i])
	  {
	    new_map[index] = found[j];
	    node_addition[k]++;
	    index++;
	  }
      /* Sort new nodes at end */
      if(proc_found < index - 1)
	ssort(&new_map[index - (index-proc_found)],index-proc_found);
      k++;
      i++;
    }
    new_map_starts[num_procs] = index;
  }
  else
    for(i = 0; i < num_cols_A_offd; i++)
      new_map[i] = col_map_offd[i];

  /* Loop through the neighbor of neighbor nodes to determine proc
   * ownership. Add node to list of receives from that proc. */
  for(i = num_cols_A_offd; i < num_cols_A_offd + newoff; i++)
  { 
    glob_col = found[i-num_cols_A_offd];
    j = 1;
    while(j <= num_procs)
    {
      if(glob_col < col_starts[j])
      {
	proc_found = 0;
	k = 0;
	while(k < num_recvs_A)
	{
	  if(recv_procs_A[k] == j-1)
	  {
	    proc_found = 1;
	    nodes_recv[j-1]++;
	    k = num_recvs_A;
	  }
	  else
	    k++;
	}
	if(!proc_found)
	  while(k < new_num_recv)
	  {
	    if(tmp_recv_proc[k] == j-1)
	    {
	      proc_found = 1;
	      nodes_recv[j-1]++;
	      k = new_num_recv;
	    }
	    else
	      k++;
	  }
	if(!proc_found)
	{
	  tmp_recv_proc[new_num_recv] = j-1;
	  nodes_recv[j-1]++;
	  new_num_recv++;
	}
	j = num_procs + 1;
      }
      j++;
    }
  }
  
  new_recv_proc = hypre_CTAlloc(int, new_num_recv);
  for(i = 0; i < new_num_recv; i++)
    new_recv_proc[i] = tmp_recv_proc[i];

  new_recv_vec_starts = hypre_CTAlloc(int, new_num_recv+1);
  new_recv_vec_starts[0] = 0;
 
  /* Now tell new processors that they need to send some info and change
   * their send comm*/
  local_info = 2*new_num_recv;
  MPI_Allgather(&local_info, 1, MPI_INT, info, 1, MPI_INT, comm); 

  /* ----------------------------------------------------------------------
   * generate information to be sent: tmp contains for each recv_proc:
   * id of recv_procs, number of elements to be received for this processor,
   * indices of elements (in this order)
   * ---------------------------------------------------------------------*/
   displs = hypre_CTAlloc(int, num_procs+1);
   displs[0] = 0;
   for (i=1; i < num_procs+1; i++)
	displs[i] = displs[i-1]+info[i-1]; 
   recv_buf = hypre_CTAlloc(int, displs[num_procs]); 

   tmp = hypre_CTAlloc(int, local_info);

   j = 0;
   /* Load old information if recv proc was already in comm */  
   for (i=0; i < num_recvs_A; i++)
   {
     num_elmts = recv_vec_starts_A[i+1]-recv_vec_starts_A[i] + 
       nodes_recv[new_recv_proc[i]];
     new_recv_vec_starts[i+1] = new_recv_vec_starts[i]+num_elmts;
     tmp[j++] = new_recv_proc[i];
     tmp[j++] = num_elmts;
   }
   /* Add information if recv proc was added */
   for (i=num_recvs_A; i < new_num_recv; i++)
   {
     num_elmts = nodes_recv[new_recv_proc[i]];
     new_recv_vec_starts[i+1] = new_recv_vec_starts[i]+num_elmts;
     tmp[j++] = new_recv_proc[i];
     tmp[j++] = num_elmts;
   }

   MPI_Allgatherv(tmp,local_info,MPI_INT,recv_buf,info,displs,MPI_INT,comm);
	
   hypre_TFree(tmp);

   /* ----------------------------------------------------------------------
    * determine num_sends and number of elements to be sent
    * ---------------------------------------------------------------------*/

   num_sends = 0;
   num_elmts = 0;
   proc_add[0] = 0;
   for (i=0; i < num_procs; i++)
   {
      j = displs[i];
      while ( j < displs[i+1])
      {
	 if (recv_buf[j++] == my_id)
	 {
	    proc_mark[num_sends] = i;
	    num_sends++;
	    proc_add[num_sends] = proc_add[num_sends-1]+recv_buf[j];
	    break;
	 }
	 j++;
      }	
   }
		
    /* ----------------------------------------------------------------------
    * determine send_procs and actual elements to be send (in send_map_elmts)
    * and send_map_starts whose i-th entry points to the beginning of the 
    * elements to be send to proc. i
    * ---------------------------------------------------------------------*/
   
   send_procs = NULL;
   send_map_elmts = NULL;

   if (num_sends)
   {
      send_procs = hypre_CTAlloc(int, num_sends);
      send_map_elmts = hypre_CTAlloc(int, proc_add[num_sends]);
   }
   send_map_starts = hypre_CTAlloc(int, num_sends+1);
   num_requests = new_num_recv+num_sends;
   if (num_requests)
   {
      requests = hypre_CTAlloc(MPI_Request, num_requests);
      status = hypre_CTAlloc(MPI_Status, num_requests);
   }

   if (num_sends) send_map_starts[0] = 0;
   for (i=0; i < num_sends; i++)
   {
      send_map_starts[i+1] = proc_add[i+1];
      send_procs[i] = proc_mark[i];
   }

   j=0;
   for (i=0; i < new_num_recv; i++)
   {
     vec_start = new_recv_vec_starts[i];
     vec_len = new_recv_vec_starts[i+1] - vec_start;
     ip = new_recv_proc[i];
     if(newoff)
       vec_start = new_map_starts[ip];
     MPI_Isend(&new_map[vec_start], vec_len, MPI_INT,
	       ip, 0, comm, &requests[j++]);
   }
   for (i=0; i < num_sends; i++)
   {
      vec_start = send_map_starts[i];
      vec_len = send_map_starts[i+1] - vec_start;
      ip = send_procs[i];
      MPI_Irecv(&send_map_elmts[vec_start], vec_len, MPI_INT,
                        ip, 0, comm, &requests[j++]);
   }

   if (num_requests)
   {
      MPI_Waitall(num_requests, requests, status);
      hypre_TFree(requests);
      hypre_TFree(status);
   }

   if (num_sends)
     for (i=0; i<send_map_starts[num_sends]; i++)
       send_map_elmts[i] -= first_col_diag;
   
   /* finish up with the hand-coded call-by-reference... */
   *p_num_recvs = new_num_recv;
   *p_recv_procs = new_recv_proc;
   *p_recv_vec_starts = new_recv_vec_starts;
   *p_num_sends = num_sends;
   *p_send_procs = send_procs;
   *p_send_map_starts = send_map_starts;
   *p_send_map_elmts = send_map_elmts;
   *p_node_add = node_addition;

   /* De-allocate memory */
   hypre_TFree(tmp_recv_proc);
   hypre_TFree(nodes_recv);   
   hypre_TFree(proc_add);
   hypre_TFree(proc_mark); 
   hypre_TFree(new_map);
   hypre_TFree(recv_buf);
   hypre_TFree(displs);
   hypre_TFree(info);
   hypre_TFree(new_map_starts);

   return;
}

/* sort for non-ordered arrays */
int ssort(int *data, int n)
{
  int i,si;               
  int change = 0;
  
  if(n > 0)
    for(i = n-1; i > 0; i--){
      si = index_of_minimum(data,i+1);
      if(i != si)
      {
	swap_int(data, i, si);
	change = 1;
      }
    }                                                                       
  return change;
}

/* Auxilary function for ssort */
int index_of_minimum(int *data, int n)
{
  int answer;
  int i;
                                                                               
  answer = 0;
  for(i = 1; i < n; i++)
    if(data[answer] < data[i])
      answer = i;
                                                                               
  return answer;
}
                                                                               
void swap_int(int *data, int a, int b)
{
  int temp;
                                                                               
  temp = data[a];
  data[a] = data[b];
  data[b] = temp;

  return;
}

/* Initialize CF_marker_offd, CF_marker, P_marker, P_marker_offd, tmp */
void initialize_vecs(int diag_n, int offd_n, int *diag_ftc, int *offd_ftc, 
		     int *diag_pm, int *offd_pm, int *tmp_CF)
{
  int i;

  /* Quicker initialization */
  if(offd_n < diag_n)
  {
    for(i = 0; i < offd_n; i++)
    {
      diag_ftc[i] = -1;
      offd_ftc[i] = -1;
      diag_pm[i] = -1;
      offd_pm[i] = -1;
      tmp_CF[i] = -1;
    }
    for(i = offd_n; i < diag_n; i++)
    { 
      diag_ftc[i] = -1;
      diag_pm[i] = -1;
    }
  }
  else
  {
    for(i = 0; i < diag_n; i++)
    {
      diag_ftc[i] = -1;
      offd_ftc[i] = -1;
      diag_pm[i] = -1;
      offd_pm[i] = -1;
      tmp_CF[i] = -1;
    }
    for(i = diag_n; i < offd_n; i++)
    { 
      offd_ftc[i] = -1;
      offd_pm[i] = -1;
      tmp_CF[i] = -1;
    }
  }
  return;
}

/* Find nodes that are offd and are not contained in original offd
 * (neighbors of neighbors) */
int new_offd_nodes(int **found, int A_ext_rows, int *A_ext_i, int *A_ext_j, 
		   int num_cols_A_offd, int *col_map_offd, int col_1, 
		   int col_n, int *Sop_i, int *Sop_j)
{
  int i, i1, ii, j, ifound, kk, k1;
  int got_loc, loc_col;

  int min, max;

  int size_offP;

  int *tmp_found, *intDummy;
  int newoff = 0;
  int full_off_procNodes = 0;

  if(num_cols_A_offd)
  {
    size_offP = num_cols_A_offd;
    min = col_map_offd[0];
    max = col_map_offd[num_cols_A_offd-1];
  }
  else
  {
    size_offP = 10;
    min = 0; max = 0;
  }
  tmp_found = hypre_CTAlloc(int, size_offP);

  /* Find nodes that will be added to the off diag list */ 
  for (i = 0; i < A_ext_rows; i++)
  {
    for (j = A_ext_i[i]; j < A_ext_i[i+1]; j++)
    {
      i1 = A_ext_j[j];
      if(i1 < col_1 || i1 >= col_n)
      {
	if(i1 < min || i1 > max)
	{
	  if(newoff >= size_offP)
	  {
	    size_offP += 10;
	    intDummy = hypre_TReAlloc(tmp_found, int, size_offP);
	    tmp_found = intDummy;
	  }
	  if(i1 < min) min = i1;
	  if(i1 > max) max = i1;
	  tmp_found[newoff]=i1;
	  newoff++;
	}
	else
	{
	  ifound = hypre_BinarySearch(col_map_offd,i1,num_cols_A_offd);
	  if(ifound == -1)
	  {
	    ifound = 0;
	    for(ii = 0; ii < newoff; ii++)
	      if(i1 == tmp_found[ii])
		ifound = 1; 
	    if(!ifound)
	    {
	      if(newoff >= size_offP)
	      {
		size_offP = newoff + 10;
		intDummy = hypre_TReAlloc(tmp_found, int, size_offP);
		tmp_found = intDummy;
	      }
	      tmp_found[newoff]=i1;
	      newoff++;
	    }
	  }
	}
      }
    }
  }
  /* Put found in monotone increasing order */
  qsort0(tmp_found,0,newoff-1);

  full_off_procNodes = newoff + num_cols_A_offd;
  /* Set column indices for Sop and A_ext such that offd nodes are
   * negatively indexed */
  for(i = 0; i < num_cols_A_offd; i++)
   {
     for(kk = Sop_i[i]; kk < Sop_i[i+1]; kk++)
     {
       k1 = Sop_j[kk];
       if(k1 < col_1 || k1 >= col_n)
       { 
	 if(newoff < num_cols_A_offd)
	 {  
	   got_loc = hypre_BinarySearch(tmp_found,k1,newoff);
	   if(got_loc > -1)
	     loc_col = got_loc + num_cols_A_offd;
	   else
	     loc_col = hypre_BinarySearch(col_map_offd,k1,
					  num_cols_A_offd);
	 }
	 else
	 {
	   loc_col = hypre_BinarySearch(col_map_offd,k1,
					num_cols_A_offd);
	   if(loc_col == -1)
	     loc_col = hypre_BinarySearch(tmp_found,k1,newoff) +
	       num_cols_A_offd;
	 }
	 if(loc_col < 0)
	 {
	   printf("Could not find node: STOP\n");
	   return(-1);
	 }
	 Sop_j[kk] = -loc_col - 1;
       }
     }
     for (kk = A_ext_i[i]; kk < A_ext_i[i+1]; kk++)
     {
       k1 = A_ext_j[kk];
       if(k1 < col_1 || k1 >= col_n)
       {
	 if(newoff < num_cols_A_offd)
	 {  
	   got_loc = hypre_BinarySearch(tmp_found,k1,newoff);
	   if(got_loc > -1)
	     loc_col = got_loc + num_cols_A_offd;
	   else
	     loc_col = hypre_BinarySearch(col_map_offd,k1,
					  num_cols_A_offd);
	 }
	 else
	 {
	   loc_col = hypre_BinarySearch(col_map_offd,k1,
					num_cols_A_offd);
	   if(loc_col == -1)
	     loc_col = hypre_BinarySearch(tmp_found,k1,newoff) +
	       num_cols_A_offd;
	 }
	 if(loc_col < 0)
	 {
	   printf("Could not find node: STOP\n");
	   return(-1);
	 }
	 A_ext_j[kk] = -loc_col - 1;
       }
     }
   }

  *found = tmp_found;
 
  return newoff;
}
