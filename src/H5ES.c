/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://www.hdfgroup.org/licenses.               *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*-------------------------------------------------------------------------
 *
 * Created:     H5ES.c
 *              Apr  6 2020
 *              Quincey Koziol
 *
 * Purpose:     Implements an "event set" for managing asynchronous
 *                      operations.
 *
 *                      Please see the asynchronous I/O RFC document
 *                      for a full description of how they work, etc.
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/

#include "H5ESmodule.h" /* This source code file is part of the H5ES module */

/***********/
/* Headers */
/***********/
#include "H5private.h"   /* Generic Functions			*/
#include "H5Eprivate.h"  /* Error handling		  	*/
#include "H5ESpkg.h"     /* Event Sets                           */
#include "H5FLprivate.h" /* Free Lists                           */
#include "H5Iprivate.h"  /* IDs                                  */

/****************/
/* Local Macros */
/****************/

/******************/
/* Local Typedefs */
/******************/

/********************/
/* Package Typedefs */
/********************/

/********************/
/* Local Prototypes */
/********************/

/*********************/
/* Package Variables */
/*********************/

/*****************************/
/* Library Private Variables */
/*****************************/

/*******************/
/* Local Variables */
/*******************/

/*-------------------------------------------------------------------------
 * Function:    H5EScreate
 *
 * Purpose:     Creates an event set.
 *
 * Return:      Success:    An ID for the event set
 *              Failure:    H5I_INVALID_HID
 *
 * Programmer:  Quincey Koziol
 *              Wednesday, April 8, 2020
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5EScreate(void)
{
    H5ES_t *es;                          /* Pointer to event set object */
    hid_t   ret_value = H5I_INVALID_HID; /* Return value */

    FUNC_ENTER_API(H5I_INVALID_HID)
    H5TRACE0("i", "");

    /* Create the new event set object */
    if (NULL == (es = H5ES__create()))
        HGOTO_ERROR(H5E_EVENTSET, H5E_CANTCREATE, H5I_INVALID_HID, "can't create event set")

    /* Register the new event set to get an ID for it */
    if ((ret_value = H5I_register(H5I_EVENTSET, es, TRUE)) < 0)
        HGOTO_ERROR(H5E_EVENTSET, H5E_CANTREGISTER, H5I_INVALID_HID, "can't register event set")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5EScreate() */

/*-------------------------------------------------------------------------
 * Function:    H5ESget_count
 *
 * Purpose:     Retrieve the # of events in an event set
 *
 * Return:      SUCCEED / FAIL
 *
 * Programmer:  Quincey Koziol
 *              Wednesday, April 8, 2020
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5ESget_count(hid_t es_id, size_t *count /*out*/)
{
    H5ES_t *es;                  /* Event set */
    herr_t  ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "ix", es_id, count);

    /* Check arguments */
    if (NULL == (es = H5I_object_verify(es_id, H5I_EVENTSET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid event set identifier")

    /* Retrieve the count, if non-NULL */
    if (count)
        *count = H5ES__list_count(&es->active);

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5ESget_count() */

/*-------------------------------------------------------------------------
 * Function:    H5ESget_op_counter
 *
 * Purpose:     Retrieve the counter that will be assigned to the next operation
 *              inserted into the event set.
 *
 * Note:        This is designed for wrapper libraries mainly, to use as a
 *              mechanism for matching operations inserted into the event
 *              set with [possible] errors that occur.
 *
 * Return:      SUCCEED / FAIL
 *
 * Programmer:  Quincey Koziol
 *              Fiiday, November 6, 2020
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5ESget_op_counter(hid_t es_id, uint64_t *op_counter /*out*/)
{
    H5ES_t *es;                  /* Event set */
    herr_t  ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "ix", es_id, op_counter);

    /* Check arguments */
    if (NULL == (es = H5I_object_verify(es_id, H5I_EVENTSET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid event set identifier")

    /* Retrieve the operation counter, if non-NULL */
    if (op_counter)
        *op_counter = es->op_counter;

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5ESget_op_counter() */

/*-------------------------------------------------------------------------
 * Function:    H5ESwait
 *
 * Purpose:     Wait (with timeout) for operations in event set to complete
 *
 * Note:        Timeout value is in ns, and is for the H5ESwait call, not each
 *              individual operation.   For example: if '10' is passed as
 *              a timeout value and the event set waited 4ns for the first
 *              operation to complete, the remaining operations would be
 *              allowed to wait for at most 6ns more.  i.e. the timeout value
 *              is "used up" across all operations, until it reaches 0, then
 *              any remaining operations are only checked for completion, not
 *              waited on.
 *
 * Note:        This call will stop waiting on operations and will return
 *              immediately if an operation fails.  If a failure occurs, the
 *              value returned for the # of operations in progress may be
 *              inaccurate.
 *
 * Return:      SUCCEED / FAIL
 *
 * Programmer:  Quincey Koziol
 *              Monday, July 13, 2020
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5ESwait(hid_t es_id, uint64_t timeout, size_t *num_in_progress /*out*/, hbool_t *op_failed /*out*/)
{
    H5ES_t *es;                  /* Event set */
    herr_t  ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE4("e", "iULxx", es_id, timeout, num_in_progress, op_failed);

    /* Check arguments */
    if (NULL == (es = H5I_object_verify(es_id, H5I_EVENTSET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid event set identifier")
    if (NULL == num_in_progress)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "NULL num_in_progress pointer")
    if (NULL == op_failed)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "NULL op_failed pointer")

    /* Wait for operations */
    if (H5ES__wait(es, timeout, num_in_progress, op_failed) < 0)
        HGOTO_ERROR(H5E_EVENTSET, H5E_CANTWAIT, FAIL, "can't wait on operations")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5ESwait() */

/*-------------------------------------------------------------------------
 * Function:    H5ESget_err_status
 *
 * Purpose:     Check if event set has failed operations
 *
 * Return:      SUCCEED / FAIL
 *
 * Programmer:  Quincey Koziol
 *              Thursday, October 15, 2020
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5ESget_err_status(hid_t es_id, hbool_t *err_status /*out*/)
{
    H5ES_t *es;                  /* Event set */
    herr_t  ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "ix", es_id, err_status);

    /* Check arguments */
    if (NULL == (es = H5I_object_verify(es_id, H5I_EVENTSET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid event set identifier")

    /* Retrieve the error flag, if non-NULL */
    if (err_status)
        *err_status = es->err_occurred;

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5ESget_err_status() */

/*-------------------------------------------------------------------------
 * Function:    H5ESget_err_count
 *
 * Purpose:     Retrieve # of failed operations
 *
 * Note:        Does not wait for active operations to complete, so count may
 *              not include all failures.
 *
 * Return:      SUCCEED / FAIL
 *
 * Programmer:  Quincey Koziol
 *              Thursday, October 15, 2020
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5ESget_err_count(hid_t es_id, size_t *num_errs /*out*/)
{
    H5ES_t *es;                  /* Event set */
    herr_t  ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "ix", es_id, num_errs);

    /* Check arguments */
    if (NULL == (es = H5I_object_verify(es_id, H5I_EVENTSET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid event set identifier")

    /* Retrieve the error flag, if non-NULL */
    if (num_errs) {
        if (es->err_occurred)
            *num_errs = H5ES__list_count(&es->failed);
        else
            *num_errs = 0;
    } /* end if */

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5ESget_err_count() */

/*-------------------------------------------------------------------------
 * Function:    H5ESget_err_info
 *
 * Purpose:     Retrieve information about failed operations
 *
 * Note:        The strings retrieved for each error info must be released
 *              by calling H5free_memory().
 *
 * Return:      SUCCEED / FAIL
 *
 * Programmer:  Quincey Koziol
 *              Friday, November 6, 2020
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5ESget_err_info(hid_t es_id, size_t num_err_info, H5ES_err_info_t err_info[] /*out*/,
                 size_t *num_cleared /*out*/)
{
    H5ES_t *es;                  /* Event set */
    herr_t  ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE4("e", "izxx", es_id, num_err_info, err_info, num_cleared);

    /* Check arguments */
    if (NULL == (es = H5I_object_verify(es_id, H5I_EVENTSET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid event set identifier")
    if (0 == num_err_info)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "err_info array size is 0")
    if (NULL == err_info)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "NULL err_info array pointer")
    if (NULL == num_cleared)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "NULL errors cleared pointer")

    /* Retrieve the error information */
    if (H5ES__get_err_info(es, num_err_info, err_info, num_cleared) < 0)
        HGOTO_ERROR(H5E_EVENTSET, H5E_CANTGET, FAIL, "can't retrieve error info for failed operation(s)")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5ESget_err_info() */

/*-------------------------------------------------------------------------
 * Function:    H5ESclose
 *
 * Purpose:     Closes an event set.
 *
 * Note:        Fails if active operations are present.
 *
 * Return:      SUCCEED / FAIL
 *
 * Programmer:  Quincey Koziol
 *              Wednesday, April 8, 2020
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5ESclose(hid_t es_id)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", es_id);

    /* Check arguments */
    if (H5I_EVENTSET != H5I_get_type(es_id))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not an event set")

    /*
     * Decrement the counter on the object.  It will be freed if the count
     * reaches zero.
     */
    if (H5I_dec_app_ref(es_id) < 0)
        HGOTO_ERROR(H5E_EVENTSET, H5E_CANTDEC, FAIL, "unable to decrement ref count on event set")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5ESclose() */
