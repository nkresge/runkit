/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2006 The PHP Group, (c) 2008-2012 Dmitry Zenovich |
  +----------------------------------------------------------------------+
  | This source file is subject to the new BSD license,                  |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.opensource.org/licenses/BSD-3-Clause                      |
  | If you did not receive a copy of the license and are unable to       |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Sara Golemon <pollita@php.net>                               |
  | Modified by Dmitry Zenovich <dzenovich@gmail.com>                    |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#include "php_runkit.h"

#ifdef PHP_RUNKIT_MANIPULATION
/* {{{ php_runkit_check_call_stack
 */
int php_runkit_check_call_stack(zend_op_array *op_array TSRMLS_DC)
{
	zend_execute_data *ptr;

	ptr = EG(current_execute_data);

	while (ptr) {
		if (ptr->op_array && ptr->op_array->opcodes == op_array->opcodes) {
			return FAILURE;
		}
		ptr = ptr->prev_execute_data;
	}

	return SUCCESS;
}
/* }}} */

static void php_runkit_hash_key_dtor(zend_hash_key *hash_key)
{
	efree(PHP_RUNKIT_HASH_KEY(hash_key));
}

/* Maintain order */
#define PHP_RUNKIT_FETCH_FUNCTION_INSPECT	0
#define PHP_RUNKIT_FETCH_FUNCTION_REMOVE	1
#define PHP_RUNKIT_FETCH_FUNCTION_RENAME	2

/* {{{ php_runkit_fetch_function
 */
static int php_runkit_fetch_function(int fname_type, char *fname, int fname_len, zend_function **pfe, int flag TSRMLS_DC)
{
	zend_function *fe;

	PHP_RUNKIT_STRTOLOWER(fname);

	if (zend_hash_find(EG(function_table), fname, fname_len + 1, (void**)&fe) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s() not found", fname);
		return FAILURE;
	}

	if (fe->type == ZEND_INTERNAL_FUNCTION &&
		!RUNKIT_G(internal_override)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s() is an internal function and runkit.internal_override is disabled", fname);
		return FAILURE;
	}

	if (fe->type != ZEND_USER_FUNCTION &&
		fe->type != ZEND_INTERNAL_FUNCTION) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s() is not a user or normal internal function", fname);
		return FAILURE;
	}

	if (pfe) {
		*pfe = fe;
	}

	if (fe->type == ZEND_INTERNAL_FUNCTION &&
		flag >= PHP_RUNKIT_FETCH_FUNCTION_REMOVE) {
		if (!RUNKIT_G(replaced_internal_functions)) {
			ALLOC_HASHTABLE(RUNKIT_G(replaced_internal_functions));
			zend_hash_init(RUNKIT_G(replaced_internal_functions), 4, NULL, NULL, 0);
		}
		zend_hash_add(RUNKIT_G(replaced_internal_functions), fname, fname_len + 1, (void*)fe, sizeof(zend_function), NULL);
		if (flag >= PHP_RUNKIT_FETCH_FUNCTION_RENAME) {
			zend_hash_key hash_key;

			if (!RUNKIT_G(misplaced_internal_functions)) {
				ALLOC_HASHTABLE(RUNKIT_G(misplaced_internal_functions));
				zend_hash_init(RUNKIT_G(misplaced_internal_functions), 4, NULL, php_runkit_hash_key_dtor, 0);
			}
			hash_key.nKeyLength = fname_len + 1;
			PHP_RUNKIT_HASH_KEY(&hash_key) = estrndup(fname, PHP_RUNKIT_HASH_KEYLEN(&hash_key));
			zend_hash_next_index_insert(RUNKIT_G(misplaced_internal_functions), (void*)&hash_key, sizeof(zend_hash_key), NULL);
		}
	}

	return SUCCESS;
}
/* }}} */

/* {{{ php_runkit_function_copy_ctor
	Duplicate structures in an op_array where necessary to make an outright duplicate */
void php_runkit_function_copy_ctor(zend_function *fe, char *newname)
{
	zend_compiled_variable *dupvars;
	zend_op *opcode_copy;
	int i;

	if (fe->op_array.static_variables) {
		HashTable *tmpHash;
		zval *tmpZval;

		ALLOC_HASHTABLE(tmpHash);
		zend_hash_init(tmpHash, zend_hash_num_elements(fe->op_array.static_variables), NULL, ZVAL_PTR_DTOR, 0);
		zend_hash_copy(tmpHash, fe->op_array.static_variables, (copy_ctor_func_t) zval_add_ref, (void*) &tmpZval, sizeof(zval*));
		fe->op_array.static_variables = tmpHash;
	}

	fe->op_array.refcount = emalloc(sizeof(zend_uint));
	*(fe->op_array.refcount) = 1;

	i = fe->op_array.last_var;
	dupvars = safe_emalloc(fe->op_array.last_var, sizeof(zend_compiled_variable), 0);
	while (i > 0) {
		i--;
		dupvars[i].name = estrdup(fe->op_array.vars[i].name);
		dupvars[i].name_len = fe->op_array.vars[i].name_len;
		dupvars[i].hash_value = fe->op_array.vars[i].hash_value;
	}
	fe->op_array.vars = dupvars;

	opcode_copy = safe_emalloc(sizeof(zend_op), fe->op_array.last, 0);
	for(i = 0; i < fe->op_array.last; i++) {
		opcode_copy[i] = fe->op_array.opcodes[i];
		if (opcode_copy[i].op1_type == IS_CONST) {
			zval_copy_ctor(&opcode_copy[i].op1.constant);
		} else {
			if (opcode_copy[i].op1.jmp_addr >= fe->op_array.opcodes &&
				opcode_copy[i].op1.jmp_addr <  fe->op_array.opcodes + fe->op_array.last) {
				opcode_copy[i].op1.jmp_addr =  opcode_copy + (fe->op_array.opcodes[i].op1.jmp_addr - fe->op_array.opcodes);
			}
        }

		if (opcode_copy[i].op2_type == IS_CONST) {
			zval_copy_ctor(&opcode_copy[i].op2.constant);
		} else {
			if (opcode_copy[i].op2.jmp_addr >= fe->op_array.opcodes &&
				opcode_copy[i].op2.jmp_addr <  fe->op_array.opcodes + fe->op_array.last) {
				opcode_copy[i].op2.jmp_addr =  opcode_copy + (fe->op_array.opcodes[i].op2.jmp_addr - fe->op_array.opcodes);
			}
		}
	}
	fe->op_array.opcodes = opcode_copy;
	EG(start_op) = fe->op_array.opcodes;

	if (newname) {
		fe->op_array.function_name = newname;
	} else {
		fe->op_array.function_name = estrdup(fe->op_array.function_name);
	}

	fe->op_array.prototype = fe;

	if (fe->op_array.arg_info) {
		zend_arg_info *tmpArginfo;

		tmpArginfo = safe_emalloc(sizeof(zend_arg_info), fe->op_array.num_args, 0);
		for(i = 0; i < fe->op_array.num_args; i++) {
			tmpArginfo[i] = fe->op_array.arg_info[i];
			tmpArginfo[i].name = estrndup(tmpArginfo[i].name, tmpArginfo[i].name_len);
			if (tmpArginfo[i].class_name) {
				tmpArginfo[i].class_name = estrndup(tmpArginfo[i].class_name, tmpArginfo[i].class_name_len);
			}
		}
		fe->op_array.arg_info = tmpArginfo;
	}

	fe->op_array.doc_comment = estrndup(fe->op_array.doc_comment, fe->op_array.doc_comment_len);
	fe->op_array.try_catch_array = (zend_try_catch_element*)estrndup((char*)fe->op_array.try_catch_array, sizeof(zend_try_catch_element) * fe->op_array.last_try_catch);
	fe->op_array.brk_cont_array = (zend_brk_cont_element*)estrndup((char*)fe->op_array.brk_cont_array, sizeof(zend_brk_cont_element) * fe->op_array.last_brk_cont);
}
/* }}}} */

/* {{{ php_runkit_generate_lambda_method
	Heavily borrowed from ZEND_FUNCTION(create_function) */
int php_runkit_generate_lambda_method(char *arguments, int arguments_len, char *phpcode, int phpcode_len, zend_function **pfe TSRMLS_DC)
{
	char *eval_code, *eval_name;
	int eval_code_length;

	eval_code_length = sizeof("function " RUNKIT_TEMP_FUNCNAME) + arguments_len + 4 + phpcode_len;
	eval_code = (char*)emalloc(eval_code_length);
	snprintf(eval_code, eval_code_length, "function " RUNKIT_TEMP_FUNCNAME "(%s){%s}", arguments, phpcode);
	eval_name = zend_make_compiled_string_description("runkit runtime-created function" TSRMLS_CC);
	if (zend_eval_string(eval_code, NULL, eval_name TSRMLS_CC) == FAILURE) {
		efree(eval_code);
		efree(eval_name);
		return FAILURE;
	}
	efree(eval_code);
	efree(eval_name);

	if (zend_hash_find(EG(function_table), RUNKIT_TEMP_FUNCNAME, sizeof(RUNKIT_TEMP_FUNCNAME), (void **)pfe) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Unexpected inconsistency during create_function");
		return FAILURE;
	}

	return SUCCESS;
}
/* }}} */

/* {{{ php_runkit_destroy_misplaced_functions
	Wipe old internal functions that were renamed to new targets
	They'll get replaced soon enough */
int php_runkit_destroy_misplaced_functions(zend_hash_key *hash_key TSRMLS_DC)
{
	if (!hash_key->nKeyLength) {
		/* Nonsense, skip it */
		return ZEND_HASH_APPLY_REMOVE;
	}

	zend_hash_del(EG(function_table), hash_key->arKey, hash_key->nKeyLength);

	return ZEND_HASH_APPLY_REMOVE;
}
/* }}} */

/* {{{ php_runkit_restore_internal_functions
	Cleanup after modifications to internal functions */
int php_runkit_restore_internal_functions(RUNKIT_53_TSRMLS_ARG(zend_internal_function *fe), int num_args, va_list args, zend_hash_key *hash_key)
{
	if (!hash_key->nKeyLength) {
		/* Nonsense, skip it */
		return ZEND_HASH_APPLY_REMOVE;
	}

	zend_hash_update(EG(function_table), hash_key->arKey, hash_key->nKeyLength, (void*)fe, sizeof(zend_function), NULL);

	return ZEND_HASH_APPLY_REMOVE;
}
/* }}} */

/* *****************
   * Functions API *
   ***************** */

/* {{{ proto bool runkit_function_add(string funcname, string arglist, string code)
	Add a new function, similar to create_function, but allows specifying name
	There's nothing about this function that's better than eval(), it's here for completeness */
PHP_FUNCTION(runkit_function_add)
{
	PHP_RUNKIT_DECL_STRING_PARAM(funcname)
	PHP_RUNKIT_DECL_STRING_PARAM(arglist)
	PHP_RUNKIT_DECL_STRING_PARAM(code)
	char *delta = NULL, *delta_desc;
	int retval;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
			PHP_RUNKIT_STRING_SPEC "/" PHP_RUNKIT_STRING_SPEC PHP_RUNKIT_STRING_SPEC,
			PHP_RUNKIT_STRING_PARAM(funcname),
			PHP_RUNKIT_STRING_PARAM(arglist),
			PHP_RUNKIT_STRING_PARAM(code)) == FAILURE) {
		RETURN_FALSE;
	}

	/* UTODO */
	PHP_RUNKIT_STRTOLOWER(funcname);

	if (zend_hash_exists(EG(function_table), funcname, funcname_len + 1)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Function %s() already exists", funcname);
		RETURN_FALSE;
	}

	spprintf(&delta, 0, "function %s(%s){%s}", funcname, arglist, code);

	if (!delta) {
		RETURN_FALSE;
	}

	delta_desc = zend_make_compiled_string_description("runkit created function" TSRMLS_CC);
	retval = zend_eval_string(delta, NULL, delta_desc TSRMLS_CC);
	efree(delta_desc);
	efree(delta);

	RETURN_BOOL(retval == SUCCESS);
}
/* }}} */

/* {{{ proto bool runkit_function_remove(string funcname)
 */
PHP_FUNCTION(runkit_function_remove)
{
	PHP_RUNKIT_DECL_STRING_PARAM(funcname)

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, PHP_RUNKIT_STRING_SPEC "/", PHP_RUNKIT_STRING_PARAM(funcname)) == FAILURE) {
		RETURN_FALSE;
	}

	if (php_runkit_fetch_function(PHP_RUNKIT_STRING_TYPE(funcname), funcname, funcname_len, NULL, PHP_RUNKIT_FETCH_FUNCTION_REMOVE TSRMLS_CC) == FAILURE) {
		RETURN_FALSE;
	}

	RETURN_BOOL(zend_hash_del(EG(function_table), funcname, funcname_len + 1) == SUCCESS);
}
/* }}} */

/* {{{ proto bool runkit_function_rename(string funcname, string newname)
 */
PHP_FUNCTION(runkit_function_rename)
{
	zend_function *fe, func;
	PHP_RUNKIT_DECL_STRING_PARAM(sfunc)
	PHP_RUNKIT_DECL_STRING_PARAM(dfunc)

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, PHP_RUNKIT_STRING_SPEC "/" PHP_RUNKIT_STRING_SPEC "/",
			PHP_RUNKIT_STRING_PARAM(sfunc), PHP_RUNKIT_STRING_PARAM(dfunc)) == FAILURE ) {
		RETURN_FALSE;
	}

	/* UTODO */
	PHP_RUNKIT_STRTOLOWER(dfunc);

	if (zend_hash_exists(EG(function_table), dfunc, dfunc_len + 1)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s() already exists", dfunc);
		RETURN_FALSE;
	}

	if (php_runkit_fetch_function(PHP_RUNKIT_STRING_TYPE(sfunc), sfunc, sfunc_len, &fe, PHP_RUNKIT_FETCH_FUNCTION_RENAME TSRMLS_CC) == FAILURE) {
		RETURN_FALSE;
	}

	func = *fe;
	PHP_RUNKIT_FUNCTION_ADD_REF(&func);

	if (zend_hash_del(EG(function_table), sfunc, sfunc_len + 1) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error removing reference to old function name %s()", sfunc);
		zend_function_dtor(&func);
		RETURN_FALSE;
	}

	if (func.type == ZEND_USER_FUNCTION) {
		efree(func.common.function_name);
		func.common.function_name = estrndup(dfunc, dfunc_len);
	}

	if (zend_hash_add(EG(function_table), dfunc, dfunc_len + 1, &func, sizeof(zend_function), NULL) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to add reference to new function name %s()", dfunc);
		zend_function_dtor(fe);
		RETURN_FALSE;
	}

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool runkit_function_redefine(string funcname, string arglist, string code)
 */
PHP_FUNCTION(runkit_function_redefine)
{
	PHP_RUNKIT_DECL_STRING_PARAM(funcname)
	PHP_RUNKIT_DECL_STRING_PARAM(arglist)
	PHP_RUNKIT_DECL_STRING_PARAM(code)
	char *delta = NULL, *delta_desc;
	int retval;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
			PHP_RUNKIT_STRING_SPEC "/" PHP_RUNKIT_STRING_SPEC PHP_RUNKIT_STRING_SPEC,
			PHP_RUNKIT_STRING_PARAM(funcname),
			PHP_RUNKIT_STRING_PARAM(arglist),
			PHP_RUNKIT_STRING_PARAM(code)) == FAILURE) {
		RETURN_FALSE;
	}

	/* UTODO */
	if (php_runkit_fetch_function(PHP_RUNKIT_STRING_TYPE(funcname), funcname, funcname_len, NULL, PHP_RUNKIT_FETCH_FUNCTION_REMOVE TSRMLS_CC) == FAILURE) {
		RETURN_FALSE;
	}

	if (zend_hash_del(EG(function_table), funcname, funcname_len + 1) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to remove old function definition for %s()", funcname);
		RETURN_FALSE;
	}

	spprintf(&delta, 0, "function %s(%s){%s}", funcname, arglist, code);

	if (!delta) {
		RETURN_FALSE;
	}

	delta_desc = zend_make_compiled_string_description("runkit created function" TSRMLS_CC);
	retval = zend_eval_string(delta, NULL, delta_desc TSRMLS_CC);
	efree(delta_desc);
	efree(delta);

	RETURN_BOOL(retval == SUCCESS);
}
/* }}} */

/* {{{ proto bool runkit_function_copy(string funcname, string targetname)
 */
PHP_FUNCTION(runkit_function_copy)
{
	PHP_RUNKIT_DECL_STRING_PARAM(sfunc)
	PHP_RUNKIT_DECL_STRING_PARAM(dfunc)
	zend_function *fe;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
			PHP_RUNKIT_STRING_SPEC "/" PHP_RUNKIT_STRING_SPEC "/",
			PHP_RUNKIT_STRING_PARAM(sfunc),
			PHP_RUNKIT_STRING_PARAM(dfunc)) == FAILURE) {
		RETURN_FALSE;
	}

	/* UTODO */
	PHP_RUNKIT_STRTOLOWER(dfunc);

	if (zend_hash_exists(EG(function_table), dfunc, dfunc_len + 1)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s() already exists", dfunc);
		RETURN_FALSE;
	}

	if (php_runkit_fetch_function(PHP_RUNKIT_STRING_TYPE(sfunc), sfunc, sfunc_len, &fe, PHP_RUNKIT_FETCH_FUNCTION_INSPECT TSRMLS_CC) == FAILURE) {
		RETURN_FALSE;
	}

	if (fe->type == ZEND_USER_FUNCTION) {
		PHP_RUNKIT_FUNCTION_ADD_REF(fe);
	} else {
		zend_hash_key hash_key;

		hash_key.nKeyLength = PHP_RUNKIT_STRING_LEN(dfunc, 1);
		PHP_RUNKIT_HASH_KEY(&hash_key) = estrndup(dfunc, PHP_RUNKIT_HASH_KEYLEN(&hash_key));
		if (!RUNKIT_G(misplaced_internal_functions)) {
			ALLOC_HASHTABLE(RUNKIT_G(misplaced_internal_functions));
			zend_hash_init(RUNKIT_G(misplaced_internal_functions), 4, NULL, php_runkit_hash_key_dtor, 0);
		}
		zend_hash_next_index_insert(RUNKIT_G(misplaced_internal_functions), (void*)&hash_key, sizeof(zend_hash_key), NULL);
	}

	if (zend_hash_add(EG(function_table), dfunc, dfunc_len + 1, fe, sizeof(zend_function), NULL) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to add refernce to new function name %s()", dfunc);
		zend_function_dtor(fe);
		RETURN_FALSE;
	}

	RETURN_TRUE;

}
/* }}} */
#endif /* PHP_RUNKIT_MANIPULATION */

/* {{{ proto bool runkit_return_value_used(void)
Does the calling function do anything with our return value? */
PHP_FUNCTION(runkit_return_value_used)
{
	zend_execute_data *ptr = EG(current_execute_data)->prev_execute_data;

	if (!ptr) {
		/* main() */
		RETURN_FALSE;
	}

	RETURN_BOOL(!(ptr->opline->result_type & EXT_TYPE_UNUSED));
}
/* }}} */
