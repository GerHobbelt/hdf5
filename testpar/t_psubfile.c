/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have          *
 * access to either file, you may request a copy from help@hdfgroup.org.     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/************************************************************

  This example illustrates the concept of virtual dataset
  and is used to simulate the access patterns envisiond for
  HDF5 subfiling. The program creates a collection of 2-dim source
  datasets and writes data to them. Each source dataset is collectively
  generated by either 1 or 2 process ranks which are split off from the 
  parallel MPI_COMM_WORLD group.  It then collectively creates a 2-dim
  virtual dataset utilizing all MPI process ranks and maps each row of this
  virtual dataset to a rank specific row of data in the previously 
  created source datasets. 

  The program closes all datasets, and then reopens the virtual
  dataset, and finds and prints its creation properties.
  Then it reads the values.

  This file is intended for use with HDF5 Library version 1.10

 ************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "hdf5.h"

int mpi_rank;
int mpi_size;
int nerrors = 0;

int mpi_global_rank;
int mpi_global_size;

#include "testpar.h"

#define NFILENAMES 4

const char *FILENAMES[NFILENAMES + 1]={"subfile_a",
                                       "subfile_b",
                                       "subfile_c",
                                       "subfile_d",
                                       NULL};

const char *DSETNAMES[NFILENAMES + 1]={"A",
                                       "B",
                                       "C",
                                       "D",
                                       NULL};


#define FILENAME_BUF_SIZE 1024

#define RANK_ELEMENTS   100

#define VDSFILE         "subfile_vds.h5"
#define DATASET         "VDS"
#define RANK2           2

/* The following can be used for file cleanup
 * after running the test.
 */
const char *SRC_FILE[] = {
    "subfile_a.h5",
    "subfile_b.h5",
    "subfile_c.h5",
    "subfile_d.h5"
};

const char *SRC_DATASET[] = {
    "A",
    "B",
    "C"
};



/*
 * The following are various utility routines used by the tests.
 */

/* Hyperslab layout styles */
#define BYROW           1       /* divide into slabs of rows */
#define BYCOL           2       /* divide into blocks of columns */
#define ZROW            3       /* same as BYCOL except process 0 gets 0 rows */
#define ZCOL            4       /* same as BYCOL except process 0 gets 0 columns */

/*
 * Setup the dimensions of the hyperslab.
 * Two modes--by rows or by columns.
 * Assume dimension rank is 2.
 * BYROW	divide into slabs of rows
 * BYCOL	divide into blocks of columns
 * ZROW		same as BYROW except process 0 gets 0 rows
 * ZCOL		same as BYCOL except process 0 gets 0 columns
 */
static void
slab_set(hsize_t dim0, hsize_t dim1, hsize_t start[], hsize_t count[],
         hsize_t stride[], hsize_t block[], int mode)
{
    switch (mode){
        case BYROW:
            /* Each process takes a slabs of rows. */
            block[0] = dim0/(hsize_t)mpi_size;
            block[1] = dim1;
            stride[0] = block[0];
            stride[1] = block[1];
            count[0] = 1;
            count[1] = 1;
            start[0] = (hsize_t)mpi_rank*block[0];
            start[1] = 0;
            break;

        case BYCOL:
            /* Each process takes a block of columns. */
            block[0] = dim0;
            block[1] = dim1/(hsize_t)mpi_size;
            stride[0] = block[0];
            stride[1] = block[1];
            count[0] = 1;
            count[1] = 1;
            start[0] = 0;
            start[1] = (hsize_t)mpi_rank*block[1];
            break;

        case ZROW:
            /* Similar to BYROW except process 0 gets 0 row */
                block[0] = (mpi_rank ? dim0/(hsize_t)mpi_size : 0);
            block[1] = dim1;
                stride[0] = (mpi_rank ? block[0] : 1);  /* avoid setting stride to 0 */
            stride[1] = block[1];
            count[0] = 1;
            count[1] = 1;
            start[0] = (mpi_rank? (hsize_t)mpi_rank*block[0] : 0);
            start[1] = 0;
            break;

        case ZCOL:
            /* Similar to BYCOL except process 0 gets 0 column */
            block[0] = dim0;
            block[1] = (mpi_rank ? dim1/(hsize_t)mpi_size : 0);
            stride[0] = block[0];
                stride[1] = (mpi_rank ? block[1] : 1);  /* avoid setting stride to 0 */
            count[0] = 1;
            count[1] = 1;
            start[0] = 0;
            start[1] = (mpi_rank? (hsize_t)mpi_rank*block[1] : 0);
            break;

        default:
            /* Unknown mode.  Set it to cover the whole dataset. */
            block[0] = dim0;
            block[1] = dim1;
            stride[0] = block[0];
            stride[1] = block[1];
            count[0] = 1;
            count[1] = 1;
            start[0] = 0;
            start[1] = 0;
            if(VERBOSE_MED) printf("slab_set wholeset\n");
            break;
    } /* end switch */

    if(VERBOSE_MED){
        printf("start[]=(%lu,%lu), count[]=(%lu,%lu), stride[]=(%lu,%lu), block[]=(%lu,%lu), total datapoints=%lu\n",
            (unsigned long)start[0], (unsigned long)start[1], (unsigned long)count[0], (unsigned long)count[1],
            (unsigned long)stride[0], (unsigned long)stride[1], (unsigned long)block[0], (unsigned long)block[1],
            (unsigned long)(block[0]*block[1]*count[0]*count[1]));
    } /* end if */
} /* end slab_set() */

/* File_Access_type bits */
#define FACC_DEFAULT    0x0     /* default */
#define FACC_MPIO       0x1     /* MPIO */
#define FACC_SPLIT      0x2     /* Split File */

/*
 * Create the appropriate File access property list
 */
static hid_t
create_faccess_plist(MPI_Comm comm, MPI_Info info, int l_facc_type)
{
    hid_t ret_pl = -1;
    herr_t ret;                 /* generic return value */

    ret_pl = H5Pcreate (H5P_FILE_ACCESS);
    VRFY((ret_pl >= 0), "H5P_FILE_ACCESS");

    if(l_facc_type == FACC_DEFAULT)
        return (ret_pl);

    if(l_facc_type == FACC_MPIO){
        /* set Parallel access with communicator */
        ret = H5Pset_fapl_mpio(ret_pl, comm, info);
        VRFY((ret >= 0), "");
        ret = H5Pset_all_coll_metadata_ops(ret_pl, TRUE);
        VRFY((ret >= 0), "");
        ret = H5Pset_coll_metadata_write(ret_pl, TRUE);
        VRFY((ret >= 0), "");
        return(ret_pl);
    } /* end if */

    if(l_facc_type == (FACC_MPIO | FACC_SPLIT)){
        hid_t mpio_pl;

        mpio_pl = H5Pcreate (H5P_FILE_ACCESS);
        VRFY((mpio_pl >= 0), "");
        /* set Parallel access with communicator */
        ret = H5Pset_fapl_mpio(mpio_pl, comm, info);
        VRFY((ret >= 0), "");

        /* setup file access template */
        ret_pl = H5Pcreate (H5P_FILE_ACCESS);
        VRFY((ret_pl >= 0), "");

        /* set Parallel access with communicator */
        ret = H5Pset_fapl_split(ret_pl, ".meta", mpio_pl, ".raw", mpio_pl);
        VRFY((ret >= 0), "H5Pset_fapl_split succeeded");
        H5Pclose(mpio_pl);
        return(ret_pl);
    } /* end if */

    /* unknown file access types */
    return ret_pl;
} /* end create_faccess_plist() */



/*-------------------------------------------------------------------------
 * Function:    generate_test_files
 *
 * Purpose:     This function is called to produce HDF5 dataset files
 *              which will eventually be used as the 'src' files in a
 *              containing Virtual Data Set (VDS) file.
 *
 *              Since data will be read back and validated, we generate
 *              data in a predictable manner rather than randomly.
 *              For now, we simply use the global mpi_rank of the writing
 *              process as a starting component for the data generation.
 *              Subsequent writes are increments from the initial start
 *              value.
 *
 * Return:      Success: 0
 *
 *              Failure: 1
 *
 * Programmer:  Richard Warren
 *              10/1/17
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static int
generate_test_files( MPI_Comm comm, int group_id )
{
    const char *fcn_name = "generate_test_files()";
    const char *failure_mssg = NULL;
    const char *dset_name = NULL;

    char data_filename[FILENAME_BUF_SIZE];

    int i, status;
    int group_size;
    int group_rank;
    int local_failure = 0;
    int global_failures = 0;
    int start_value;
    int   written[RANK_ELEMENTS],          /* Data to write */
        retrieved[RANK_ELEMENTS];          /* Data read in */

    hsize_t srcspace_dims[2] = {2, RANK_ELEMENTS};
    hsize_t memspace_dims[2] = {1, RANK_ELEMENTS};
    hsize_t start[2];                   /* for hyperslab setting */
    hsize_t count[2], stride[2];        /* for hyperslab setting */
    hsize_t block[2];                   /* for hyperslab setting */

    hsize_t orig_size=RANK_ELEMENTS;   	   /* Original dataset dim size */
    hid_t fid   = -1;
    hid_t fs;   		/* File dataspace ID */
    hid_t ms;   		/* Memory dataspace ID */
    hid_t dataset   = -1;
    hid_t fapl   = -1;
    hid_t dcpl   = -1;
    hbool_t pass = true;
    float *data_slice = NULL;
    herr_t ret;

    HDassert(comm != MPI_COMM_NULL);
    status = MPI_Comm_rank(comm, &group_rank);
    VRFY((status == MPI_SUCCESS), "MPI_Comm_rank succeeded");
    status = MPI_Comm_size(comm, &group_size);
    VRFY((status == MPI_SUCCESS), "MPI_Comm_size succeeded");

    /* Some error reporting use the globals: mpi_rank and/or mpi_size */
    mpi_rank = group_rank;
    if((mpi_size = group_size) == 1)
        srcspace_dims[0] = 1;

    /* setup file access template */
    fapl = create_faccess_plist(comm, MPI_INFO_NULL, FACC_MPIO);
    VRFY((fapl >= 0), "create_faccess_plist succeeded");

    h5_fixname(FILENAMES[group_id], fapl, data_filename, sizeof data_filename);
    dset_name =DSETNAMES[group_id];
    /* -------------------
     * START AN HDF5 FILE
     * -------------------*/
    /* create the file collectively */
    fid = H5Fcreate(data_filename, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
    VRFY((fid >= 0), "H5Fcreate succeeded");

    /* Release file-access template */
    ret = H5Pclose(fapl);
    VRFY((ret >= 0), "H5Pclose succeeded");

    /* --------------------------------------------------------------
     * Define the dimensions of the overall datasets and create them.
     * ------------------------------------------------------------- */

    /* set up dataset storage chunk sizes and creation property list */
    dcpl = H5Pcreate(H5P_DATASET_CREATE);
    VRFY((dcpl >= 0), "H5Pcreate succeeded");

    /* setup dimensionality object */
    /* File space is the global view */
    fs = H5Screate_simple (2, srcspace_dims, NULL);
    VRFY((fs >= 0), "H5Screate_simple succeeded");

    /* Collectively create a dataset */
    dataset = H5Dcreate2(fid, dset_name, H5T_NATIVE_INT, fs, H5P_DEFAULT, dcpl, H5P_DEFAULT);
    VRFY((dataset >= 0), "H5Dcreate2 succeeded");

    /* release resource */
    ret = H5Pclose(dcpl);
    VRFY((ret >= 0), "H5Pclose succeeded");

    slab_set(srcspace_dims[0], srcspace_dims[1], start, count, stride, block, BYROW);

    /* -------------------------
     * Test writing to dataset
     * -------------------------*/
    /* create a memory dataspace independently */
    /* Memory space is a local view */
    ms = H5Screate_simple(2, memspace_dims, NULL);
    VRFY((ms >= 0), "H5Screate_simple succeeded");

    ret = H5Sselect_hyperslab(fs, H5S_SELECT_SET, start, NULL, count, block);
    VRFY((ret >= 0), "H5Sselect_hyperslab succeeded");

    /* put some trivial (rank specific) data in the data_array */
    start_value = ((int)(orig_size) * mpi_global_rank);
    for(i = 0; i < (int)(orig_size); i++)
        written[i] = start_value + i;
    MESG("data array initialized");
    if(VERBOSE_MED) {
        MESG("writing at offset zero: ");
        for(i = 0; i < (int)orig_size; i++)
            printf("%s%d", i?", ":"", written[i]);
        printf("\n");
    }
    ret = H5Dwrite(dataset, H5T_NATIVE_INT, ms, fs, H5P_DEFAULT, written);
    VRFY((ret >= 0), "H5Dwrite succeeded");

    /* -------------------------
     * Read initial data from dataset.
     * -------------------------*/
    ret = H5Dread(dataset, H5T_NATIVE_INT, ms, fs, H5P_DEFAULT, retrieved);
    VRFY((ret >= 0), "H5Dread succeeded");
    for (i=0; i<(int)orig_size; i++)
        if(written[i]!=retrieved[i]) {
            printf("Line #%d: written!=retrieved: written[%d]=%d, retrieved[%d]=%d\n",__LINE__,
                i,written[i], i,retrieved[i]);
            nerrors++;
        }
    if(VERBOSE_MED){
        MESG("read at offset zero: ");
        for (i=0; i<(int)orig_size; i++)
            printf("%s%d", i?", ":"", retrieved[i]);
        printf("\n");
    }

    ret = H5Dclose(dataset);
    VRFY((ret >= 0), "H5Dclose succeeded");

    ret = H5Fclose(fid);
    VRFY((ret >= 0), "H5Fclose succeeded");

    /* collect results from other processes.
     * Only overwrite the failure message if no previous error
     * has been detected
     */
    local_failure = ( nerrors > 0 ? 1 : 0 );

    /* This is a global all reduce (NOT group specific) */
    if(MPI_Allreduce(&local_failure, &global_failures, 1,
                       MPI_INT, MPI_SUM, MPI_COMM_WORLD) != MPI_SUCCESS) {
        if ( pass ) {
            pass = FALSE;
            failure_mssg = "MPI_Allreduce() failed.\n";
        }
    } else if ( global_failures > 0 ) {
        pass = FALSE;
        failure_mssg = "One or more processes report failure.\n";
    }

    /* report results */
    if(mpi_global_rank == 0) {
        if(pass)
            HDfprintf(stdout, "Done.\n");
        else {
            HDfprintf(stdout, "FAILED.\n");
            HDfprintf(stdout, "%s: failure_mssg = \"%s\"\n",
                      fcn_name, failure_mssg);
        } /* end else */
    } /* end if */

    /* free data_slice if it has been allocated */
    if(data_slice != NULL) {
        HDfree(data_slice);
        data_slice = NULL;
    } /* end if */

    return !pass;
} /* end generate_test_file() */


/*-------------------------------------------------------------------------
 * Function:    generate_vds_container
 *
 * Purpose:     Create a parallel VDS container using the source files
 *              previously created in generate_test_files().
 *
 * Return:      Success: 0
 *
 *              Failure: 1
 *
 * Programmer:  Richard Warren
 *              10/1/17
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static int
generate_vds_container(MPI_Comm comm)
{
    hid_t        file, src_space, vspace, dset; /* Handles */ 
    hid_t        odd_space = -1;
    hid_t        fapl;
    hid_t        dcpl;
    herr_t       status;
    hsize_t      n_elements = RANK_ELEMENTS;
    hsize_t      vdsdims[2] = {2, RANK_ELEMENTS},      /* Virtual datasets dimension */
                 srcdims[2] = {2, RANK_ELEMENTS},      /* Source datasets dimensions */
                 extradims[2] = {1, RANK_ELEMENTS},
                 start[2],                             /* Hyperslab parameters */
                 count[2],
                 block[2];
    hsize_t      start_out[2],
                 stride_out[2],
                 count_out[2],
                 block_out[2];
    int          *rdata;
    int          expected = 0;
    int          i, j, k, l;
    int          fill_value = -1;            /* Fill value for VDS */
    int          local_failure = 0;
    int          global_failures = 0;
    int          group_size;
    int          group_rank;
    int          n_groups;

    H5D_layout_t layout;                     /* Storage layout */
    size_t       num_map;                    /* Number of mappings */
    size_t       len;                        /* Length of the string; also a return value */
    char         *filename;
    char         *dsetname;
    hssize_t     nblocks;
    hsize_t      *buf = NULL;                /* Buffer to hold hyperslab coordinates */

    HDassert(comm != MPI_COMM_NULL);
    status = MPI_Comm_rank(comm, &group_rank);
    VRFY((status == MPI_SUCCESS), "MPI_Comm_rank succeeded");
    status = MPI_Comm_size(comm, &group_size);
    VRFY((status == MPI_SUCCESS), "MPI_Comm_size succeeded");

    n_groups = group_size/2;
    vdsdims[0] = (hsize_t)group_size;	/* [mpi_size][RANK_ELEMENTS] */

    /* setup for error reporting and slab_set() */
    mpi_rank = group_rank;
    mpi_size = group_size;
    n_elements = (hsize_t)mpi_size * vdsdims[1];

    /* setup file access template */
    fapl = create_faccess_plist(comm, MPI_INFO_NULL, FACC_MPIO);
    VRFY((fapl >= 0), "create_faccess_plist succeeded");

    /* Create file in which virtual dataset will be stored. */
    file = H5Fcreate (VDSFILE, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
    VRFY((file >= 0), "H5Fcreate succeeded");

    /* Create VDS dataspace.  */
    vspace = H5Screate_simple (RANK2, vdsdims, NULL);
    VRFY((vspace >= 0), "H5Screate_simple succeeded");

    /* Set VDS creation property. */
    dcpl = H5Pcreate (H5P_DATASET_CREATE);
    VRFY((dcpl >= 0), "H5Pcreate succeeded");

    status = H5Pset_fill_value (dcpl, H5T_NATIVE_INT, &fill_value);
    VRFY((status >= 0), "H5Pset_fill_value succeeded");

    start[0] = 0;
    start[1] = 0;
    count[0] = 1;
    count[1] = 1;
    block[0] = 2;
    block[1] = 100;

    /*
     * Build the mappings.
     * Selections in the source datasets are H5S_ALL.
     * In the virtual dataset we select the first, the second and the third rows 
     * and map each row to the data in the corresponding source dataset. 
     */
    src_space = H5Screate_simple (RANK2, srcdims, NULL);
    VRFY((src_space >= 0), "H5Screate_simple succeeded");

    /* Each src dataset is a 2D array (2 x 100) 
     * and we want to select the entire space.
     * The exception to this is if there are an ODD number
     * of MPI ranks; which forced us to create 1 additional
     * dataset of size (1 x 100).
     */
    status = H5Sselect_hyperslab(src_space, H5S_SELECT_SET, start, NULL, count, block);
    VRFY((status >= 0), "H5Sselect_hyperslab succeeded");

    for(i = 0; i < n_groups; i++) {
        start[0] = (hsize_t)i*2;
        /* Select i-th row in the virtual dataset; selection in the source datasets is the same. */
        status = H5Sselect_hyperslab (vspace, H5S_SELECT_SET, start, NULL, count, block);
        VRFY((status >= 0), "H5Sselect_hyperslab succeeded");
        status = H5Pset_virtual (dcpl, vspace, SRC_FILE[i], SRC_DATASET[i], src_space);
        VRFY((status >= 0), "H5Pset_virtual succeeded");
    } /* end for */

    /* A final source file is added if the group size is odd {1,3,5} */
    if(group_size % 2) {
        block[0] = 1;
        srcdims[0] = 1;
        odd_space = H5Screate_simple (RANK2, extradims, NULL);
        VRFY((odd_space >= 0), "H5Screate_simple succeeded");
        start[0] += 2;
        status = H5Sselect_hyperslab(src_space, H5S_SELECT_SET, start, NULL, count, block);
        VRFY((status >= 0), "H5Sselect_hyperslab succeeded");
        status = H5Sselect_hyperslab (vspace, H5S_SELECT_SET, start, NULL, count, block);
        VRFY((status >= 0), "H5Sselect_hyperslab succeeded");
        status = H5Pset_virtual (dcpl, vspace, SRC_FILE[i], SRC_DATASET[i], odd_space);
        VRFY((status >= 0), "H5Pset_virtual succeeded");
    } /* end if */

    /* Create a virtual dataset. */
    dset = H5Dcreate2 (file, DATASET, H5T_NATIVE_INT, vspace, H5P_DEFAULT, dcpl, H5P_DEFAULT);
    VRFY((dset >= 0), "H5Dcreate2 succeeded");
    status = H5Sclose (vspace);
    VRFY((status >= 0), "H5Sclose succeeded");
    status = H5Sclose (src_space);
    VRFY((status >= 0), "H5Sclose succeeded");
    if(odd_space >= 0) {
        status = H5Sclose (odd_space);
        VRFY((status >= 0), "H5Sclose succeeded");
    } /* end if */
    status = H5Dclose (dset);
    VRFY((status >= 0), "H5Dclose succeeded");
    status = H5Fclose (file);
    VRFY((status >= 0), "H5Fclose succeeded");

    /*
     * Now we begin the read section of this example.
     */

    /*
     * Open the file and virtual dataset.
     */
    file = H5Fopen (VDSFILE, H5F_ACC_RDONLY, fapl);
    VRFY((file >= 0), "H5Fopen succeeded");
    dset = H5Dopen2 (file, DATASET, H5P_DEFAULT);
    VRFY((dset >= 0), "H5Dopen2 succeeded");

    /*
     * Get creation property list and mapping properties.
     */
    dcpl = H5Dget_create_plist (dset);
    VRFY((dcpl >= 0), "H5Dget_create_plist succeeded");

    /*
     * Get storage layout.
     */
    layout = H5Pget_layout (dcpl);
    if(group_rank == 0) {
        if (H5D_VIRTUAL == layout)
            printf(" Dataset has a virtual layout \n");
        else
            printf(" Wrong layout found \n");
    } /* end if */

     /*
      * Find the number of mappings.
      */
    status = H5Pget_virtual_count (dcpl, &num_map);
    VRFY((status >= 0), "H5Pget_virtual_count succeeded");
    if(group_rank == 0)
        printf(" Number of mappings is %lu\n", (unsigned long)num_map);

     /*
      * Get mapping parameters for each mapping.
      */
    for(i = 0; i < (int)num_map; i++) {
        if (group_rank == 0) {
            printf(" Mapping %d \n", i);
            printf("         Selection in the virtual dataset ");
        } /* end if */

        /* Get selection in the virtual dataset */
        vspace = H5Pget_virtual_vspace (dcpl, (size_t)i);
        VRFY((vspace >= 0), "H5Pget_virtual_vspace succeeded");

        /* Make sure that this is a hyperslab selection and then print information. */
        if(H5Sget_select_type(vspace) == H5S_SEL_HYPERSLABS) { 
            nblocks = H5Sget_select_hyper_nblocks (vspace);
            buf = (hsize_t *)malloc(sizeof(hsize_t)*2*RANK2*(hsize_t)nblocks);
            status = H5Sget_select_hyper_blocklist(vspace, (hsize_t)0, (hsize_t)nblocks, buf);
            if(group_rank == 0) {
                for(l=0; l<nblocks; l++) {
                    printf("(");
                    for (k=0; k<RANK2-1; k++)
                        printf("%d,", (int)buf[k]);
                    printf("%d ) - (", (int)buf[k]);
                    for (k=0; k<RANK2-1; k++) 
                        printf("%d,", (int)buf[RANK2+k]);
                    printf("%d)\n", (int)buf[RANK2+k]);
                } /* end for */
            } /* end if */

            /* We also can use new APIs to get start, stride, count and block */
            if(H5Sis_regular_hyperslab(vspace)) {
                status = H5Sget_regular_hyperslab (vspace, start_out, stride_out, count_out, block_out);
                if(group_rank == 0) {
                    printf("         start  = [%llu, %llu] \n", (unsigned long long)start_out[0], (unsigned long long)start_out[1]);
                    printf("         stride = [%llu, %llu] \n", (unsigned long long)stride_out[0], (unsigned long long)stride_out[1]);
                    printf("         count  = [%llu, %llu] \n", (unsigned long long)count_out[0], (unsigned long long)count_out[1]);
                    printf("         block  = [%llu, %llu] \n", (unsigned long long)block_out[0], (unsigned long long)block_out[1]);
                } /* end if */
            } /* end if */
        } /* end if */

        /* Get source file name. */
        len = (size_t)H5Pget_virtual_filename (dcpl, (size_t)i, NULL, 0);
        filename = (char *)malloc(len*sizeof(char)+1);
        H5Pget_virtual_filename (dcpl, (size_t)i, filename, len+1);
        if(group_rank == 0)
            printf("         Source filename %s\n", filename);

        /* Get source dataset name. */
        len = (size_t)H5Pget_virtual_dsetname (dcpl, (size_t)i, NULL, 0);
        dsetname = (char *)malloc((size_t)len*sizeof(char)+1);
        H5Pget_virtual_dsetname (dcpl, (size_t)i, dsetname, len+1);
        if(group_rank == 0)
            printf("         Source dataset name %s\n", dsetname);

        /* Get selection in the source dataset. */
        if(group_rank == 0)
            printf("         Selection in the source dataset ");
        src_space = H5Pget_virtual_srcspace (dcpl, (size_t)i);

        /* Make sure it is ALL selection and then print the coordinates. */
        if(H5Sget_select_type(src_space) == H5S_SEL_ALL) {
            if (group_rank == 0)
                printf("(0) - (99) \n");
        } /* end if */
        if (group_rank == 0)
            puts("");
        H5Sclose(vspace);
        H5Sclose(src_space);
        free(filename);
        free(dsetname);
        free(buf);
    } /* end for */

    /*
     * Read the data using the default properties.
     */
    rdata = (int *)malloc(sizeof(int)*n_elements);
    VRFY((rdata != NULL), "malloc succeeded");

    status = H5Dread (dset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, rdata);
    VRFY((status >= 0), "H5Dread succeeded");
    /* MPI rank 0 does the validation check */
    if(mpi_global_rank == 0) {
        expected = 0;
        k=0;
        for(i=0; i< (int)vdsdims[0]; i++) {
            for(j=0; j<(int)vdsdims[1]; j++) {
                if (rdata[k++] != expected++)
                    local_failure++;
            } /* end for */
        } /* end for */
    } /* end if */

    /*
     * Close and release resources.
     */
    status = H5Pclose (dcpl);
    status = H5Dclose (dset);
    status = H5Fclose (file);

    /* collect results from other processes.
     * Only overwrite the failure message if no previous error
     * has been detected
     */

    status = MPI_Allreduce( &local_failure, &global_failures, 1,
			    MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    VRFY((status == MPI_SUCCESS), "MPI_Allreduce succeeded");

    return global_failures;
} /* end generate_vds_container() */

/* #define SERIAL_ACCESS */

/*-------------------------------------------------------------------------
 * Function:    independent_read_vds
 *
 * Purpose:     Each mpi process reads 1/Nth of the data contained in a
 *              VDS file previously created by generate_vds_container().
 *              The 'N' in this case is the number of parallel ranks in
 *              MPI_COMM_WORLD.
 *              The function reads thru the VDS file (as opposed to reading
 *              the component 'source' files) and is treated as a normal
 *              HDF5 dataset.
 *
 * Return:      Success: 0
 *
 *              Failure: 1
 *
 * Programmer:  Richard Warren
 *              10/1/17
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static int
independent_read_vds(MPI_Comm comm)
{
    int          status;
    int          expected = 100;
    int          local_failure = 0;
    int          global_failures = 0;
    hid_t        vfile = -1;                       /* File with virtual dset */
    hid_t        vdset = -1;                       /* Virtual dataset */
    hid_t        fapl;
    hid_t        file_dataspace;                   /* File dataspace ID */
    hid_t        mem_dataspace;                    /* memory dataspace ID */

    hsize_t      vdsdims[2] = {2, RANK_ELEMENTS};  /* Virtual datasets dimension */
    hsize_t      stride[2],
                 start[2],                         /* Hyperslab parameters */
                 count[2],
                 block[2];
    int          rdata[8][RANK_ELEMENTS];
    int          i;
    herr_t       ret;


    HDassert(comm != MPI_COMM_NULL);
    status = MPI_Comm_rank(comm, &mpi_rank);
    VRFY((status == MPI_SUCCESS), "MPI_Comm_rank succeeded");
    status = MPI_Comm_size(comm, &mpi_size);
    VRFY((status == MPI_SUCCESS), "MPI_Comm_size succeeded");


    /* setup file access template */
    fapl = create_faccess_plist(comm, MPI_INFO_NULL, FACC_MPIO);
    VRFY((fapl >= 0), "create_faccess_plist succeeded");

    /* The VDS is a 2D object containing 'mpi_size' rows
     * with 'RANK_ELEMENTS' number of columns.
     * 
     */
    vdsdims[0] = (hsize_t)mpi_size;
    slab_set(vdsdims[0], vdsdims[1], start, count, stride, block, BYROW);
#ifdef SERIAL_ACCESS
    if (mpi_rank != 0) {
        MPI_Status stat;
        int flag=0;
        MPI_Recv(&flag, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &stat);
    }
    vfile = H5Fopen(VDSFILE, H5F_ACC_RDWR, H5P_DEFAULT);
#else
    vfile = H5Fopen(VDSFILE, H5F_ACC_RDWR, fapl);
#endif

    VRFY((vfile >= 0), "H5Fopen succeeded");

    /* No longer need the fapl */
    ret = H5Pclose(fapl);
    VRFY((ret >= 0), "H5Pclose succeeded");
    
    vdset = H5Dopen2(vfile, DATASET, H5P_DEFAULT);
    VRFY((vdset >= 0), "H5Dopen2 succeeded");

    file_dataspace = H5Dget_space (vdset);
    VRFY((file_dataspace >= 0), "H5Dget_space succeeded");
    ret = H5Sselect_hyperslab(file_dataspace, H5S_SELECT_SET, start, stride, count, block);
    VRFY((ret >= 0), "H5Sset_hyperslab succeeded");

    /* create a memory dataspace independently */
    mem_dataspace = H5Screate_simple (2, block, NULL);
    VRFY((mem_dataspace >= 0), "");


#ifdef SERIAL_ACCESS
    ret = H5Dread(vdset, H5T_NATIVE_INT, mem_dataspace, file_dataspace, H5P_DEFAULT, rdata[0]);
#else    
    ret = H5Dread(vdset, H5T_NATIVE_INT, mem_dataspace, file_dataspace, H5P_DEFAULT, rdata[0]);
#endif
    VRFY((ret >= 0), "H5Dread succeeded");
    ret = H5Dclose(vdset);
    VRFY((ret >= 0), "H5Dclose succeeded");
    ret = H5Fclose(vfile);
    VRFY((ret >= 0), "H5Fclose succeeded");

#ifdef SERIAL_ACCESS
    {
        int mpi_next = mpi_rank+1;
        if (mpi_next < mpi_size)
            MPI_Send(&mpi_next, 1, MPI_INT, mpi_next, 0x0acc, comm);
    } /* end block */
#endif
    expected *= mpi_rank;
    for(i=0; i<100; i++) {
        if(rdata[0][i] != expected++)
            local_failure++;
    } /* end for */

    status = MPI_Allreduce( &local_failure, &global_failures, 1,
			    MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    VRFY((status == MPI_SUCCESS), "MPI_Allreduce succeeded");

    return global_failures;
} /* end independent_read_vds() */


int
main (int argc, char **argv)
{
    int nerrs = 0;
    int which_group = 0;
    MPI_Comm group_comm = MPI_COMM_NULL;

    if((MPI_Init(&argc, &argv)) != MPI_SUCCESS) {
        HDfprintf(stderr, "FATAL: Unable to initialize MPI\n");
        HDexit(EXIT_FAILURE);
    } /* end if */

    if((MPI_Comm_rank(MPI_COMM_WORLD, &mpi_global_rank)) != MPI_SUCCESS) {
        HDfprintf(stderr, "FATAL: MPI_Comm_rank returned an error\n");
        HDexit(EXIT_FAILURE);
    } /* end if */

    if((MPI_Comm_size(MPI_COMM_WORLD, &mpi_global_size)) != MPI_SUCCESS) {
        HDfprintf(stderr, "FATAL: MPI_Comm_size returned an error\n");
        HDexit(EXIT_FAILURE);
    } /* end if */

    /* Attempt to turn off atexit post processing so that in case errors
     * happen during the test and the process is aborted, it will not get
     * hang in the atexit post processing in which it may try to make MPI
     * calls.  By then, MPI calls may not work.
     */
    if(H5dont_atexit() < 0)
        printf("Failed to turn off atexit processing. Continue.\n");
    H5open();
    h5_show_hostname();

    mpi_rank = mpi_global_rank;
    mpi_size = mpi_global_size;

    if((mpi_size < 4) || (mpi_size > 6)) {
        nerrs++;

        if(mpi_global_rank == 0)
            HDprintf("MPI size = %d, need at least 4 processes, max = 6.  Exiting.\n", mpi_size);
        goto finish;
    } /* end if */

    if(mpi_rank == 0) {
        HDfprintf(stdout, "============================================\n");
        HDfprintf(stdout, "Subfiling functionality (parallel VDS) tests\n");
        HDfprintf(stdout, "        mpi_size     = %d\n", mpi_size);
        HDfprintf(stdout, "============================================\n");
    } /* end if */

    /* ------  Create MPI groups of 2 ------
     *
     * We split MPI_COMM_WORLD into n groups of size 2.
     * The resulting communicators will be used to generate
     * HDF dataset files which in turn will be opened in parallel and the
     * contents verified in the second read test below.
     */

    which_group = mpi_rank / 2;

    if((MPI_Comm_split(MPI_COMM_WORLD,
                       which_group,
                       0,
                       &group_comm)) != MPI_SUCCESS) {

        HDfprintf(stderr, "FATAL: MPI_Comm_split returned an error\n");
        HDexit(EXIT_FAILURE);
    } /* end if */

    /* ------  Generate all source files ------ */
    nerrs += generate_test_files( group_comm, which_group );

    if(nerrs > 0) {
        if(mpi_global_rank == 0)
            HDprintf("    SubFile construction failed -- skipping tests.\n");
        goto finish;
    } /* end if */

    /* We generate a containing VDS file and read the data 
     * from the multiple containers produced in 'generate_test_files'.
     */
    nerrs += generate_vds_container( MPI_COMM_WORLD );

    if(nerrs > 0) {
        if(mpi_global_rank == 0)
            HDprintf("    VDS file construction failed -- skipping tests.\n");
        goto finish;
    } /* end if */

    nerrs += independent_read_vds( MPI_COMM_WORLD );

    if(nerrs > 0)
        if(mpi_global_rank == 0)
            HDprintf("    VDS file independent read failed.\n");

finish:
    if((group_comm != MPI_COMM_NULL) &&
            (MPI_Comm_free(&group_comm)) != MPI_SUCCESS)
        HDfprintf(stderr, "MPI_Comm_free failed!\n");

    /* make sure all processes are finished before final report, cleanup
     * and exit.
     */
    MPI_Barrier(MPI_COMM_WORLD);

    if(mpi_global_rank == 0) {           /* only process 0 reports */
        int i;
        const char *header = "Subfiling validation tests";

        HDfprintf(stdout, "===================================\n");
        if(nerrs > 0) {
            HDfprintf(stdout, "***%s detected %d failures***\n", header, nerrs);
            H5_FAILED();
        } /* end if */
        else {
            HDfprintf(stdout, "%s finished with no failures\n", header);
            PASSED();
        } /* end else */
        HDfprintf(stdout, "===================================\n");

        /* File cleanup */
        for(i=0; i<NFILENAMES; i++)
            HDremove(SRC_FILE[i]);
        HDremove(VDSFILE);
    } /* end if */

    /* close HDF5 library */
    if(H5close() != SUCCEED)
        HDfprintf(stdout, "H5close() failed. (Ignoring)\n");

    /* MPI_Finalize must be called AFTER H5close which may use MPI calls */
    MPI_Finalize();

    /* cannot just return (nerrs) because exit code is limited to 1byte */
    return (nerrs > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
} /* end main() */

