/* -*- Mode: C; c-basic-offset: 4 -*-
 * vim: tabstop=4 shiftwidth=4 expandtab
 *
 * Copyright (C) 2005-2009 Johan Dahlin <johan@gnome.org>
 * Copyright (C) 2011 John (J5) Palimier <johnp@redhat.com>
 *
 *   pygi-invoke.c: main invocation function
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#include <pyglib.h>
#include "pygi-invoke.h"
#include "pygi-marshal-cleanup.h"

static inline gboolean
_invoke_callable (PyGIInvokeState *state,
                  PyGICallableCache *cache,
                  GICallableInfo *callable_info)
{
    GError *error;
    gint retval;

    error = NULL;

    pyg_begin_allow_threads;

    /* FIXME: use this for now but we can streamline the calls */
    if (cache->function_type == PYGI_FUNCTION_TYPE_VFUNC)
        retval = g_vfunc_info_invoke ( callable_info,
                                       state->implementor_gtype,
                                       state->in_args,
                                       cache->n_from_py_args,
                                       state->out_args,
                                       cache->n_to_py_args,
                                      &state->return_arg,
                                      &error);
    else
        retval = g_function_info_invoke ( callable_info,
                                          state->in_args,
                                          cache->n_from_py_args,
                                          state->out_args,
                                          cache->n_to_py_args,
                                         &state->return_arg,
                                         &error);
    pyg_end_allow_threads;

    if (!retval) {
        g_assert (error != NULL);
        pyglib_error_check (&error);

        /* It is unclear if the error occured before or after the C
         * function was invoked so for now assume success
         * We eventually should marshal directly to FFI so we no longer
         * have to use the reference implementation
         */
        pygi_marshal_cleanup_args_from_py_marshal_success (state, cache);

        return FALSE;
    }

    if (state->error != NULL) {
        if (pyglib_error_check (&(state->error))) {
            /* even though we errored out, the call itself was successful,
               so we assume the call processed all of the parameters */
            pygi_marshal_cleanup_args_from_py_marshal_success (state, cache);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
_check_for_unexpected_kwargs (const gchar *function_name,
                              GHashTable  *arg_name_hash,
                              PyObject    *py_kwargs)
{
    PyObject *dict_key, *dict_value;
    Py_ssize_t dict_iter_pos = 0;

    while (PyDict_Next (py_kwargs, &dict_iter_pos, &dict_key, &dict_value)) {
        PyObject *key;

#if PY_VERSION_HEX < 0x03000000
        if (PyString_Check (dict_key)) {
            Py_INCREF (dict_key);
            key = dict_key;
        } else
#endif
        {
            key = PyUnicode_AsUTF8String (dict_key);
            if (key == NULL) {
                return FALSE;
            }
        }

        if (g_hash_table_lookup (arg_name_hash, PyBytes_AsString(key)) == NULL) {
            PyErr_Format (PyExc_TypeError,
                          "%.200s() got an unexpected keyword argument '%.400s'",
                          function_name,
                          PyBytes_AsString (key));
            Py_DECREF (key);
            return FALSE;
        }

        Py_DECREF (key);
    }
    return TRUE;
}

/**
 * _py_args_combine_and_check_length:
 * @function_name: the name of the function being called. Used for error messages.
 * @arg_name_list: a list of the string names for each argument. The length
 *                 of this list is the number of required arguments for the
 *                 function. If an argument has no name, NULL is put in its
 *                 position in the list.
 * @py_args: the tuple of positional arguments. A referece is stolen, and this
             tuple will be either decreffed or returned as is.
 * @py_kwargs: the dict of keyword arguments to be merged with py_args.
 *             A reference is borrowed.
 *
 * Returns: The py_args and py_kwargs combined into one tuple.
 */
static PyObject *
_py_args_combine_and_check_length (const gchar *function_name,
                                   GSList      *arg_name_list,
                                   GHashTable  *arg_name_hash,
                                   PyObject    *py_args,
                                   PyObject    *py_kwargs)
{
    PyObject *combined_py_args = NULL;
    Py_ssize_t n_py_args, n_py_kwargs, i;
    guint n_expected_args;
    GSList *l;

    n_py_args = PyTuple_GET_SIZE (py_args);
    n_py_kwargs = PyDict_Size (py_kwargs);

    n_expected_args = g_slist_length (arg_name_list);

    if (n_py_kwargs == 0 && n_py_args == n_expected_args) {
        return py_args;
    }

    if (n_expected_args < n_py_args) {
        PyErr_Format (PyExc_TypeError,
                      "%.200s() takes exactly %d %sargument%s (%zd given)",
                      function_name,
                      n_expected_args,
                      n_py_kwargs > 0 ? "non-keyword " : "",
                      n_expected_args == 1 ? "" : "s",
                      n_py_args);

        Py_DECREF (py_args);
        return NULL;
    }

    if (!_check_for_unexpected_kwargs (function_name, arg_name_hash, py_kwargs)) {
        Py_DECREF (py_args);
        return NULL;
    }

    /* will hold arguments from both py_args and py_kwargs
     * when they are combined into a single tuple */
    combined_py_args = PyTuple_New (n_expected_args);

    for (i = 0; i < n_py_args; i++) {
        PyObject *item = PyTuple_GET_ITEM (py_args, i);
        Py_INCREF (item);
        PyTuple_SET_ITEM (combined_py_args, i, item);
    }

    Py_CLEAR(py_args);

    for (i = 0, l = arg_name_list; i < n_expected_args && l; i++, l = l->next) {
        PyObject *py_arg_item, *kw_arg_item = NULL;
        const gchar *arg_name = l->data;

        if (arg_name != NULL) {
            /* NULL means this argument has no keyword name */
            /* ex. the first argument to a method or constructor */
            kw_arg_item = PyDict_GetItemString (py_kwargs, arg_name);
        }
        py_arg_item = PyTuple_GET_ITEM (combined_py_args, i);

        if (kw_arg_item != NULL && py_arg_item == NULL) {
            Py_INCREF (kw_arg_item);
            PyTuple_SET_ITEM (combined_py_args, i, kw_arg_item);

        } else if (kw_arg_item == NULL && py_arg_item == NULL) {
            PyErr_Format (PyExc_TypeError,
                          "%.200s() takes exactly %d %sargument%s (%zd given)",
                          function_name,
                          n_expected_args,
                          n_py_kwargs > 0 ? "non-keyword " : "",
                          n_expected_args == 1 ? "" : "s",
                          n_py_args);

            Py_DECREF (combined_py_args);
            return NULL;

        } else if (kw_arg_item != NULL && py_arg_item != NULL) {
            PyErr_Format (PyExc_TypeError,
                          "%.200s() got multiple values for keyword argument '%.200s'",
                          function_name,
                          arg_name);

            Py_DECREF (combined_py_args);
            return NULL;
        }
    }

    return combined_py_args;
}

static inline gboolean
_invoke_state_init_from_callable_cache (PyGIInvokeState *state,
                                        PyGICallableCache *cache,
                                        PyObject *py_args,
                                        PyObject *kwargs)
{
    state->implementor_gtype = 0;

    /* TODO: We don't use the class parameter sent in by  the structure
     * so we remove it from the py_args tuple but we can keep it 
     * around if we want to call actual gobject constructors
     * in the future instead of calling g_object_new
     */
    if (cache->function_type == PYGI_FUNCTION_TYPE_CONSTRUCTOR) {
        PyObject *constructor_class;
        constructor_class = PyTuple_GetItem (py_args, 0);

        if (constructor_class == NULL) {
            PyErr_Clear ();
            PyErr_Format (PyExc_TypeError,
                          "Constructors require the class to be passed in as an argument, "
                          "No arguments passed to the %s constructor.",
                          cache->name);

            return FALSE;
        }
    } else if (cache->function_type == PYGI_FUNCTION_TYPE_VFUNC) {
        PyObject *py_gtype;
        py_gtype = PyTuple_GetItem (py_args, 0);
        if (py_gtype == NULL) {
            PyErr_SetString (PyExc_TypeError,
                             "need the GType of the implementor class");
            return FALSE;
        }

        state->implementor_gtype = pyg_type_from_object (py_gtype);

        if (state->implementor_gtype == 0)
            return FALSE;
    }

    if  (cache->function_type == PYGI_FUNCTION_TYPE_CONSTRUCTOR ||
            cache->function_type == PYGI_FUNCTION_TYPE_VFUNC) {

        /* we could optimize this by using offsets instead of modifying the tuple but it makes the
         * code more error prone and confusing so don't do that unless profiling shows
         * significant gain
         */
        state->py_in_args = PyTuple_GetSlice (py_args, 1, PyTuple_Size (py_args));
    } else {
        state->py_in_args = py_args;
        Py_INCREF (state->py_in_args);
    }

    state->py_in_args = _py_args_combine_and_check_length (cache->name,
                                                           cache->arg_name_list,
                                                           cache->arg_name_hash,
                                                           state->py_in_args,
                                                           kwargs);

    if (state->py_in_args == NULL) {
        return FALSE;
    }
    state->n_py_in_args = PyTuple_Size (state->py_in_args);

    state->args = g_slice_alloc0 (cache->n_args * sizeof (GIArgument *));
    if (state->args == NULL && cache->n_args != 0) {
        PyErr_NoMemory();
        return FALSE;
    }

    state->in_args = g_slice_alloc0 (cache->n_from_py_args * sizeof(GIArgument));
    if (state->in_args == NULL && cache->n_from_py_args != 0) {
        PyErr_NoMemory ();
        return FALSE;
    }

    state->out_values = g_slice_alloc0 (cache->n_to_py_args * sizeof(GIArgument));
    if (state->out_values == NULL && cache->n_to_py_args != 0) {
        PyErr_NoMemory ();
        return FALSE;
    }

    state->out_args = g_slice_alloc0 (cache->n_to_py_args * sizeof(GIArgument));
    if (state->out_args == NULL && cache->n_to_py_args != 0) {
        PyErr_NoMemory ();
        return FALSE;
    }

    state->error = NULL;

    return TRUE;
}

static inline void
_invoke_state_clear (PyGIInvokeState *state, PyGICallableCache *cache)
{
    g_slice_free1 (cache->n_args * sizeof(GIArgument *), state->args);
    g_slice_free1 (cache->n_from_py_args * sizeof(GIArgument), state->in_args);
    g_slice_free1 (cache->n_to_py_args * sizeof(GIArgument), state->out_args);
    g_slice_free1 (cache->n_to_py_args * sizeof(GIArgument), state->out_values);

    Py_XDECREF (state->py_in_args);
}

static gboolean _caller_alloc (PyGIInvokeState *state,
                               PyGIArgCache *arg_cache,
                               gssize arg_count,
                               gssize out_count)
{
    PyGIInterfaceCache *iface_cache;

    g_assert (arg_cache->type_tag == GI_TYPE_TAG_INTERFACE);

    iface_cache = (PyGIInterfaceCache *)arg_cache;

    state->out_args[out_count].v_pointer = NULL;
    state->args[arg_count] = &state->out_args[out_count];
    if (iface_cache->g_type == G_TYPE_BOXED) {
        state->args[arg_count]->v_pointer =
            _pygi_boxed_alloc (iface_cache->interface_info, NULL);
    } else if (iface_cache->g_type == G_TYPE_VALUE) {
        state->args[arg_count]->v_pointer = g_slice_new0 (GValue);
    } else if (iface_cache->is_foreign) {
        PyObject *foreign_struct =
            pygi_struct_foreign_convert_from_g_argument (
                iface_cache->interface_info,
                NULL);

            pygi_struct_foreign_convert_to_g_argument (foreign_struct,
                                                       iface_cache->interface_info,
                                                       GI_TRANSFER_EVERYTHING,
                                                       state->args[arg_count]);
    } else {
            gssize size = g_struct_info_get_size(
                (GIStructInfo *)iface_cache->interface_info);
            state->args[arg_count]->v_pointer = g_malloc0 (size);
    }

    if (state->args[arg_count]->v_pointer == NULL)
        return FALSE;


    return TRUE;
}

static inline gboolean
_invoke_marshal_in_args (PyGIInvokeState *state, PyGICallableCache *cache)
{
    gssize i, in_count, out_count;
    in_count = 0;
    out_count = 0;

    if (state->n_py_in_args > cache->n_py_args) {
        PyErr_Format (PyExc_TypeError,
                      "%s() takes exactly %zd argument(s) (%zd given)",
                      cache->name,
                      cache->n_py_args,
                      state->n_py_in_args);
        return FALSE;
    }

    for (i = 0; i < cache->n_args; i++) {
        GIArgument *c_arg;
        PyGIArgCache *arg_cache = cache->args_cache[i];
        PyObject *py_arg = NULL;

        switch (arg_cache->direction) {
            case PYGI_DIRECTION_FROM_PYTHON:
                state->args[i] = &(state->in_args[in_count]);
                in_count++;

                if (arg_cache->meta_type > 0)
                    continue;

                if (arg_cache->py_arg_index >= state->n_py_in_args) {
                    PyErr_Format (PyExc_TypeError,
                                  "%s() takes exactly %zd argument(s) (%zd given)",
                                   cache->name,
                                   cache->n_py_args,
                                   state->n_py_in_args);

                    /* clean up all of the args we have already marshalled,
                     * since invoke will not be called
                     */
                    pygi_marshal_cleanup_args_from_py_parameter_fail (state,
                                                                      cache,
                                                                      i - 1);
                    return FALSE;
                }

                py_arg =
                    PyTuple_GET_ITEM (state->py_in_args,
                                      arg_cache->py_arg_index);

                break;
            case PYGI_DIRECTION_BIDIRECTIONAL:
                /* this will be filled in if it is an child value */
                if (state->in_args[in_count].v_pointer != NULL)
                    state->out_values[out_count] = state->in_args[in_count];

                state->in_args[in_count].v_pointer = &state->out_values[out_count];
                in_count++;

                if (arg_cache->meta_type != PYGI_META_ARG_TYPE_CHILD) {
                    if (arg_cache->py_arg_index >= state->n_py_in_args) {
                        PyErr_Format (PyExc_TypeError,
                                      "%s() takes exactly %zd argument(s) (%zd given)",
                                       cache->name,
                                       cache->n_py_args,
                                       state->n_py_in_args);
                        pygi_marshal_cleanup_args_from_py_parameter_fail (state,
                                                                          cache,
                                                                          i - 1);
                        return FALSE;
                    }

                    py_arg =
                        PyTuple_GET_ITEM (state->py_in_args,
                                          arg_cache->py_arg_index);
                }
            case PYGI_DIRECTION_TO_PYTHON:
                if (arg_cache->is_caller_allocates) {
                    if (!_caller_alloc (state, arg_cache, i, out_count)) {
                        PyErr_Format (PyExc_TypeError,
                                      "Could not caller allocate argument %zd of callable %s",
                                      i, cache->name);
                        pygi_marshal_cleanup_args_from_py_parameter_fail (state,
                                                                          cache,
                                                                          i - 1);
                        return FALSE;
                    }
                } else {
                    state->out_args[out_count].v_pointer = &state->out_values[out_count];
                    state->args[i] = &state->out_values[out_count];
                }
                out_count++;
                break;
        }

        c_arg = state->args[i];
        if (arg_cache->from_py_marshaller != NULL) {
            if (!arg_cache->allow_none && py_arg == Py_None) {
                PyErr_Format (PyExc_TypeError,
                              "Argument %zd does not allow None as a value",
                              i);

                 pygi_marshal_cleanup_args_from_py_parameter_fail (state,
                                                                   cache,
                                                                   i - 1);
                 return FALSE;
            }
            gboolean success = arg_cache->from_py_marshaller (state,
                                                              cache,
                                                              arg_cache,
                                                              py_arg,
                                                              c_arg);
            if (!success) {
                pygi_marshal_cleanup_args_from_py_parameter_fail (state,
                                                                  cache,
                                                                  i - 1);
                return FALSE;
            }

        }

    }

    return TRUE;
}

static inline PyObject *
_invoke_marshal_out_args (PyGIInvokeState *state, PyGICallableCache *cache)
{
    PyObject *py_out = NULL;
    PyObject *py_return = NULL;
    gssize total_out_args = cache->n_to_py_args;
    gboolean has_return = FALSE;

    if (cache->return_cache) {
        if (!cache->return_cache->is_skipped) {
            if (cache->function_type == PYGI_FUNCTION_TYPE_CONSTRUCTOR) {
                if (state->return_arg.v_pointer == NULL) {
                    PyErr_SetString (PyExc_TypeError, "constructor returned NULL");
                    pygi_marshal_cleanup_args_return_fail (state,
                                                       cache);
                    return NULL;
                }
            }

            py_return = cache->return_cache->to_py_marshaller ( state,
                                                                cache,
                                                                cache->return_cache,
                                                               &state->return_arg);
            if (py_return == NULL) {
                pygi_marshal_cleanup_args_return_fail (state,
                                                       cache);
                return NULL;
            }


            if (cache->return_cache->type_tag != GI_TYPE_TAG_VOID) {
                total_out_args++;
                has_return = TRUE;
            }
        } else {
            if (cache->return_cache->transfer == GI_TRANSFER_EVERYTHING) {
                PyGIMarshalCleanupFunc to_py_cleanup =
                    cache->return_cache->to_py_cleanup;

                if (to_py_cleanup != NULL)
                    to_py_cleanup ( state,
                                    cache->return_cache,
                                   &state->return_arg,
                                    FALSE);
            }
        }
    }

    total_out_args -= cache->n_to_py_child_args;

    if (cache->n_to_py_args - cache->n_to_py_child_args  == 0) {
        if (cache->return_cache->is_skipped && state->error == NULL) {
            /* we skip the return value and have no (out) arguments to return,
             * so py_return should be NULL. But we must not return NULL,
             * otherwise Python will expect an exception.
             */
            g_assert (py_return == NULL);
            Py_INCREF(Py_None);
            py_return = Py_None;
        }

        py_out = py_return;
    } else if (total_out_args == 1) {
        /* if we get here there is one out arg an no return */
        PyGIArgCache *arg_cache = (PyGIArgCache *)cache->to_py_args->data;
        py_out = arg_cache->to_py_marshaller (state,
                                              cache,
                                              arg_cache,
                                              state->args[arg_cache->c_arg_index]);
        if (py_out == NULL) {
            pygi_marshal_cleanup_args_to_py_parameter_fail (state,
                                                            cache,
                                                            0);
            return NULL;
        }

    } else {
        gssize py_arg_index = 0;
        GSList *cache_item = cache->to_py_args;
        /* return a tuple */
        py_out = PyTuple_New (total_out_args);
        if (has_return) {
            PyTuple_SET_ITEM (py_out, py_arg_index, py_return);
            py_arg_index++;
        }

        for(; py_arg_index < total_out_args; py_arg_index++) {
            PyGIArgCache *arg_cache = (PyGIArgCache *)cache_item->data;
            PyObject *py_obj = arg_cache->to_py_marshaller (state,
                                                            cache,
                                                            arg_cache,
                                                            state->args[arg_cache->c_arg_index]);

            if (py_obj == NULL) {
                if (has_return)
                    py_arg_index--;
 
                pygi_marshal_cleanup_args_to_py_parameter_fail (state,
                                                                cache,
                                                                py_arg_index);
                Py_DECREF (py_out);
                return NULL;
            }

            PyTuple_SET_ITEM (py_out, py_arg_index, py_obj);
            cache_item = cache_item->next;
        }
    }
    return py_out;
}

PyObject *
_wrap_g_callable_info_invoke (PyGIBaseInfo *self,
                              PyObject *py_args,
                              PyObject *kwargs)
{
    PyGIInvokeState state = { 0, };
    PyObject *ret = NULL;

    if (self->cache == NULL) {
        self->cache = _pygi_callable_cache_new (self->info);
        if (self->cache == NULL)
            return NULL;
    }

    if (!_invoke_state_init_from_callable_cache (&state, self->cache, py_args, kwargs))
        goto err;

    if (!_invoke_marshal_in_args (&state, self->cache))
        goto err;

    if (!_invoke_callable (&state, self->cache, self->info))
        goto err;

    pygi_marshal_cleanup_args_from_py_marshal_success (&state, self->cache);

    ret = _invoke_marshal_out_args (&state, self->cache);
    if (ret)
        pygi_marshal_cleanup_args_to_py_marshal_success (&state, self->cache);
err:
    _invoke_state_clear (&state, self->cache);
    return ret;
}
