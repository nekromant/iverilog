/* vi:sw=6
 * Copyright (c) 2002 Michael Ruff (mruff at chiaro.com)
 *                    Michael Runyan (mrunyan at chiaro.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */
#ifdef HAVE_CVS_IDENT
#ident "$Id: veriusertfs.c,v 1.7 2003/04/23 15:01:29 steve Exp $"
#endif

/*
 * Contains the routines required to implement veriusertfs routines
 * via VPI. This is extremly ugly, so don't look after eating dinner.
 */

# include <string.h>
# include <stdlib.h>
# include <assert.h>
# include "vpi_user.h"
# include "veriuser.h"

/*
 * local structure used to hold the persistent veriusertfs data
 * and anything else we decide to put in here, like workarea data.
 */
typedef struct t_pli_data {
      p_tfcell	tf;		/* pointer to veriusertfs cell */
      int	paramvc;	/* parameter number for misctf */
} s_pli_data, *p_pli_data;

static int compiletf(char *);
static int calltf(char *);
static int callback(p_cb_data);

/*
 * Register veriusertfs routines/wrappers. Iterate over the tfcell
 * array, registering each function.
 */
void veriusertfs_register_table(p_tfcell vtable)
{
      p_tfcell tf;
      s_vpi_systf_data tf_data;
      p_pli_data data;

      for (tf = vtable; tf; tf++) {
	    /* last element */
	    if (tf->type == 0) break;

	    /* force forwref true */
	    if (!tf->forwref) {
		  vpi_printf("veriusertfs: %s, forcing forwref = true\n",
			tf->tfname);
	    }

	    /* squirrel away veriusertfs in persistent user_data */
	    data = (p_pli_data) calloc(1, sizeof(s_pli_data));
	    assert(data != NULL);
	    data->tf = tf;

	      /* Build a VPI system task/function structure, and point
		 it to the pli_data that represents this
		 function. Supply wrapper functions for the system
		 task actions. */
	    (void) memset(&tf_data, 0, sizeof(s_vpi_systf_data));
	    switch (tf->type) {
		case usertask:
		  tf_data.type = vpiSysTask;
		  break;
		case userfunction:
		  tf_data.type = vpiSysFunc;
		  break;
		default:
		  vpi_printf("veriusertfs: %s, unsupported type %d\n",
			     tf->tfname, tf->type);
		  continue;
	    }

	    tf_data.tfname = tf->tfname;
	    tf_data.compiletf = compiletf;
	    tf_data.calltf = calltf;
	    tf_data.sizetf = tf->sizetf;
	    tf_data.user_data = (char *)data;

	    /* register */
	    vpi_register_systf(&tf_data);
      }

      return;
}

/*
 * This function calls the veriusertfs checktf and sets up all the
 * callbacks misctf requires.
 */
static int compiletf(char *data)
{
      p_pli_data pli;
      p_tfcell tf;
      s_cb_data cb_data;
      vpiHandle call_h, arg_i, arg_h;
      p_pli_data dp;
      int paramvc = 1;
      int rtn;

      /* cast back from opaque */
      pli = (p_pli_data)data;
      tf = pli->tf;

      /* get call handle */
      call_h = vpi_handle(vpiSysTfCall, NULL);

	/* Attach the pli_data structure to the vpi handle of the
	   system task. This is how I manage the map from vpiHandle to
	   PLI1 pli data. We do it here (instead of during register)
	   because this is the first that I have both the vpiHandle
	   and the pli_data. */
      vpi_put_userdata(call_h, pli);

      /* default cb_data */
      (void) memset(&cb_data, 0, sizeof(s_cb_data));
      cb_data.cb_rtn = callback;
      cb_data.user_data = data;

      /* register EOS misctf callback */
      cb_data.reason = cbEndOfSimulation;
      cb_data.obj = call_h;
      vpi_register_cb(&cb_data);

	/* If there is a misctf function, then create a value change
	   callback for all the arguments. In the tf_* API, misctf
	   functions get value change callbacks, controlled by the
	   tf_asyncon and tf_asyncoff functions. */
      if (tf->misctf && ((arg_i = vpi_iterate(vpiArgument, call_h)) != NULL)) {

	    cb_data.reason = cbValueChange;
	    while ((arg_h = vpi_scan(arg_i)) != NULL) {
		  /* replicate user_data for each instance */
		  dp = (p_pli_data)calloc(1, sizeof(s_pli_data));
		  assert(dp != NULL);
		  memcpy(dp, cb_data.user_data, sizeof(s_pli_data));
		  dp->paramvc = paramvc++;
		  cb_data.user_data = (char *)dp;
		  cb_data.obj = arg_h;
		  vpi_register_cb(&cb_data);
	    }
      }

      /*
       * Since we are in compiletf, checktf and misctf need to
       * be executed. Check runs first to match other simulators.
       */
      rtn = (tf->checktf) ? tf->checktf(tf->data, reason_checktf) : 0;
      if (tf->misctf) tf->misctf(tf->data, reason_endofcompile, 0);

      return rtn;
}

/*
 * This function is the wrapper for the veriusertfs calltf routine.
 */
static int calltf(char *data)
{
      int rc = 0;
      p_pli_data pli;
      p_tfcell tf;

      /* cast back from opaque */
      pli = (p_pli_data)data;
      tf = pli->tf;

      /* execute calltf */
      if (tf->calltf)
	    rc = tf->calltf(tf->data, reason_calltf);

      return rc;
}

/*
 * This function is the wrapper for all the misctf callbacks
 */
extern int async_misctf_enable;

static int callback(p_cb_data data)
{
      p_pli_data pli;
      p_tfcell tf;
      int reason;
      int paramvc = 0;

	/* not enabled */
      if (data->reason == cbValueChange && !async_misctf_enable)
	    return 0;

	/* cast back from opaque */
      pli = (p_pli_data)data->user_data;
      tf = pli->tf;

      switch (data->reason) {
	  case cbValueChange:
	    reason = reason_paramvc;
	    paramvc = pli->paramvc;
	    break;
	  case cbEndOfSimulation:
	    reason = reason_finish;
	    break;
	  case cbReadWriteSynch:
	    reason = reason_synch;
	    break;
	  default:
	    assert(0);
      }

      /* execute misctf */
      return (tf->misctf) ? tf->misctf(tf->data, reason, paramvc) : 0;
}

PLI_INT32 tf_isynchronize(void*obj)
{
      vpiHandle sys = (vpiHandle)obj;
      p_pli_data pli = vpi_get_userdata(sys);
      s_cb_data cb;
      s_vpi_time ti;

      ti.type = vpiSuppressTime;

      cb.reason = cbReadWriteSynch;
      cb.cb_rtn = callback;
      cb.obj = sys;
      cb.time = &ti;
      cb.user_data = pli;

      vpi_register_cb(&cb);
      return 0;
}

PLI_INT32 tf_synchronize(void)
{
      return tf_isynchronize(tf_getinstance());
}

/*
 * $Log: veriusertfs.c,v $
 * Revision 1.7  2003/04/23 15:01:29  steve
 *  Add tf_synchronize and tf_multiply_long.
 *
 * Revision 1.6  2003/02/16 02:23:14  steve
 *  Change the IV veriusertfs_register to accept table pointers.
 *
 * Revision 1.5  2002/08/12 01:35:02  steve
 *  conditional ident string using autoconfig.
 *
 * Revision 1.4  2002/06/04 01:42:58  steve
 *  Add misctf support to libveriuser
 *
 * Revision 1.3  2002/06/03 00:08:42  steve
 *  Better typing for veriusertfs table.
 *
 * Revision 1.2  2002/05/31 18:21:39  steve
 *  Check for and don't dereference null pointers,
 *  Avoid copy of static objects.
 *  (mruff)
 *
 * Revision 1.1  2002/05/30 02:37:26  steve
 *  Add the veriusertf_register funciton.
 *
 */
